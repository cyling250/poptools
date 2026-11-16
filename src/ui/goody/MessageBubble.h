#pragma once

#include <QFrame>
#include <QTextBrowser>

#include <initializer_list>
#include <utility>

#include "ui/ThemeColors.h"

class QLabel;
class QToolButton;
class QVBoxLayout;

namespace poptools {

class GoodyCodeHighlighter;

// 气泡动作四件套（T-050-P2）。语义（需求定案）：
//   Copy          复制消息原文（assistant 复制 markdown 原文）
//   EditResend    仅 user：从该消息截断其后全部消息 + 输入框预填该消息文本
//   Regenerate    仅 assistant（且只在最后一条）：丢弃最后一条回复并重问
//   Delete        删除该条（assistant 删除连带其 reasoning——reasoning 内嵌
//                 在消息对象里随消息整体删除）
enum BubbleAction
{
    BubbleCopy = 0,
    BubbleEditResend = 1,
    BubbleRegenerate = 2,
    BubbleDelete = 3,
};

// ============================================================
// 定高自适应文本浏览器：气泡正文用——按文档实际高度收紧自身高度
//（超高度上限后转内部滚动），保证气泡列表由外部 QScrollArea 统一滚动。
// ============================================================
class AutoHeightBrowser : public QTextBrowser
{
public:
    explicit AutoHeightBrowser(QWidget *parent = nullptr);

    void adjustHeight();               // 依文档尺寸设置固定高（幂等防抖）
    void setHeightCap(int cap) { m_heightCap = cap; }

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    int m_heightCap = 420; // 单气泡内部滚动阈值
};

// ============================================================
// MessageBubble：单条消息气泡（T-050-P2 聊天窗重构的核心组件）。
//
// 结构（assistant）：圆角卡 QFrame ⊃ [思考折叠区（默认折叠）+ 正文
// QTextBrowser + 悬浮工具条（hover 出现，overlay 不占布局）]；
// 流式期间正文走"增量纯文本追加"（打字机粒度、零重渲染），流结束后
// 一次 setMarkdown 定稿（Qt markdown 导入 + GoodyCodeHighlighter 对
// ``` 围栏块做语法高亮），代码块行 hover 时右上浮"复制"按钮。
//
// 关键颜色纪律（P-022/P-023）：控件 objectName + ID 选择器；颜色由
// applyTheme(ThemeColors) 写入控件自身内联样式 + 调色板三组钉扎。
// ============================================================
class MessageBubble : public QFrame
{
    Q_OBJECT
public:
    enum Role
    {
        User,
        Assistant,
        System,
    };

    MessageBubble(Role role, QWidget *parent = nullptr);

    // 内容填充（按角色二选一路径）
    void setUserText(const QString &text);
    void setSystemText(const QString &text, bool error);
    // assistant 流式：纯文本增量追加 + 定稿
    void beginStream();
    void appendStreamText(const QString &delta);
    void finalizeMarkdown(const QString &fullText, bool endedInFence);
    // assistant 思考折叠区：流式增量（默认折叠，标题行报字数）+ 定稿
    void appendReasoningDelta(const QString &delta);
    void setReasoningFinal(const QString &full);

    // 元数据与状态
    Role role() const { return m_role; }
    void setRegenerateVisible(bool on); // 仅最后一条 assistant 气泡为 true
    void setMaxContentWidth(int paneWidth); // 窗口缩放时重设宽度约束
    void applyTheme(const ThemeColors &c);

signals:
    // 动作请求（气泡不带消息下标：会话消息下标由窗口在动作时刻按
    // "尾部距离"换算——trim 只从头部淘汰，尾部距离恒稳定）
    void actionRequested(int action);

protected:
    void resizeEvent(QResizeEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    bool errorLine() const; // System 角色的错误提示行（对象名后缀判定）
    void buildUser();
    void buildAssistant();
    void buildSystem();
    // 悬浮工具条构建样板（T-062 抽取，user/assistant 共用）：QFrame overlay +
    // 文本按钮 + clicked→actionRequested 接线；buttons 为 {成员指针落点, 动作
    // 枚举} 表，文案由调用方在返回后补齐
    QFrame *makeToolbar(std::initializer_list<std::pair<QToolButton **, int>> buttons);
    void updateToolbar();
    void positionToolbar();
    void updateReasoningHeader();
    void handleViewportMove(const QPoint &pos);
    void copyCodeBlockUnder(const QTextBlock &block);
    QTextBlock codeBlockAtViewport(const QPoint &pos) const;

    Role m_role;
    bool m_streaming = false;
    bool m_caretShown = false;  // 流式打字光标 "▍" 是否在文末

    QVBoxLayout *m_lay = nullptr;
    QLabel *m_userText = nullptr;
    QLabel *m_systemText = nullptr;
    AutoHeightBrowser *m_body = nullptr;

    // 思考折叠区（assistant）
    QWidget *m_reasonSection = nullptr;
    QToolButton *m_reasonHead = nullptr;
    QTextBrowser *m_reasonBody = nullptr;
    QString m_reasoningText;
    bool m_reasonFinalized = false;

    // 悬浮工具条（overlay：不进布局，hover 显隐）
    QFrame *m_toolbar = nullptr;
    QToolButton *m_btnCopy = nullptr;
    QToolButton *m_btnEdit = nullptr;
    QToolButton *m_btnRegen = nullptr;
    QToolButton *m_btnDelete = nullptr;
    int m_hoverCount = 0;

    // 代码块复制（assistant 正文浏览器的 viewport 悬浮钮）
    QToolButton *m_codeCopyBtn = nullptr;
    GoodyCodeHighlighter *m_highlighter = nullptr;
};

} // namespace poptools
