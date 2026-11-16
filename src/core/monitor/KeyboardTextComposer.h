#pragma once

#include <QObject>
#include <QString>

namespace poptools {

// ============================================================
// 键入文本组装器：把键盘钩子的原始键事件还原为"用户写了什么"。
//
// 策略：
//   * 只在主线程事件循环中跑重活（钩子回调只入队）；
//   * 修饰键（Shift/Ctrl/Alt/CapsLock）状态机 + ToUnicodeEx 翻译
//     成字符（跟随前台窗口的键盘布局）；
//   * 同一 (app, title) 下连续键入攒成一段，空闲 3s / 满 512 字 /
//     切窗时作为一次 textReady 发出；
//   * 密码打码：masked 状态下每个可打印键记一个 ●（长度近似保留，
//     内容不落盘）；打码判定由外部（PasswordFieldGuard）在每次
//     键事件前评估注入，本类保持可单测。
//
// 已知边界（有意接受）：
//   * 中文输入法组合期记录的是拼音字母（LL 钩子拿不到上屏中文），
//     最终中文由 OCR 快照管线补齐（ocrtext 流，source=ocr）；
//   * 死键（Dead Key）组合音标暂不还原；Ctrl/Alt 组合键（快捷键）
//     不记录——记录的是"写了什么"，不是"按了什么"。例外：AltGr
//     （Ctrl+Alt 同按）按字符键交翻译流程，产出可打印字符照记。
// ============================================================
class KeyboardTextComposer : public QObject
{
    Q_OBJECT
public:
    explicit KeyboardTextComposer(QObject *parent = nullptr);

    // 总打码开关（Settings monitor/maskPasswords，默认 true）：false 时密码段
    // 直接记明文——只随用户显式关闭生效；与 setMasked（每键判定的临时态）独立
    void setMaskPasswords(bool on) { m_maskPasswords = on; }

public slots:
    // 外部注入的密码框判定结果（每个键事件前由模块刷新）。仅在值变化时
    // 冲刷旧段（P2-7）：● 段与后续明文不得混入同一段（段级 masked 标记
    // 以段为单位）——每键都会调本函数，无变化时不得刷
    void setMasked(bool on)
    {
        if (on == m_maskedNow)
            return;
        flush();
        m_maskedNow = on;
    }

    // 前台窗口上下文变化（切窗）；上下文变化前先冲刷旧段
    void setWindowContext(const QString &app, const QString &title);

    // 键盘钩子事件（主线程槽）
    void onKeyEvent(int vk, int scan, bool extended, bool down);

    // 1s 心跳：空闲超时冲刷（由模块的 tick 定时器驱动）
    void onIdleTick(qint64 nowMs);

    // 立即冲刷当前缓冲（停用监控/退出时）
    void flush();

signals:
    void textReady(const QString &app, const QString &title, const QString &text,
                   bool masked);

private:
    bool translateVk(int vk, int scan, QChar &out) const; // 主线程，Win32
    void notePrintable(const QChar &c);

    bool m_maskPasswords = true;
    bool m_maskedNow = false;

    // 修饰键状态机（钩子全量 down/up 驱动；初值取系统实时状态）
    bool m_shift = false, m_ctrl = false, m_alt = false, m_caps = false;

    // 当前积累段
    QString m_app, m_title;
    QString m_text;
    bool m_runMasked = false; // 本段是否含打码内容
    qint64 m_lastInputMs = 0;

    int m_idleFlushMs = 3000;   // 空闲冲刷阈值（"一句话说完了"的近似）
    int m_maxRunChars = 512;    // 单段上限（控制 keys.jsonl 单行体量）
};

} // namespace poptools
