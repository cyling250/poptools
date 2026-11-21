#pragma once

#include <QWidget>

class QLabel;
class QToolButton;
class QWidget;

namespace poptools {

class AutoHeightBrowser;

// ============================================================
// ProcessRow：dsh WebUI「过程行」的 Qt 还原（T-070）——工具调用等过程性
// 内容的统一折叠行。
//
// 形态（对齐 dsh ToolRow/DisclosureRow）：头部行 = 状态徽章点（8px 圆）
// + 工具名（13px Semibold）+ 分隔点 + 单行省略摘要；点击头部切换展开体
//（结果文本，AutoHeightBrowser 自动高度、超限内部滚动）。状态徽章语义
// 对齐 dsh：运行中蓝 / 完成绿 / 失败红。
// 颜色经 setTheme(const struct GoodyThemeTokens&) 随窗口主题重抛
//（窗口 applyTheme 遍历调用）；徽章色也可由 finish() 按状态直取。
// ============================================================
class ProcessRow : public QWidget
{
    Q_OBJECT
public:
    enum class State
    {
        Running,
        Ok,
        Error,
    };

    explicit ProcessRow(const QString &toolName, const QString &summary,
                        QWidget *parent = nullptr);

    // 工具执行结束：更新徽章态与结果体（result 为 JSON 串，原样展示）
    void finish(State state, const QString &resultText);
    // 运行中摘要刷新（参数全量到达时）
    void setSummary(const QString &summary);

private:
    void refreshHead(); // 按当前态重写头部文本（徽章色经 m_dot QSS）

    QWidget *m_halo = nullptr;   // 徽章外圈光环容器（运行中蓝晕，收束态透明）
    QLabel *m_dot = nullptr;     // 状态徽章点（8px 圆）
    QToolButton *m_head = nullptr; // 头部折叠行（整行可点）
    AutoHeightBrowser *m_result = nullptr; // 展开体
    QString m_toolName;
    QString m_summary;
    State m_state = State::Running;
};

} // namespace poptools
