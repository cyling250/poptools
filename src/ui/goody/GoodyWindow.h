#pragma once

#include <QList>
#include <QWidget>
#include <functional>

class QLabel;
class QPlainTextEdit;
class QPushButton;
class QTextBrowser;
class QTimer;

namespace poptools {

// ============================================================
// Goody 管家聊天窗口：无边框 Qt::Tool 卡片（BoxWindow 窗口纪律）。
//
// 结构：标题栏（拖拽 + 清空/关闭）| 气泡流（QTextBrowser，markdown
// 渲染，流式期间 100ms 节流重绘）| 权限确认横条（隐藏式，三按钮）|
// 输入区（Enter 发送 / Shift+Enter 换行）。
// 流式渲染策略：不逐字 append（markdown 会抖），累积原文整页重绘。
// 主题：ThemeColors + objectName ID 选择器（P-022），随系统明暗切换。
// ============================================================
class GoodyWindow : public QWidget
{
    Q_OBJECT
public:
    explicit GoodyWindow(QWidget *parent = nullptr);

    void appendUserBubble(const QString &text);
    void beginAssistantBubble();                       // 开启流式气泡
    void appendAssistantDelta(const QString &delta);   // 流式增量（节流渲染）
    void finishAssistantBubble(const QString &fullText); // 收尾（原文为准）
    void appendSystemLine(const QString &text);        // 工具调用/结果提示行
    void showErrorLine(const QString &text);
    void clearTranscript();

    // 权限确认横条：展示请求 → 用户点按钮 → 一次性回调
    void promptPermission(const QString &tool, const QString &argsSummary,
                          const std::function<void(bool allowed, bool always)> &onDecide);

    void setBusy(bool on);
    bool isBusy() const { return m_busy; }

signals:
    void sendRequested(const QString &text);
    void stopRequested();
    void sessionCleared();
    void windowHidden(); // 关窗/隐藏（模块记忆几何用）

protected:
    void paintEvent(QPaintEvent *event) override; // 卡片底色 + 圆角
    bool eventFilter(QObject *watched, QEvent *event) override; // 标题栏拖拽 + Enter 发送
    void hideEvent(QHideEvent *event) override; // 发 windowHidden

private slots:
    void applyTheme();

private:
    void renderTranscript(); // 全量重绘（气泡列表 → markdown）
    void scheduleRender();   // 流式节流

    struct Bubble
    {
        QString role; // "user" / "assistant" / "system"
        QString text;
        bool error = false;
    };
    QList<Bubble> m_bubbles;

    QTextBrowser *m_transcript = nullptr;
    QPlainTextEdit *m_input = nullptr;
    QPushButton *m_sendBtn = nullptr;
    QWidget *m_permBar = nullptr;
    QLabel *m_permText = nullptr;
    QPushButton *m_permAllow = nullptr;
    QPushButton *m_permAlways = nullptr;
    QPushButton *m_permDeny = nullptr;
    QTimer *m_renderTimer = nullptr;

    QString m_streamBuf;   // 流式累积原文
    bool m_streaming = false;
    bool m_busy = false;
    QPoint m_dragAt;       // 标题栏拖拽起点（窗口坐标）
    bool m_dragging = false;

    std::function<void(bool, bool)> m_permCallback;
};

} // namespace poptools
