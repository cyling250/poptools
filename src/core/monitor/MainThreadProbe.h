#pragma once

#include <QObject>
#include <QMutex>
#include <QWaitCondition>
#include <QTimer>
#include <deque>
#include <vector>

#ifdef Q_OS_WIN

namespace poptools {

// ============================================================
// 主线程输入探针（T-068「鼠标拖动卡顿→瞬移」取证，临时诊断设施）
//
// 病理：本项目三个低级钩子（GlobalMouseHook 的 WH_MOUSE_LL、
// GlobalKeyboardHook 的 WH_KEYBOARD_LL、GlobalHotKey 的裸键优先钩子）
// 都安装在 Qt 主线程。系统（RIT）派发低级钩子时要等安装线程把回调泵出
// 来并应答——主线程被任何一段耗时工作占住时，鼠标/键盘事件在系统侧排队，
// 光标冻结；主线程缓过来后排队事件一次性放行，光标瞬移。这正是用户
// 感知"拖动卡一下再瞬移、像丢了段动画"的机理。探针在进程内记录两类证据：
//   ① 钩子延迟：每个钩子回调打点，「事件自身 time 戳 → 回调实际执行
//      时刻」的差值即主线程没在泵消息的时长，就是用户感知的"卡了多久"；
//   ② 泵心跳：主线程 10ms QTimer 的实际触发间隔，超阈值记一次泵卡顿
//      （无输入活动时也能发现主线程被占——钩子打点只在有输入时出现）。
//
// 线程模型：钩子回调经主线程消息泵投递，两个定时器也在主线程——记录
// 全部产生于主线程，环形缓冲零锁；每 2s 把「异常事件 + 前后上下文窗口 +
// 周期统计」换出给 worker 线程落盘（磁盘 IO 不进主线程，红线 1），
// 正常路径零磁盘写入。
//
// 日志：AppLocalDataLocation/probe/probe-YYYYMMDD.jsonl（异常与统计）+
// probe-meta.json（QPC/GetTickCount/墙钟三个时钟各记一份起点，供与外部
// 被动监听脚本 temp_scripts/mouse_stall_monitor/ 的系统侧日志对时——
// 双方都用 QPC，可精确对齐）。根因定案后本探针整体删除。
// ============================================================
class MainThreadProbe : public QObject
{
    Q_OBJECT
public:
    // 钩子来源（写日志区分三条链）
    enum Src : quint8 { SrcMouse = 0, SrcKeyboard = 1, SrcPriorityKey = 2 };

    // RAII 打点：构造记回调进入（含事件 time 戳与 injected 合成输入标记），
    // 析构记退出并合成一条完整记录。早退路径（如优先钩子吞键 return 1）
    // 由析构兜住，回调体内无需其他插桩
    class HookCall
    {
    public:
        HookCall(Src src, quint32 eventTickMs, bool injected);
        ~HookCall();
        HookCall(const HookCall &) = delete;
        HookCall &operator=(const HookCall &) = delete;

    private:
        Src m_src;
    };

    // 取单例（首次调用创建并启动心跳/换出定时器与落盘线程）。必须在主线程
    // 首调——组合根 MainWindow 构造最前调用一次，此后才可能有钩子回调；
    // 钩子回调同样跑在主线程，后续调用恒为取现成指针
    static MainThreadProbe *instance();

    // ---- 链路标记（T-069b F1 卡顿取证扩展） ----
    // 主线程任意点打标（托盘菜单弹出/关闭、F1 热键回调、抓屏起止等），
    // 全量落盘（频率低，每条带 QPC/tick/前台 PID）。tag 截断到 31 字符。
    // 与钩子延迟/泵卡顿记录同钟（QPC），回查时按 qpc 对齐还原完整链路时序
    void mark(const char *tag);

private:
    explicit MainThreadProbe();
    ~MainThreadProbe() override;

    void hookEnter(Src src, quint32 eventTickMs, bool injected);
    void hookExit(Src src);
    void onHeartbeat();   // 10ms 泵心跳：间隔超阈值记泵卡顿
    void onFlushTick();   // 2s 换出：异常事件 + 上下文窗口 + 统计 → 落盘线程
    void shutdown();      // aboutToQuit：冲账最后一批后收线程

