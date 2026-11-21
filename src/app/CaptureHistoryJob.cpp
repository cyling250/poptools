#include "CaptureHistoryJob.h"

#include "core/ocr/RapidOcrService.h"

#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>

namespace poptools {

namespace {
constexpr int kRetentionDays = 30;     // 历史保留期（截图历史定时清理）
constexpr int kLabelPerTick = 3;       // 每轮最多标注张数（OCR 逐张串行）
constexpr int kOcrTimeoutMs = 120000;  // 单张 OCR 看门狗（引擎冷启动余量）
} // namespace

CaptureHistoryJob::CaptureHistoryJob(const QString &dataRoot, RapidOcrService *ocr,
                                     QObject *parent)
    : QObject(parent)
    , m_dataRoot(dataRoot)
    , m_ocr(ocr)
{
    m_timer.setInterval(15 * 60 * 1000); // 15min 一轮
    connect(&m_timer, &QTimer::timeout, this, &CaptureHistoryJob::tick);
    QTimer::singleShot(2 * 60 * 1000, this, &CaptureHistoryJob::tick); // 启动 2min 首扫
    m_timer.start();
}

void CaptureHistoryJob::pruneExpired()
{
    const qint64 cutoff =
        QDateTime::currentDateTime().addDays(-kRetentionDays).toMSecsSinceEpoch();
    const QDir base(m_dataRoot + QStringLiteral("/captures"));
    if (!base.exists())
        return;
    for (const QFileInfo &month : base.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        bool monthEmpty = true;
        for (const QFileInfo &fi :
             QDir(month.absoluteFilePath()).entryInfoList({"*.png"}, QDir::Files)) {
            if (fi.lastModified().toMSecsSinceEpoch() < cutoff) {
                QFile::remove(fi.absoluteFilePath());
                QFile::remove(labelPathFor(fi.absoluteFilePath()));
            } else {
                monthEmpty = false;
            }
        }
        if (monthEmpty)
            base.rmdir(month.absoluteFilePath()); // 空月份目录回收
    }
}

QString CaptureHistoryJob::nextUnlabeled() const
{
    const QDir base(m_dataRoot + QStringLiteral("/captures"));
    if (!base.exists())
        return QString();
    QStringList months;
    for (const QFileInfo &month : base.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot))
        months << month.absoluteFilePath();
    months.sort(); // 月份升序：最旧的先标注（与清理衔接）
    for (const QString &month : months) {
        for (const QFileInfo &fi :
             QDir(month).entryInfoList({"*.png"}, QDir::Files, QDir::Name)) {
            if (!QFileInfo::exists(labelPathFor(fi.absoluteFilePath())))
                return fi.absoluteFilePath();
        }
    }
    return QString();
}

void CaptureHistoryJob::tick()
{
    // 清理每日一轮（启动首轮即触发当日标记）
    const int today = QDate::currentDate().dayOfYear();
    if (m_lastPruneDay != today) {
        m_lastPruneDay = today;
        pruneExpired();
    }
    if (m_busy || !m_ocr || !m_ocr->available())
        return;
    const QString png = nextUnlabeled();
    if (png.isEmpty())
        return;
    QImage image(png);
    if (image.isNull()) {
        QFile::remove(png); // 死图（半截/被删一半）：清掉防反复扫
        return;
    }
    m_busy = true;
    QVector<OcrBox> boxes;
    QString failure;
    QEventLoop loop;
    QTimer::singleShot(kOcrTimeoutMs, &loop, &QEventLoop::quit);
    connect(m_ocr, &RapidOcrService::ocrBoxesFinished, &loop,
            [&](bool ok, const QVector<OcrBox> &b, const QString &err) {
                boxes = b;
                failure = err;
                if (!ok)
                    failure = failure.isEmpty() ? QStringLiteral("OCR 失败") : failure;
                loop.quit();
            });
    m_ocr->recognizeBoxes(image);
    loop.exec();
    m_busy = false;

    if (!failure.isEmpty()) {
        // 引擎类失败留待下轮重试（不给该图打永久"失败"标——侧车缺位即重试）
        return;
    }
    sortOcrBoxesReadingOrder(boxes);
    QStringList lines;
    for (const auto &box : boxes)
        lines << box.text;
    QJsonObject label{{"file", png},
                      {"labeled_at", QDateTime::currentDateTime().toString(Qt::ISODate)},
                      {"ocr", lines.join(QLatin1Char('\n'))}};
    QFile f(labelPathFor(png));
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(label).toJson(QJsonDocument::Compact));
}

} // namespace poptools
