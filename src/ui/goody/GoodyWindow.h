#pragma once

#include <QElapsedTimer>
#include <QList>
#include <QWidget>
#include <functional>

#include "MarkdownRenderer.h"
#include "ui/theming/FluentTheme.h"
#include "core/assistant/ChatSession.h" // SessionSummary（makeSessionRow 参数）
#include "MessageBubble.h"              // MessageBubble::Role

#include <QHash>

class QFrame;
class QLabel;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QScrollArea;
class QToolButton;
class QVBoxLayout;

namespace poptools {

class ChatSession;
class ProcessRow;

// ============================================================
// Goody 管家聊天窗（T-050-P2 全新重构：居中卡片聊天窗）。
//
// 结构：
//   ┌ 标题栏（侧栏开关 | Goody 管家 | 清空 | ✕，可拖拽）──────┐
//   ├ 会话侧栏（可折叠） │ 对话区（气泡列表 QScrollArea 滚动流）┤
//   │   ＋新对话          │  user 右对齐 accent 贴纸气泡        │
//   │   会话条目列表      │  assistant 左对齐卡片气泡（思考折叠 │
//   │   标题+活跃时间     │   区 + markdown 定稿 + 代码高亮/复制│
//   │                    │   + hover 四件套工具条）             │
//   │                    │  工具确认卡片（隐藏式，三动作）      │
//   └                    ┴ 输入区（多行输入 + 发送/停止）─────────┘
//
// 渲染管线：contentDelta chunk → MarkdownRenderer（节流状态机，150ms
// AI 代决）→ 当前 assistant 气泡增量纯文本追加（打字机粒度、零重渲染）
// → 回合结束一次 setMarkdown 定稿（代码高亮）。气泡列表懒构建：会话
// 切换时一次性重建，流式只动当前气泡。
//
// 自动滚动只在本就处于底部时跟随（用户上翻不抢滚动条）。
// 性能红线：无常驻定时器（渲染节流由 chunk 驱动，时间来自 QElapsedTimer）。
//
// 几何：assistant/windowGeometry 新键记忆；无记忆时居中弹出（默认
// 720×560，可缩放）。主题：ThemeColors + ID 选择器（P-022）+ 关键颜色
// 双通道（P-023）。
// ============================================================
class GoodyWindow : public QWidget
{
    Q_OBJECT
public:
    explicit GoodyWindow(QWidget *parent = nullptr);

    // 注入会话域对象（多会话侧栏/历史气泡/四件套的数据源）；窗口只读
    // 会话数据并转发域操作请求给模块，自身不直接改写会话
    void setSession(ChatSession *session);

    // ---- 回合链路（GoodyModule 驱动） ----
    void appendUserBubble(const QString &text);
    void beginAssistantBubble();                       // 开启流式气泡
    void appendAssistantDelta(const QString &delta);   // 正文流式增量（节流渲染）
    void appendReasoningDelta(const QString &delta);   // 思考流式增量（150ms 第二道节流线，T-050-FIX2）
    void finishAssistantBubble(const QString &fullText,
                               const QString &reasoning = QString()); // 定稿
    void appendSystemLine(const QString &text);        // 工具调用/结果提示行
    void showErrorLine(const QString &text);

    // 权限确认卡片：展示请求 → 用户点按钮 → 一次性回调
    void promptPermission(const QString &tool, const QString &argsSummary,
                          const std::function<void(bool allowed, bool always)> &onDecide);

    void setBusy(bool on);
    bool isBusy() const { return m_busy; }
    // 工具执行期标志（模块注入）：与 busy 态共同决定「清空」按钮可用性
    //（工具期防孤儿 tool 序列 B-2；busy 期防清空后迟到回包写坏会话 C-N2）
    void setToolPhase(bool on);

