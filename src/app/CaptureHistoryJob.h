#pragma once

#include <QObject>
#include <QString>
#include <QTimer>

namespace poptools {

class RapidOcrService;

// ============================================================
// CaptureHistoryJob：截图历史层后台服务（T-074，用户构想落地）。
//
// 职责：
//   * 标注：扫描 captures/年-月/*.png 中无侧车的图片 → RapidOCR 识别 →
//     写 <png>.label.json（OCR 文本 + 时间戳）——AI 截图管理的元数据层
//     （本地 OCR 零 token；深度解读由 dsh agent 按需经 MCP 工具做）；
//   * 清理：保留期 30 天，启动时 + 每日一轮（PNG 过期即删，侧车随删）。
//
// 节奏：启动 2min 首扫 + 15min 一轮（每轮最多标注 3 张，OCR 引擎懒启动、
// 逐张串行——与监控红线同口径：OCR 在服务子进程，主线程零重活）。
// ============================================================
class CaptureHistoryJob : public QObject
{
    Q_OBJECT
public:
    explicit CaptureHistoryJob(const QString &dataRoot, RapidOcrService *ocr,
                               QObject *parent = nullptr);

private slots:
    void tick(); // 一轮：清理（低频）+ 标注（有缺口时逐张）

private:
    void pruneExpired();
    QString nextUnlabeled() const; // 最旧的未标注 PNG（绝对路径）；空 = 无
    QString labelPathFor(const QString &png) const { return png + QStringLiteral(".label.json"); }

    QString m_dataRoot;
    RapidOcrService *m_ocr = nullptr;
    QTimer m_timer;
    bool m_busy = false; // OCR 在途标记（信号回包防串）
    int m_lastPruneDay = -1; // 每日一轮清理的当日标记
};

} // namespace poptools
