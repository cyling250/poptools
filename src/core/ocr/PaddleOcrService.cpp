#include "PaddleOcrService.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QStandardPaths>
#include <QUuid>

#include <algorithm>

namespace poptools {

// ---- OcrBox 兼容：PaddleOcrBox 转 OcrBox ----
// 外部使用 OcrBox（定义在 OcrService.h），这里用 PaddleOcrBox 内部处理，
// 最终 emit 时转换为 QVector<OcrBox>。但为了简化，外部直接使用 OcrBox，
// 保持与 OcrController 兼容。
// 注意：PaddleOcrBox 只在此文件内使用，外部信号用 OcrBox 对齐旧接口。

// ---- 辅助函数 ----

// 四边形四个角 → 最小外接 QRect（取 min/max 的 x/y）
static QRect quadToRect(const QJsonArray &box)
{
    // box: [[x1,y1],[x2,y2],[x3,y3],[x4,y4]]
    int minX = INT_MAX, minY = INT_MAX, maxX = INT_MIN, maxY = INT_MIN;
    for (const QJsonValue &pt : box)
    {
        const QJsonArray p = pt.toArray();
        if (p.size() >= 2)
        {
            const int x = p[0].toInt();
            const int y = p[1].toInt();
            minX = std::min(minX, x);
            minY = std::min(minY, y);
            maxX = std::max(maxX, x);
            maxY = std::max(maxY, y);
        }
    }
    return QRect(minX, minY, maxX - minX, maxY - minY);
}

// ---- PaddleOcrService ----

PaddleOcrService::PaddleOcrService(QObject *parent)
    : QObject(parent)
{
    // 识别超时看门狗：子进程活着但不响应时 m_busy 原先永真，后续请求被静默
    // 丢弃且无报错（用户视角"扫描永远转圈"）。超时杀进程，下次请求自动重启
    m_timeoutTimer = new QTimer(this);
    m_timeoutTimer->setSingleShot(true);
    m_timeoutTimer->setInterval(20000);
    connect(m_timeoutTimer, &QTimer::timeout, this, [this]
    {
        if (!m_busy)
            return;
        if (m_process)
            m_process->kill(); // 触发 CrashExit finished，收尾路径不会重复报错
        fail(QStringLiteral("OCR 识别超时（20 秒无响应），引擎已重启，请重试"));
    });
}

// 请求收尾（不含 emit）：停超时看门狗、清临时文件、复位忙态
void PaddleOcrService::finishRequest()
{
    m_timeoutTimer->stop();
    m_busy = false;
    cleanupTemp();
}

void PaddleOcrService::fail(const QString &msg)
{
    finishRequest();
    emit ocrBoxesFinished(false, QVector<OcrBox>(), msg);
}

PaddleOcrService::~PaddleOcrService()
{
    cleanupTemp();
    if (m_process)
    {
        m_process->kill();
        m_process->waitForFinished(3000);
    }
}

QString PaddleOcrService::paddleDir()
{
    return QCoreApplication::applicationDirPath() + QStringLiteral("/paddleocr");
}

bool PaddleOcrService::available() const
{
    const QString exe = paddleDir() + QStringLiteral("/PaddleOCR-json.exe");
    return QFileInfo::exists(exe);
}

void PaddleOcrService::ensureProcess()
{
    if (m_process && m_process->state() == QProcess::Running)
        return;

    if (!m_process)
    {
        m_process = new QProcess(this);
        QObject::connect(m_process, &QProcess::readyReadStandardOutput,
                         this, &PaddleOcrService::onReadyRead);
        // 进程异常退出时自动重启（下次请求时）
        QObject::connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                         this, [this](int /*exitCode*/, QProcess::ExitStatus status)
        {
            if (status == QProcess::CrashExit && m_busy)
            {
                // 正在处理中崩溃 → 报错
                finishRequest();
                emit ocrBoxesFinished(false, QVector<OcrBox>(),
                                      QStringLiteral("OCR 引擎进程意外退出"));
            }
        });
    }

    const QString exePath = paddleDir() + QStringLiteral("/PaddleOCR-json.exe");
    const QString workDir = paddleDir();

