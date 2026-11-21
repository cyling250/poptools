// ============================================================
// poptools_mcp_server：PopTools MCP 能力桥服务器（T-074 M2）。
//
// 形态：MCP stdio 服务器（newline-delimited JSON-RPC 2.0）——由 dsh 的
// mcp-manager 作为子进程拉起（profiles/web/cordis.patch.yml 配置 command），
// 桌面能力以工具面暴露给 dsh agent：
//   poptools_activity_daily      活动监控日志读取（日报/「今天干了什么」取数）
//   poptools_write/read_daily_digest     日报落盘/读取（goody/daily 约定路径，
//                                桌面日历摘要链路续命）
//   poptools_write/read_monthly_digest   月报落盘/读取（goody/monthly）
//   poptools_capture_screenshot  整虚拟桌面截图（PNG 落临时目录）
//   poptools_ocr_image           RapidOCR 离线识别（图片路径 → 文本）
//
// 实现纪律：
//   * 工具本体在 McpTools（纯函数可单测），本文件只做 MCP 框架 + OCR 异步；
//   * stdin 读取线程 → 主线程队列处理（QMetaObject::invokeMethod），
//     stdout 写入互斥；stdin EOF = 客户端关闭 → 退出；
//   * OCR 为异步服务调用，QEventLoop 等待信号（120s 看门狗）。
// ============================================================

#include "McpTools.h"

#include "core/ocr/RapidOcrService.h"

#include <QGuiApplication>
#include <QEventLoop>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QStandardPaths>
#include <QStringList>
#include <QTimer>

#include <atomic>
#include <iostream>
#include <string>
#include <thread>

namespace {

QMutex g_stdoutMutex;

void writeLine(const QByteArray &line)
{
    QMutexLocker locker(&g_stdoutMutex);
    std::cout << line.constData() << "\n";
    std::cout.flush();
}

void sendResult(const QJsonValue &id, const QJsonObject &result)
{
    QJsonObject r{{"jsonrpc", QStringLiteral("2.0")}, {"id", id}, {"result", result}};
    writeLine(QJsonDocument(r).toJson(QJsonDocument::Compact));
}

void sendError(const QJsonValue &id, int code, const QString &message)
{
    QJsonObject r{{"jsonrpc", QStringLiteral("2.0")},
                  {"id", id},
                  {"error", QJsonObject{{"code", code}, {"message", message}}}};
    writeLine(QJsonDocument(r).toJson(QJsonDocument::Compact));
}

QJsonObject wrapPayload(const poptools::McpToolResult &result, bool *isError)
{
    *isError = result.isError;
    return result.payload;
}

} // namespace