    quint32 captureForegroundPid() const;   // 事件时刻的前台进程号（µs 级只读）

    // ---- 主线程侧（无锁） ----
    struct Rec {          // 单条钩子回调记录（环形缓冲，仅主线程写）
        qint64 qpc;       // 回调执行时刻 QPC（与外部监听脚本同钟）
        quint32 tick;     // 回调执行时刻 GetTickCount（与事件 time 同钟）
        qint32 delayMs;   // 事件→回调延迟（nCode<0 无事件戳时记 -1）
        qint16 durMs;     // 回调自身耗时
        quint8 src;       // Src 枚举
        quint8 injected;  // 事件带 injected 标记（合成输入）
        quint8 noEvent;   // 1=nCode<0（本调用无钩子事件数据）
    };
    struct HookIncident { // 钩子延迟超阈事件
        qint64 qpc;
        quint32 tick;
        quint32 fgPid;
        qint32 delayMs;
        qint16 durMs;
        quint8 src;
        quint8 injected;
    };
    struct PumpIncident { // 泵心跳卡顿事件
        qint64 qpc;
        quint32 tick;
        quint32 fgPid;
        qint32 gapMs;
    };
    struct MarkRec {      // 链路标记（全量落盘）
        qint64 qpc;
        quint32 tick;
        quint32 fgPid;
        char tag[32];     // 定长 POD：跨线程值拷贝，截断到 31 字符 + NUL
    };
    // 落盘批（POD 值拷贝跨线程，无共享指针）；零异常周期也换出——统计行
    // 是探针的活体证据，区分"没有卡顿"与"探针死了"
    struct Batch {
        std::vector<Rec> ctx;                        // 上下文窗口记录
        std::vector<HookIncident> hookIncidents;
        std::vector<PumpIncident> pumpIncidents;
        std::vector<MarkRec> marks;                  // 本周期链路标记（全量）
        quint64 calls[3] = {0, 0, 0};                // 本周期回调数（按源）
        quint32 maxDelay[3] = {0, 0, 0};             // 本周期最大延迟（按源）
        quint32 maxGap = 0;                          // 本周期最大泵间隔
        quint32 ringSize = 0;                        // 换出时环形缓冲存量
        quint64 dropped = 0;                         // 环满累计丢弃数
        qint64 qpc = 0;
        quint32 tick = 0;
        quint32 fgPid = 0;
    };

    QTimer *m_heartbeat = nullptr;      // 10ms 泵心跳（主线程）
    QTimer *m_flush = nullptr;          // 2s 换出（主线程）
    qint64 m_qpcFreq = 1;               // QPC 频率（构造时定，全程不变）
    qint64 m_lastBeatQpc = 0;
    struct Pending {                    // 进行中的钩子回调（回调不嵌套，每源一格）
        qint64 enterQpc = 0;
        quint32 eventTick = 0;
        bool injected = false;
        bool active = false;
    };
    Pending m_pending[3];
    std::deque<Rec> m_ring;             // 钩子记录环形缓冲（主线程独占）
    quint64 m_dropped = 0;
    quint64 m_calls[3] = {0, 0, 0};
    quint32 m_maxDelay[3] = {0, 0, 0};
    quint32 m_maxGap = 0;
    std::vector<HookIncident> m_hookIncidents;
    std::vector<PumpIncident> m_pumpIncidents;
    std::vector<MarkRec> m_marks;       // 链路标记缓冲（主线程独占，2s 换出）
    qint64 m_lastIncidentQpc[3] = {0, 0, 0}; // 同源 500ms 内合并：一次卡顿放行
                                             // 一串事件只记一次异常
    // ---- 落盘线程侧（m_ioMutex 保护） ----
    QMutex m_ioMutex;
    QWaitCondition m_ioCond;
    std::deque<Batch> m_ioQueue;
    bool m_writerQuit = false;
    qint64 m_lastPumpQpc = 0;           // 泵卡顿同源合并（500ms 内只记一次）
    class ProbeWriterThread *m_writer = nullptr;

    friend class ProbeWriterThread;
};

} // namespace poptools

#endif // Q_OS_WIN
