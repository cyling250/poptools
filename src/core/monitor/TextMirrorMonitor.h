#pragma once

#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QVector>

#include "UiaTextReader.h" // Result（moc 签名需要）

class QTimer;

namespace poptools {

// ------------------------------------------------------------
// 纯逻辑：字段影子库 + 双向差分（无 QObject，单测直测）。
// 事件只含文本与字段身份，时间戳/窗口上下文由调用方（模块落盘处）补。
// 必须先于 TextMirrorMonitor 定义（其信号签名内嵌 MirrorEvent）。
// ------------------------------------------------------------
class TextMirrorStore
{
public:
    struct MirrorEvent
    {
        QString kind;    // "insert" / "delete" / "replace"
        QString text;    // 新增侧（insert/replace 有值）
        QString removed; // 被删侧（delete/replace 有值）
        QString field;   // 字段身份键
    };

    // 双向差分（纯函数）：公共前缀/后缀收拢，中段即变化。两侧快照来自
    // 同一条 UIA 取文路径（粒度一致），与 SnapshotOcrPipeline::diffAdded
    // 的"宁缺毋滥三案"不同——这里任何差异都能定位，交给 applyRead 的
    // churn 护栏统一裁决伪变化
    struct Change
    {
        QString removed; // 旧文本中段（被删/被替换部分）
        QString added;   // 新文本中段（新增/替换后部分）
    };
    static Change diffChange(const QString &oldText, const QString &newText);

    // 最大公共重叠（纯函数）：新旧文本的最长公共前缀/后缀中的较大者。
    // 续接判定用——UI 重建后的字段内容与重建前只有小步差异（长重叠），
    // 无关字段则没有
    static int continuationOverlap(const QString &oldText, const QString &newText);

    // 消费一次字段读数（fieldKey=字段身份，windowKey=所属窗口身份）：
    // 既有身份走常规差分（churn 护栏内至多一条事件，影子照常推进）；
    // 新身份首拍时尝试与同窗口最近旧影子续接（UI 重建场景——RuntimeId
    // 变化而内容只有小步前进），重叠达到 kMinContinuationOverlap 才按
    // 差分补记，否则静默建影子（宁缺毋滥）。密码字段走 dropShadow，
    // 不走本函数
    QVector<MirrorEvent> applyRead(const QString &fieldKey, const QString &windowKey,
                                   const QString &text);

    // 丢弃字段影子（密码框专用：影子若残留旧文，退出密码框后的第一次
    // 差分会把密码框现状当增量带出；重置即断泄漏路径）
    void dropShadow(const QString &fieldKey);

    int shadowCount() const { return m_shadow.size(); }

    static constexpr int kMaxFieldShadows = 4;        // 影子 LRU 上限（同时活跃的输入框）
    static constexpr int kMaxChurnChars = 600;        // 两侧齐变的伪变化判定阈值
    static constexpr int kMinContinuationOverlap = 8; // 续接最小公共重叠字符数

private:
    struct ShadowEntry
    {
        QString text;     // 上次读到的字段文本
        QString windowKey; // 所属窗口身份（续接只在同窗口内寻找）
    };
    void insertShadow(const QString &key, const QString &windowKey, const QString &text);
    void touch(const QString &key);