int main(int argc, char *argv[])
{
    // QGuiApplication（非 Core）：poptools_capture_screenshot 需要 QScreen；
    // 应用名与主程序同口径（AppLocalDataLocation = %LOCALAPPDATA%/PopTools/PopTools）
    QGuiApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("PopTools"));
    QCoreApplication::setOrganizationName(QStringLiteral("PopTools"));

    poptools::RapidOcrService ocr(&app);
    std::atomic<bool> closed{false};

    // 工具分发（tools/call）：纯文件工具走 McpTools 同步实现，OCR 走异步等待
    auto dispatchTool = [&](const QString &name, const QJsonObject &args,
                            bool *isError) -> QJsonObject {
        using namespace poptools;
        const QString dataRoot =
            QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
        *isError = false;
        if (name == QLatin1String("poptools_activity_daily"))
            return wrapPayload(
                runActivityDaily(dataRoot + QStringLiteral("/monitor"), args), isError);
        if (name == QLatin1String("poptools_write_daily_digest"))
            return wrapPayload(
                runWriteDailyDigest(dataRoot + QStringLiteral("/goody"), args), isError);
        if (name == QLatin1String("poptools_read_daily_digest"))
            return wrapPayload(
                runReadDailyDigest(dataRoot + QStringLiteral("/goody"), args), isError);
        if (name == QLatin1String("poptools_write_monthly_digest"))
            return wrapPayload(
                runWriteMonthlyDigest(dataRoot + QStringLiteral("/goody"), args), isError);
        if (name == QLatin1String("poptools_read_monthly_digest"))
            return wrapPayload(
                runReadMonthlyDigest(dataRoot + QStringLiteral("/goody"), args), isError);
        if (name == QLatin1String("poptools_capture_screenshot"))
            return wrapPayload(runCaptureScreenshot(args), isError);

        if (name == QLatin1String("poptools_ocr_image")) {
            const QString path = args.value(QStringLiteral("image_path")).toString();
            QImage image(path);
            if (image.isNull())
                return QJsonObject{{"ok", false},
                                   {"error", QStringLiteral("图片加载失败：%1").arg(path)}};
            if (!ocr.available())
                return QJsonObject{{"ok", false},
                                   {"error",
                                    QStringLiteral("OCR 引擎不可用（RapidOCR-json 缺失）")}};
            QVector<poptools::OcrBox> boxes;
            QString failure;
            QEventLoop loop;
            QTimer::singleShot(120000, &loop, &QEventLoop::quit); // 冷启动余量看门狗
            QObject::connect(&ocr, &poptools::RapidOcrService::ocrBoxesFinished, &loop,
                             [&](bool ok, const QVector<poptools::OcrBox> &b,
                                 const QString &err) {
                                 boxes = b;
                                 failure = err;
                                 loop.quit();
                             });
            ocr.recognizeBoxes(image);
            loop.exec();
            if (!failure.isEmpty())
                return QJsonObject{{"ok", false}, {"error", failure}};
            poptools::sortOcrBoxesReadingOrder(boxes);
            QStringList lines;
            for (const auto &box : boxes)
                lines << box.text;
            return QJsonObject{{"ok", true},
                               {"text", lines.join(QLatin1Char('\n'))},
                               {"image_path", path}};
        }

        *isError = true;
        return QJsonObject{{"error", QStringLiteral("未知工具：%1").arg(name)}};
    };

    // 请求处理（主线程）：MCP 方法分发
    auto handleMessage = [&](const QString &line) {
        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(line.toUtf8(), &parseError);
        if (doc.isNull() || !doc.isObject()) {
            if (!line.trimmed().isEmpty())
                sendError(QJsonValue(), -32700, // 解析错误：id 恒 null（MCP 规范）
                          QStringLiteral("JSON 解析失败：%1").arg(parseError.errorString()));
            return;
        }
        const QJsonObject req = doc.object();
        const QString method = req.value(QStringLiteral("method")).toString();
        const QJsonValue id = req.value(QStringLiteral("id"));
        const bool isNotification = id.isUndefined();
        const QJsonObject params = req.value(QStringLiteral("params")).toObject();

        if (method == QLatin1String("initialize")) {
            const QString requested =
                params.value(QStringLiteral("protocolVersion")).toString();
            sendResult(id,
                       QJsonObject{{"protocolVersion",
                                    requested.isEmpty() ? QStringLiteral("2025-06-18")
                                                        : requested},
                                   {"capabilities", QJsonObject{{"tools", QJsonObject{}}}},
                                   {"serverInfo",
                                    QJsonObject{{"name", QStringLiteral("poptools")},
                                                {"version", QStringLiteral("1.0.0")}}}});
            return;
        }
        if (method.startsWith(QLatin1String("notifications/")))
            return; // 通知无应答
        if (method == QLatin1String("ping")) {
            sendResult(id, QJsonObject{});
            return;
        }
        if (method == QLatin1String("tools/list")) {
            sendResult(id, QJsonObject{{"tools", poptools::mcpToolDescriptors()}});
            return;
        }
        if (method == QLatin1String("tools/call")) {
            const QString name = params.value(QStringLiteral("name")).toString();
            const QJsonObject args = params.value(QStringLiteral("arguments")).toObject();
            bool isError = false;
            const QJsonObject payload = dispatchTool(name, args, &isError);
            // 深层嵌套走 insert（MSVC 大括号初始化列表炸 C2440，McpTools 同教训）
            QJsonObject contentBlock;
            contentBlock.insert(QStringLiteral("type"), QStringLiteral("text"));
            contentBlock.insert(QStringLiteral("text"),
                                QString::fromUtf8(QJsonDocument(payload).toJson(
                                    QJsonDocument::Compact)));
            QJsonArray content;
            content.append(contentBlock);
            QJsonObject result;
            result.insert(QStringLiteral("content"), content);
            result.insert(QStringLiteral("isError"), isError);
            sendResult(id, result);
            return;
        }
        if (!isNotification)
            sendError(id, -32601, QStringLiteral("未知方法：%1").arg(method));
    };

    // stdin 读取线程：行 → 主线程队列（跨线程 invokeMethod 到 app 线程）
    std::thread reader([&] {
        std::string line;
        while (!closed.load() && std::getline(std::cin, line)) {
            const QString qline = QString::fromUtf8(QByteArray::fromStdString(line));
            QMetaObject::invokeMethod(
                &app,
                [&handleMessage, qline] { handleMessage(qline); },
                Qt::QueuedConnection);
        }
        closed.store(true);
        QMetaObject::invokeMethod(
            &app,
            [] { QCoreApplication::quit(); },
                                  Qt::QueuedConnection);
    });

    const int rc = app.exec();
    closed.store(true);
    // 读线程阻塞在管道 getline 上：客户端关闭管道后 getline 返回、线程自然
    // 结束；此处 detach（强 join 会挂，进程退出由 OS 回收）
    if (reader.joinable())
        reader.detach();
    return rc;
}