    // 会话切换/编辑重发后由模块触发整列表重建（气泡懒构建：一次性）
    void reloadFromSession();
    // 编辑重发的输入框预填（用户可改后重发）
    void setDraftInput(const QString &text);

signals:
    void sendRequested(const QString &text);
    void stopRequested();
    void sessionCleared();
    void windowHidden(); // 关窗/隐藏（模块记忆悬浮球回归用）
    // ---- T-050-P2 会话侧栏与消息四件套（域操作在模块侧守卫执行） ----
    void sessionSwitchRequested(const QString &id);
    void sessionCreateRequested();
    void sessionDeleteRequested(const QString &id);
    void editResendRequested(int sessionIndex);
    void messageDeleteRequested(int sessionIndex);
    void regenerateRequested();

protected:
    void paintEvent(QPaintEvent *event) override; // 卡片底色 + 圆角
    bool eventFilter(QObject *watched, QEvent *event) override; // 标题栏拖拽 + Enter 发送
    void hideEvent(QHideEvent *event) override; // 几何落盘 + 发 windowHidden
    void resizeEvent(QResizeEvent *event) override; // 气泡宽度约束重设

private slots:
    void applyTheme();
    void onSessionDataChanged(); // ChatSession::changed（侧栏/气泡联动）

private:
    void buildSidebar();
    void buildChatPane();
    void addBubble(MessageBubble *bubble);
    void clearBubbles();               // 清空当前展示（不touch会话域数据）
    void appendWelcome();              // 空会话欢迎提示行
    int bubbleMessageIndex(MessageBubble *target) const; // 气泡→会话消息下标
    void refreshSessionList();         // 侧栏重建（流式期间推迟）
    QWidget *makeSessionRow(const ChatSession::SessionSummary &s,
                            bool dark); // 单条侧栏行装配（T-062 抽取）
    void markRegenerateOnLast();       // 仅最后一条 assistant 气泡显示"重新生成"
    void updateMaxWidths();            // 窗口缩放 → 气泡宽度上限与消息列居中
    int maxBubblePaneWidth() const;    // 气泡可用宽度（视口与消息列上限取小）
    void adjustInputHeight();          // 输入框自适应高度
    void scrollIfAtBottom();           // 仅在本就贴底时跟随（force=false 委托）
    void scrollToBottomNow(bool force); // force=true 强制贴底（"必须看见"场景）
    void onBubbleAction(MessageBubble *bubble, int action);
    void updateClearBtnState();        // busy/工具期联合刷新「清空」等（B-2/C-N2）
    void updateSessionUiLocked();      // busy/工具期联合禁侧栏操作
    // applyTheme 拆分（T-062）：窗口级 QSS 与控件调色板钉扎两段各归其位，
    // 气泡重抛/侧栏刷新留主干——纯函数分解，调用顺序不变
    // （T-070：配色整体切 GoodyTheme，dsh WebUI 形态）
    void applyWindowQss(bool dark);
    void applyControlPalettes(bool dark);

public:
    // ---- T-070 工具过程行（dsh ToolRow 形态）：模块在工具发起/结束时调，
    // 行插在消息流内（随列表重建清空） ----
    void toolRowStarted(const QString &callId, const QString &name,
                        const QString &argsSummary);
    void toolRowFinished(const QString &callId, bool ok, const QString &resultText);

private:

    ChatSession *m_session = nullptr;
    MarkdownRenderer m_renderer;       // 流式渲染状态机（纯逻辑，150ms 节流）
    // 两条节流线刻意分置（T-062 注）：正文节流在 MarkdownRenderer（状态机 +
    // ``` 围栏跨 chunk 扫描），思考节流在窗口（纯时限）——结构对称但抽公共
    // 基类属过度设计，保持分置
    QElapsedTimer m_streamClock;       // 渲染节流时钟（chunk 驱动，无常驻定时器）
    QElapsedTimer m_reasonClock;       // T-050-FIX2：reasoning 第二道节流线时钟（同款模式）
    QString m_reasonPending;           // T-050-FIX2：节流线内累积、到线一次性 flush 的思考增量

    // 侧栏
    QWidget *m_sidebar = nullptr;
    QPushButton *m_newChatBtn = nullptr;
    QListWidget *m_sessionList = nullptr;
    bool m_sessionListDirty = false;   // 流式期间侧栏刷新推迟标记

    // 对话区
    QScrollArea *m_scroll = nullptr;
    QWidget *m_msgList = nullptr;
    QVBoxLayout *m_msgLay = nullptr;
    QList<MessageBubble *> m_bubbles;
    QHash<QString, ProcessRow *> m_processRows; // callId→工具过程行（T-070）
    QString m_builtSessionId;          // 气泡列表当前对应的会话 id
    bool m_atBottom = true;            // 用户本就贴底才自动跟随

    // 输入区（T-070 composer 卡）
    QFrame *m_composerCard = nullptr;  // 输入卡片容器（圆角大卡，dsh composer）
    QPlainTextEdit *m_input = nullptr;
    QPushButton *m_sendBtn = nullptr;
    QPushButton *m_clearBtn = nullptr;
    QToolButton *m_sidebarToggle = nullptr;

    // 权限确认卡片
    QWidget *m_permBar = nullptr;
    QLabel *m_permText = nullptr;
    QPushButton *m_permAllow = nullptr;
    QPushButton *m_permAlways = nullptr;
    QPushButton *m_permDeny = nullptr;

    // 流式状态
    bool m_streaming = false;
    bool m_busy = false;
    bool m_toolPhase = false; // 工具执行期（模块注入，与 busy 联合禁「清空」）
    MessageBubble *m_streamBubble = nullptr;
    QString m_streamReasoning;         // 流式思考累积（字数显示与定稿兜底）

    QPoint m_dragAt;                   // 标题栏拖拽起点（窗口坐标）
    bool m_dragging = false;
    std::function<void(bool, bool)> m_permCallback;
};

} // namespace poptools