    QHash<QString, ShadowEntry> m_shadow; // 字段键 → 影子
    QList<QString> m_lru;                 // 触碰序（尾部最新，与 m_shadow 同步维护）
};

// ============================================================
// UIA 焦点控件文本镜像（T-066，路线见 requirements/20261118-tsf-text-mirror.md）：
//   键入/剪贴板/切窗触发 → 防抖 → UIA 读焦点控件全文（UiaTextReader 专用
//   worker 线程）→ 按字段影子快照双向差分 → 增/删/替换事件 → textlog.jsonl。
//
// 与 SnapshotOcrPipeline 的分工：该管线服务"中文上屏的证据补拍"（UIA 光标
// 附近文本 + 截图 OCR 兜底，只记新增）；本镜像服务"输入与回退的原文抄录"
// ——用户输入了什么、退格删了什么，都以最终文本形态原样落盘。两者读同一
// 焦点元素但各持基线，互不干扰。
//
// 与被否掉的 TSF TIP 路线的关系（调研细档 docs/handover/t066_tsf_research.md）：
// 被动 TIP 未被选中不加载、选中即顶掉用户输入法、经典 EDIT 对 TSF 盲——
// UIA 恰好反向覆盖：经典 EDIT 有 ValuePattern，TSF-aware 宿主（浏览器/
// 新版记事本/Word）有 TextPattern；读不到 UIA 文本的程序（游戏/自绘画布）
// 仍是盲区，由字母键序流兜底（T-066 验收通过后字母流移除，见需求单）。
//
// 字段身份：UIA RuntimeId（浏览器/Electron 内嵌控件无句柄也有稳定身份）
// → 元素句柄 → 顶层窗口，逐级退化；影子按字段 LRU ≤4 个，密码字段即读
// 即弃（防密码字符日后以差分形式泄漏）。
//
// UI 重建不吞增量（T-066 验收期实测修复）：Electron 应用重建输入框时
// RuntimeId 随之变化（实测同一聊天框 10+ 个身份），重建后的首次读以
// "新字段身份"出现——续接机制让它与同窗口最近旧影子做差分，内容只有
// 小步前进（长公共重叠）就补记事件、迁基线；无关字段（重叠不足）静默
// 建影子，宁缺毋滥。
//
// 诚实边界：>kMaxChars 的长字段里 TextPattern 取文窗口随光标滑动，头部
// 滑出会被误判为大段删除——churn 护栏（两侧齐变超阈值判为窗口滑动伪
// 变化，宁缺毋滥丢事件、基线照常推进）兜底；纯增长/纯删除不受护栏限制
// （大段粘贴与整段回退都是真实输入，必须抄全）。
//
// 线程纪律（红线1）：UIA COM 全在 UiaTextReader 专用 worker；差分与事件
// 回主线程；落盘经 ActivityStore worker。触发入口零耗时（对齐钩子回调
// 纪律）。changeReady 为同线程直连信号，MirrorEvent 不注册 metatype。
// ============================================================
class TextMirrorMonitor : public QObject
{
    Q_OBJECT
public:
    explicit TextMirrorMonitor(QObject *parent = nullptr);
    ~TextMirrorMonitor() override;

    // 前台窗口上下文（ForegroundTracker 转发；事件行 app/title 用）
    void setWindowContext(const QString &app, const QString &title)
    {
        m_app = app;
        m_title = title;
    }

    // 字段身份键（纯函数，单测覆盖）：RuntimeId 优先 → 元素句柄 → 顶层
    // 窗口；三者皆缺时退 "t:0"（同进程内多未知控件共享一键，churn 护栏
    // 与进程对号兜底）
    static QString fieldKeyOf(const UiaTextReader::Result &result);

    // 窗口身份键（纯函数，单测覆盖）：顶层窗口 → 焦点元素进程号；皆缺
    // 返回空（续接跳过）。供影子续接限定"同窗口"——UI 重建只换字段身份，
    // 不换窗口
    static QString windowKeyOf(const UiaTextReader::Result &result);

public slots:
    // 键盘 down / 剪贴板内容变化：防抖合并（连打只保留最后一拍）
    void onTypingActivity();

    // 切窗：更短防抖（新字段当前内容尽快上镜建影子）
    void onWindowChanged();

signals:
    // 字段文本变化（同线程直连；app/title 为当前前台上下文）
    void changeReady(const QString &app, const QString &title,
                     const poptools::TextMirrorStore::MirrorEvent &ev);

private slots:
    void readNow();
    void onUiaFinished(const UiaTextReader::Result &result);

private:
    QTimer *m_debounce = nullptr;
    UiaTextReader *m_uia = nullptr; // 随本对象生死（worker 线程内部自管理）
    TextMirrorStore m_store;
    QString m_app, m_title;
    bool m_busy = false;       // UIA 读在途（busy 门卫：在途时新触发直接丢弃）
    quintptr m_reqHwnd = 0;    // 取文时刻的前台窗口（密码判定回写用）
    qint64 m_lastReadMs = 0;   // 限流：两次 UIA 读最小间隔
};

} // namespace poptools
