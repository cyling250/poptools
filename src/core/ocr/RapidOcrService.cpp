#include "RapidOcrService.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QPointer>
#include <QStandardPaths>
#include <QThreadPool>
#include <QUuid>

#include <algorithm>

namespace poptools {

namespace {
// 空闲自动回收阈值：识别完成后 3 分钟无新请求则结束子进程。
// 子进程常驻约 770MB（Paddle 推理库 + 模型），是全项目最大单一开销；
// 冷启动实测约 0.6s，回收后下次请求自动重启，代价可接受
constexpr int kOcrIdleRecycleMs = 3 * 60 * 1000;
} // namespace

// ---- 辅助函数 ----

// 四边形四个角 → 最小外接 QRect（取 min/max 的 x/y）；全部点非法时返回空矩形
//（调用方跳过。此前会返回 x=INT_MAX 的垃圾矩形，R3 #22）
static QRect quadToRect(const QJsonArray &box)
{
    // box: [[x1,y1],[x2,y2],[x3,y3],[x4,y4]]
    int minX = INT_MAX, minY = INT_MAX, maxX = INT_MIN, maxY = INT_MIN;
    bool anyValid = false;
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
            anyValid = true;
        }
    }
    if (!anyValid)
        return QRect();
    return QRect(minX, minY, maxX - minX, maxY - minY);
}

// ---- RapidOcrService ----

RapidOcrService::RapidOcrService(QObject *parent)
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
        fail(QStringLiteral("OCR 引擎无响应已终止（20 秒超时），请重试"));
    });

    // 空闲自动回收：识别完成即开表（finishRequest），新请求即停（recognizeBoxes）。
    // 超时 kill 子进程——CrashExit 处理器里 m_busy 为 false，不会误报
    m_idleTimer = new QTimer(this);
    m_idleTimer->setSingleShot(true);
    m_idleTimer->setInterval(kOcrIdleRecycleMs);
    connect(m_idleTimer, &QTimer::timeout, this, [this]
    {
        if (m_busy || !m_process)
            return;
        m_process->kill();
    });
}

// 请求收尾（不含 emit）：停超时看门狗、清临时文件、复位忙态，
// 并启动空闲自动回收倒计时（3 分钟无新请求则结束子进程省内存）
void RapidOcrService::finishRequest()
{
    m_timeoutTimer->stop();
    m_busy = false;
    cleanupTemp();
    m_idleTimer->start();
}

void RapidOcrService::fail(const QString &msg)
{
    if (!m_busy)
        return; // 异步启动化后失败来自事件处理器（errorOccurred/看门狗等），
                // 防止同一请求双重收尾、ocrBoxesFinished 双发
    finishRequest();
    emit ocrBoxesFinished(false, QVector<OcrBox>(), msg);
}

RapidOcrService::~RapidOcrService()
{
    cleanupTemp();
    if (m_process)
    {
        m_process->kill();
        m_process->waitForFinished(3000);
    }
}

QString RapidOcrService::engineDir()
{
    return QCoreApplication::applicationDirPath() + QStringLiteral("/rapidocr");
}

bool RapidOcrService::available() const
{
    const QString exe = engineDir() + QStringLiteral("/RapidOCR-json.exe");
    return QFileInfo::exists(exe);
}

