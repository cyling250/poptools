#include "MarkdownRenderer.h"

namespace poptools {

void MarkdownRenderer::begin()
{
    m_text.clear();
    m_pending.clear();
    m_lineBuf.clear();
    m_lastPresentMs = 0;
    m_inFence = false;
    m_active = true;
    m_presentedAny = false;
}

MarkdownRenderStep MarkdownRenderer::feed(const QString &delta, qint64 nowMs)
{
    MarkdownRenderStep step;
    if (!m_active || delta.isEmpty())
        return step;

    m_text += delta;
    m_pending += delta;
    scanFences(delta);

    // 节流状态机：首块立即呈现（打字机起步不等节流线），此后跨过
    // kThrottleMs 线的下一块把积压一次性吐出——呈现频率 ≤ 1/150ms，
    // 禁止逐 chunk 全量重渲染（性能红线）
    if (!m_presentedAny || nowMs - m_lastPresentMs >= kThrottleMs) {
        step.present = true;
        step.newText = m_pending;
        m_pending.clear();
        m_lastPresentMs = nowMs;
        m_presentedAny = true;
    }
    return step;
}

MarkdownRenderStep MarkdownRenderer::finish()
{
    MarkdownRenderStep step;
    if (!m_active)
        return step; // 幂等：重复 finish 不再发定稿指令
    m_active = false;
    // 冲掉积压（UI 若直接按 finalize 走 setMarkdown，此段增量被覆盖，无碍）
    step.present = true;
    step.newText = m_pending;
    m_pending.clear();
    step.finalize = true;
    step.endedInFence = m_inFence;
    return step;
}

// 围栏开合状态机（代码块完成检测）：只认"完整行"——增量 chunk 可能把
// ``` 切成两半，半行留在 m_lineBuf 拼到下一块凑成整行再判定。
// 判定规则（GFM 子集）：剥去行首至多 3 个空格后以 ``` 开头的行切换开合
//（开栏可带语言后缀，闭栏忽略行尾余文）。
void MarkdownRenderer::scanFences(const QString &delta)
{
    m_lineBuf += delta;
    int nl;
    while ((nl = m_lineBuf.indexOf(QLatin1Char('\n'))) >= 0) {
        const QString line = m_lineBuf.left(nl);
        m_lineBuf.remove(0, nl + 1);
        int i = 0;
        while (i < line.size() && i < 3 && line.at(i) == QLatin1Char(' '))
            ++i; // 行首至多 3 空格缩进仍可开/闭栏
        if (line.mid(i).startsWith(QStringLiteral("```")))
            m_inFence = !m_inFence;
    }
    // 无换行的残留半行不判定（未成整行的 ``` 不改变开合状态）
}

} // namespace poptools
