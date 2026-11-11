#pragma once

#include <QString>

namespace poptools {

// ============================================================
// MarkdownRenderer：流式 Markdown 渲染纯逻辑类（T-050-P2）。
//
// 职责 = 流式文本块累积 → 节流触发 → 产出"当前应呈现内容"：
//   * 流式期间：原始文本增量追加（打字机粒度，零重渲染闪烁）；
//     节流 150ms（AI 代决定案）——首块立即呈现，窗口内的 chunk 只
//     累积不呈现，跨过节流线的下一块把积压一次性吐出。
//   * 结束时：一次 finalize 指令（UI 层对全量原文 setMarkdown 定稿，
//     含代码高亮）。
//   * 代码块完成检测：维护 ``` 围栏开合状态机（跨 chunk 半行安全），
//     inOpenCodeFence() 报告当前是否处于未闭合围栏内；finalize 指令
//     附带 endedInFence（闭合与否决定该块呈现语义——未闭合块定稿时
//     UI 可按原文呈现而不做高亮包装）。
//
// 不持有 UI、不持有定时器：时间由调用方以单调递增的 nowMs 注入
//（纯逻辑可单测），UI 层用 QElapsedTimer 供时钟。性能红线：调用方
// 每 chunk 调一次 feed 也没有常驻定时器；呈现只在节流线与定稿点。
// ============================================================

// 一次 feed/finish 的渲染指令（纯数据，UI 按指令更新当前气泡）
struct MarkdownRenderStep
{
    bool present = false;   // 流式呈现：把 newText 以纯文本追加到当前气泡
    QString newText;        // 本次应追加的原始文本（节流窗口内积压的合并）
    bool finalize = false;  // 定稿：UI 对 finalText（全量原文）一次 setMarkdown
    bool endedInFence = false; // 定稿时文本停在未闭合 ``` 围栏内（信息位）
};

class MarkdownRenderer
{
public:
    static constexpr int kThrottleMs = 150; // 流式呈现节流（AI 代决定案）

    void begin(); // 新回合开始（清空全部状态）

    // 喂入一个正文增量 chunk；返回本块触发的渲染指令（可能无动作）
    MarkdownRenderStep feed(const QString &delta, qint64 nowMs);

    // 流结束：冲掉节流窗口积压并给出定稿指令（幂等安全：重复调用
    // finalize=false）
    MarkdownRenderStep finish();

    // 当前累计原文（流式中已接收的全部，定稿前的呈现基准）
    QString streamText() const { return m_text; }
    // 当前是否处于未闭合 ``` 围栏内（跨 chunk 状态机）
    bool inOpenCodeFence() const { return m_inFence; }

private:
    void scanFences(const QString &delta); // 围栏状态机：仅扫完整行

    QString m_text;       // 全量累积原文
    QString m_pending;    // 节流窗口内尚未呈现的增量
    QString m_lineBuf;    // 围栏扫描的半行残留
    qint64 m_lastPresentMs = 0;
    bool m_inFence = false;
    bool m_active = false;
    bool m_presentedAny = false; // 首块是否已呈现（首块免节流，起步不等 150ms）
};

} // namespace poptools