void RapidOcrService::startEngine()
{
    if (m_process && m_process->state() != QProcess::NotRunning)
        return; // Running/Starting：已有实例在途（Starting → started 后发送命令）

    if (!m_process)
    {
        m_process = new QProcess(this);
        QObject::connect(m_process, &QProcess::readyReadStandardOutput,
                         this, &RapidOcrService::onReadyRead);
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
        // 启动失败（如引擎 exe 缺失）异步透出。旧实现 waitForStarted(5000) 最坏
        // 阻塞主线程 5 秒（红线 1），改为事件驱动，超时兜底见下方看门狗
        QObject::connect(m_process, &QProcess::errorOccurred, this,
                         [this](QProcess::ProcessError error)
        {
            if (error == QProcess::FailedToStart && m_busy)
                fail(QStringLiteral("无法启动 OCR 引擎进程"));
        });
        // 引擎就绪：发送识别命令（m_busy 防护：启动期间请求已被超时等路径收尾时不再发送）
        QObject::connect(m_process, &QProcess::started, this, [this]
        {
            if (m_busy)
                writeCommand();
        });
    }

    const QString exePath = engineDir() + QStringLiteral("/RapidOCR-json.exe");

    m_process->setWorkingDirectory(engineDir());
    // 固定简中配置（det/cls/rec/字典）——与原 PaddleOCR-json 空参数默认行为对齐；
    // 6 语言 rec 模型随包分发，语言切换为后续可选项（引擎按启动参数加载单套）
    m_process->start(exePath, QStringList{
                         QStringLiteral("--models=models"),
                         QStringLiteral("--det=ch_PP-OCRv3_det_infer.onnx"),
                         QStringLiteral("--cls=ch_ppocr_mobile_v2.0_cls_infer.onnx"),
                         QStringLiteral("--rec=ch_PP-OCRv3_rec_infer.onnx"),
                         QStringLiteral("--keys=dict_chinese.txt"),
                     }, QProcess::ReadWrite);
    // 启动阶段同样开表（不等待初始化完成，模型加载在后台进行）：启动挂死时
    // 看门狗 kill + 超时报错，m_busy 不可能永久卡真（started 后 writeCommand 重开表）
    m_timeoutTimer->start();
    m_idleTimer->stop();
}

void RapidOcrService::recognizeBoxes(const QImage &image)
{
    if (m_busy || image.isNull())
        return;
    m_busy = true;

    // 临时文件路径计算（纯字符串）留主线程；mkpath + PNG 编码落盘挪线程池
    // （红线 1：磁盘 IO 与大图编码禁入主线程，整幅选区 PNG 可秒级阻塞）。
    // QPointer 交付模式（SnapshotOcrPipeline/DailyAggregator 先例）：服务销毁后
    // 在途任务不得访问悬垂对象；image 按值捕获（COW）不受调用方后续改动影响
    cleanupTemp();
    const QString tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    m_tempFile = tempDir + QStringLiteral("/poptools_ocr_")
                 + QUuid::createUuid().toString(QUuid::WithoutBraces)
                 + QStringLiteral(".png");
    QPointer<RapidOcrService> self(this);
    QThreadPool::globalInstance()->start([self, image, path = m_tempFile, tempDir] {
        QDir().mkpath(tempDir);
        const bool ok = image.save(path, "PNG");
        if (!self)
        {
            QFile::remove(path); // 服务已销毁：临时图即弃，不留 %TEMP% 垃圾
            return;
        }
        QMetaObject::invokeMethod(self.data(), [self, ok] {
            if (!self)
                return;
            self->onTempImageReady(ok);
        }, Qt::QueuedConnection);
    });
}

// worker 落盘完成回主线程：成功则启动引擎（异步）发送命令，失败统一报错
void RapidOcrService::onTempImageReady(bool ok)
{
    if (!ok)
    {
        fail(QStringLiteral("无法写入 OCR 临时图片文件"));
        return;
    }
    startEngine();
}

void RapidOcrService::writeCommand()
{
    // 发送命令 (管道模式：JSON 命令 + 换行符；经 QJsonDocument 构造保证路径转义)
    const QByteArray cmd = QJsonDocument(
                               QJsonObject{{QStringLiteral("image_path"), m_tempFile}})
                               .toJson(QJsonDocument::Compact)
                           + '\n';
    m_buffer.clear();
    m_process->write(cmd);
    m_timeoutTimer->start(); // 识别超时看门狗开表（重开：启动阶段已计一部分）
    m_idleTimer->stop();     // 有活跃请求：暂停空闲回收倒计时
}

void RapidOcrService::onReadyRead()
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
                    if (r.isNull())
                        continue; // 四角全非法：跳过，不产出垃圾框
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

void RapidOcrService::cleanupTemp()
{
    if (!m_tempFile.isEmpty())
    {
        QFile::remove(m_tempFile);
        m_tempFile.clear();
    }
}

} // namespace poptools