    m_process->setWorkingDirectory(workDir);
    m_process->start(exePath, QStringList(), QProcess::ReadWrite);
    // 等待进程启动（不等待初始化完成，模型加载在后台进行）
    if (!m_process->waitForStarted(5000))
    {
        fail(QStringLiteral("无法启动 OCR 引擎进程"));
        return;
    }
}

void PaddleOcrService::recognizeBoxes(const QImage &image)
{
    if (m_busy || image.isNull())
        return;
    m_busy = true;
    sendImage(image);
}

void PaddleOcrService::sendImage(const QImage &image)
{
    // 保存图片到临时文件
    cleanupTemp();
    const QString tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    QDir().mkpath(tempDir);
    m_tempFile = tempDir + QStringLiteral("/poptools_ocr_")
                 + QUuid::createUuid().toString(QUuid::WithoutBraces)
                 + QStringLiteral(".png");
    if (!image.save(m_tempFile, "PNG"))
    {
        fail(QStringLiteral("无法写入 OCR 临时图片文件"));
        return;
    }

    // 确保进程运行
    ensureProcess();
    if (!m_process || m_process->state() != QProcess::Running)
    {
        fail(QStringLiteral("OCR 引擎未运行"));
        return;
    }

    // 发送命令 (管道模式：JSON 命令 + 换行符；经 QJsonDocument 构造保证路径转义)
    const QByteArray cmd = QJsonDocument(
                               QJsonObject{{QStringLiteral("image_path"), m_tempFile}})
                               .toJson(QJsonDocument::Compact)
                           + '\n';
    m_buffer.clear();
    m_process->write(cmd);
    m_timeoutTimer->start(); // 识别超时看门狗开表
}

void PaddleOcrService::onReadyRead()
{
    m_buffer.append(m_process->readAllStandardOutput());

    // 按行分割处理（每行一个完整 JSON 响应）
    while (true)
    {
        const int nlPos = m_buffer.indexOf('\n');
        if (nlPos < 0)
            break; // 没有完整行，等更多数据

        const QByteArray line = m_buffer.left(nlPos).trimmed();
        m_buffer.remove(0, nlPos + 1);

        if (line.isEmpty())
            continue;

        // 解析 JSON
        const QJsonDocument doc = QJsonDocument::fromJson(line);
        if (!doc.isObject())
            continue; // 非 JSON 行（如启动日志），跳过

        const QJsonObject root = doc.object();
        const int code = root.value(QStringLiteral("code")).toInt();

        if (code == 100)
        {
            // 成功识别
            const QJsonArray data = root.value(QStringLiteral("data")).toArray();
            QVector<OcrBox> boxes;

            for (const QJsonValue &val : data)
            {
                const QJsonObject item = val.toObject();
                const QString text = item.value(QStringLiteral("text")).toString().trimmed();
                if (text.isEmpty())
                    continue;

                // 构建 OcrBox
                const QJsonArray boxArr = item.value(QStringLiteral("box")).toArray();
                if (!boxArr.isEmpty())
                {
                    const QRect r = quadToRect(boxArr);
                    const double score = item.value(QStringLiteral("score")).toDouble();
                    OcrBox b;
                    b.rect = r;
                    b.text = text;
                    b.conf = score * 100.0; // 转为 0-100 分数
                    boxes.append(b);
                }
            }

            finishRequest();
            emit ocrBoxesFinished(true, boxes, QString());
        }
        else if (code == 101)
        {
            // 未识别到文字
            finishRequest();
            const QString msg = root.value(QStringLiteral("msg")).toString();
            emit ocrBoxesFinished(false, QVector<OcrBox>(),
                                  msg.isEmpty() ? QStringLiteral("未识别到文字") : msg);
        }
        else
        {
            // 其他错误码
            finishRequest();
            const QString msg = root.value(QStringLiteral("msg")).toString();
            emit ocrBoxesFinished(false, QVector<OcrBox>(),
                                  msg.isEmpty() ? QStringLiteral("OCR 识别失败") : msg);
        }
        return; // 只处理第一个响应行（后续行留到下次 readyRead）
    }
}

void PaddleOcrService::cleanupTemp()
{
    if (!m_tempFile.isEmpty())
    {
        QFile::remove(m_tempFile);
        m_tempFile.clear();
    }
}

} // namespace poptools