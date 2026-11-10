#pragma once

#include <QObject>
#include <functional>

namespace poptools {

// ============================================================
// 输入法上屏检测：判断"刚发生了一次中文候选上屏"。
//
// 背景：WH_KEYBOARD_LL 看不到 IME 组合与上屏（那是目标进程内的
// 消息），只能从原始键序推断。启发式：
//   * 最近 3s 内敲过字母键（拼音/假名都在字母区）；
//   * 且此刻前台窗口的键盘布局是 CJK（HKL 高位语言 ID：zh=0x04、
//     ja=0x11、ko=0x12）——排除纯英文打字的空格/回车；
//   * 且按下了候选确认键（空格=首选、数字 1-9=备选、回车=上屏原文）。
// 三者同时成立 → 疑似上屏。误报（CJK 布局下打英文）由快照管线的
// 增量 diff 自然消化（无新增文字就不落盘），限流器控制成本。
//
// 时钟与 IME 探测均可注入（单测用虚拟时钟 + 恒真门）。
// ============================================================
class ImeCommitDetector : public QObject
{
    Q_OBJECT
public:
    explicit ImeCommitDetector(QObject *parent = nullptr);

    // 测试注入 IME 门（默认探测前台窗口键盘布局）
    void setImeGate(const std::function<bool()> &gate) { m_imeGate = gate; }
    // 测试注入时钟
    void setClock(const std::function<qint64()> &now) { m_now = now; }

public slots:
    // 键盘钩子事件（down 事件才有意义）
    void onKeyEvent(int vk, bool down);
    // 换窗后字母记忆失效
    void reset();

signals:
    void commitDetected(); // 疑似上屏（防抖/限流由快照管线负责）

private:
    qint64 now() const;
    bool imeGate();

    std::function<bool()> m_imeGate;
    std::function<qint64()> m_now;
    qint64 m_letterAtMs = 0; // 最近一次字母键按下时刻
};

} // namespace poptools
