#include "TextMirrorMonitor.h"

#include <QDateTime>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>

#include "PasswordFieldGuard.h"

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace poptools {

namespace {
constexpr int kTypingDebounceMs = 350; // 连打合并：最后一键后 350ms 才读
constexpr int kSwitchDebounceMs = 120; // 切窗/粘贴后的快速一拍
constexpr int kMinReadGapMs = 250;     // 限流：两次 UIA 读最小间隔

// ---- 可选诊断（常驻，环境变量 POPTOOLS_MIRROR_DEBUG=1 开启，默认零
// 开销）：每次取文回包与触发跳过写 %TEMP%\poptools_mirror_debug.jsonl。
// T-066 验收期凭它定位了"RuntimeId 重建吞增量"（261 读全 ok、同一聊天框
// 10+ 字段身份），保留作现场诊断工具 ----
void mirrorDebugLog(const QJsonObject &o)
{
    static const bool enabled = qEnvironmentVariableIsSet("POPTOOLS_MIRROR_DEBUG");
    if (!enabled)
        return;
    QJsonObject row = o;
    row.insert("dbgTs", QDateTime::currentMSecsSinceEpoch());
    QFile f(qEnvironmentVariable("TEMP") + QStringLiteral("/poptools_mirror_debug.jsonl"));
    if (f.open(QIODevice::WriteOnly | QIODevice::Append))
        f.write(QJsonDocument(row).toJson(QJsonDocument::Compact) + QByteArrayLiteral("\n"));
}
} // namespace

TextMirrorMonitor::TextMirrorMonitor(QObject *parent)
    : QObject(parent)
    , m_uia(new UiaTextReader(this)) // 随本对象生死（worker 线程内部自管理）
{
    m_debounce = new QTimer(this);
    m_debounce->setSingleShot(true);
    connect(m_debounce, &QTimer::timeout, this, &TextMirrorMonitor::readNow);
    connect(m_uia, &UiaTextReader::readFinished, this,
            &TextMirrorMonitor::onUiaFinished);
}

// 停防抖即可：在途 UIA 读的回包走 queued 投递，本对象析构后由 QObject
// 断连语义自动丢弃（UiaTextReader 为子对象，随父子树析构）
TextMirrorMonitor::~TextMirrorMonitor()
{
    m_debounce->stop();
}

QString TextMirrorMonitor::fieldKeyOf(const UiaTextReader::Result &result)
{
    // 逐级退化：RuntimeId（浏览器/Electron 无句柄控件也有稳定身份）→
    // 元素句柄 → 顶层窗口。十六进制避免与 RuntimeId 的点分十进制混淆
    if (!result.runtimeId.isEmpty())
        return QStringLiteral("r:") + result.runtimeId;
    if (result.elHwnd)
        return QStringLiteral("h:") + QString::number(quintptr(result.elHwnd), 16);
    return QStringLiteral("t:") + QString::number(quintptr(result.topHwnd), 16);
}

QString TextMirrorMonitor::windowKeyOf(const UiaTextReader::Result &result)
{
    // 顶层窗口优先（同窗口内 UI 重建只换字段身份不换窗口）；浏览器
    // 场景 topHwnd 可能不可知，退焦点元素进程号；皆缺返回空（续接跳过）
    if (result.topHwnd)
        return QStringLiteral("w:") + QString::number(quintptr(result.topHwnd), 16);
    if (result.elPid)
        return QStringLiteral("p:") + QString::number(quintptr(result.elPid));
    return QString();
}

void TextMirrorMonitor::onTypingActivity()
{
    m_debounce->start(kTypingDebounceMs);
}

void TextMirrorMonitor::onWindowChanged()
{
    m_debounce->start(kSwitchDebounceMs);
}

void TextMirrorMonitor::readNow()
{
    // UIA 读在途：本次触发直接丢弃（防堆积；下次击键自然补拍。UIA COM
    // 无超时 API，与 SnapshotOcrPipeline 同款 busy 门卫保证不排队）
    if (m_busy) {
        QJsonObject d;
        d.insert(QStringLiteral("ev"), QStringLiteral("skip-busy"));
        mirrorDebugLog(d);
        return;
    }
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - m_lastReadMs < kMinReadGapMs) {
        m_debounce->start(kMinReadGapMs); // 限流：稍后一拍再读
        QJsonObject d;
        d.insert(QStringLiteral("ev"), QStringLiteral("skip-rate"));
        mirrorDebugLog(d);
        return;
    }
#ifdef Q_OS_WIN
    const HWND fg = GetForegroundWindow();
    if (!fg || IsHungAppWindow(fg)) {
        QJsonObject d;
        d.insert(QStringLiteral("ev"), QStringLiteral("skip-hung"));
        mirrorDebugLog(d);
        return; // 挂起窗口的跨进程 UIA 会拖住 worker（SnapshotOcrPipeline 同款预检）
    }
    m_lastReadMs = now;
    m_reqHwnd = reinterpret_cast<quintptr>(fg);
    m_busy = true;
    {
        QJsonObject d;
        d.insert(QStringLiteral("ev"), QStringLiteral("read"));
        d.insert(QStringLiteral("fg"),
                 QString::number(reinterpret_cast<quintptr>(fg), 16));
        mirrorDebugLog(d);
    }
    m_uia->readAsync(m_reqHwnd); // worker 读焦点元素；回包进 onUiaFinished
#endif
}

void TextMirrorMonitor::onUiaFinished(const UiaTextReader::Result &result)
{
    m_busy = false;
    {
        QJsonObject d;
        d.insert(QStringLiteral("ev"), QStringLiteral("result"));
        d.insert(QStringLiteral("ok"), result.ok);
        d.insert(QStringLiteral("pwd"), result.password);
        d.insert(QStringLiteral("rid"), result.runtimeId);
        d.insert(QStringLiteral("elPid"), QString::number(result.elPid));
        d.insert(QStringLiteral("elHwnd"), QString::number(result.elHwnd, 16));
        d.insert(QStringLiteral("topHwnd"), QString::number(result.topHwnd, 16));
        d.insert(QStringLiteral("len"), result.text.size());
        d.insert(QStringLiteral("tail"), result.text.right(40));
        d.insert(QStringLiteral("wkey"), windowKeyOf(result));
        mirrorDebugLog(d);
    }
    if (result.password) {
        // 密码框：整链静默 + 影子重置——影子若残留进框前旧文，退出密码框
        // 后的第一次差分会把密码框现状当增量带出；重置即断泄漏路径。判定
        // 回写 PasswordFieldGuard 供键入流打码共享（元素句柄为 0 时退记
        // 取文时刻的前台窗口，对齐 SnapshotOcrPipeline 口径）
        PasswordFieldGuard::cacheUiaPassword(
            result.elHwnd ? result.elHwnd : m_reqHwnd, true);
        m_store.dropShadow(fieldKeyOf(result));
        return;
    }
    if (!result.ok)
        return; // 读失败/换窗被 worker 拒：影子保留（churn 护栏兜大差）
    const auto evs = m_store.applyRead(fieldKeyOf(result), windowKeyOf(result),
                                       result.text);
    for (const auto &ev : evs)
        emit changeReady(m_app, m_title, ev);
}

// ---- TextMirrorStore：双向差分 + 字段影子 ----

namespace {
// 差分 → 事件（churn 护栏内）；护栏外/无变化返回 false（宁缺毋滥）。
// insert/delete/replace 的组装口径只有这一处，续接与常规路径共用
bool buildEvent(const QString &fieldKey, const TextMirrorStore::Change &c,
                TextMirrorStore::MirrorEvent &out)
{
    if (c.removed.isEmpty() && c.added.isEmpty())
        return false;
    if (!c.removed.isEmpty() && !c.added.isEmpty()
        && c.removed.size() + c.added.size() > TextMirrorStore::kMaxChurnChars)
        return false;
    out.field = fieldKey;
    if (c.removed.isEmpty()) {
        out.kind = QStringLiteral("insert");
        out.text = c.added;
    } else if (c.added.isEmpty()) {
        out.kind = QStringLiteral("delete");
        out.removed = c.removed;
    } else {
        out.kind = QStringLiteral("replace");
        out.text = c.added;
        out.removed = c.removed;
    }
    return true;
}
} // namespace

TextMirrorStore::Change TextMirrorStore::diffChange(const QString &oldText,
                                                    const QString &newText)
{
    Change c;
    if (oldText == newText)
        return c;
    // 公共前缀：停在第一处失配
    int p = 0;
    const int maxP = qMin(oldText.size(), newText.size());
    while (p < maxP && oldText.at(p) == newText.at(p))
        ++p;
    // 公共后缀：不越过前缀（maxS 保证中段不重叠计数）
    int s = 0;
    const int maxS = qMin(oldText.size(), newText.size()) - p;
    while (s < maxS && oldText.at(oldText.size() - 1 - s) == newText.at(newText.size() - 1 - s))
        ++s;
    c.removed = oldText.mid(p, oldText.size() - p - s);
    c.added = newText.mid(p, newText.size() - p - s);
    return c;
}

int TextMirrorStore::continuationOverlap(const QString &oldText, const QString &newText)
{
    // 与 diffChange 同一套前后缀收拢，只取度量不取中段（后缀不做前缀避让，
    // 纯度量场景无碍：完全相同的文本前后缀各计满，max 仍是全长）
    int p = 0;
    const int maxP = qMin(oldText.size(), newText.size());
    while (p < maxP && oldText.at(p) == newText.at(p))
        ++p;
    int s = 0;
    const int maxS = qMin(oldText.size(), newText.size());
    while (s < maxS && oldText.at(oldText.size() - 1 - s) == newText.at(newText.size() - 1 - s))
        ++s;
    return qMax(p, s);
}

QVector<TextMirrorStore::MirrorEvent>
TextMirrorStore::applyRead(const QString &fieldKey, const QString &windowKey,
                           const QString &text)
{
    QVector<MirrorEvent> out;
    MirrorEvent ev;
    if (!m_shadow.contains(fieldKey)) {
        // 新字段身份首拍：UI 重建（RuntimeId 变化）后内容与重建前只有小步
        // 前进——与同窗口最近旧影子续接（重叠足额才采信），按差分补记事件
        // 并迁基线；无关字段/重叠不足/跨窗口/窗口未知一律静默建影子
        QString prevKey;
        if (!windowKey.isEmpty()) {
            for (int i = m_lru.size() - 1; i >= 0; --i) {
                const QString &k = m_lru.at(i);
                if (k != fieldKey && m_shadow.value(k).windowKey == windowKey) {
                    prevKey = k;
                    break;
                }
            }
        }
        if (!prevKey.isEmpty()
            && continuationOverlap(m_shadow.value(prevKey).text, text)
                   >= kMinContinuationOverlap) {
            const Change c = diffChange(m_shadow.value(prevKey).text, text);
            if (buildEvent(fieldKey, c, ev)) {
                insertShadow(fieldKey, windowKey, text);
                out.append(ev);
                return out;
            }
            // 重建后内容未变：迁基线不产事件
        }
        insertShadow(fieldKey, windowKey, text); // 陌生字段：静默建影子
        return out;
    }
    // 既有字段：常规差分
    const Change c = diffChange(m_shadow.value(fieldKey).text, text);
    if (buildEvent(fieldKey, c, ev)) {
        insertShadow(fieldKey, windowKey, text);
        out.append(ev);
        return out;
    }
    if (c.removed.isEmpty() && c.added.isEmpty()) {
        touch(fieldKey); // 无变化：只刷新 LRU
    } else {
        // churn 护栏外（两侧齐变合计超阈值 → 窗口滑动/版面重绘伪变化）：
        // 宁缺毋滥丢事件、基线照常推进；纯增长/纯删除不受护栏限制——大段
        // 粘贴与整段回退都是真实输入，必须抄全
        insertShadow(fieldKey, windowKey, text);
    }
    return out;
}

void TextMirrorStore::dropShadow(const QString &fieldKey)
{
    m_shadow.remove(fieldKey);
    m_lru.removeOne(fieldKey);
}

void TextMirrorStore::insertShadow(const QString &key, const QString &windowKey,
                                   const QString &text)
{
    m_shadow.insert(key, ShadowEntry{text, windowKey});
    touch(key);
    // 溢出逐出最旧（队头）：先删后插保证 ≤ kMaxFieldShadows 且当前字段存活
    while (m_lru.size() > kMaxFieldShadows) {
        const QString oldest = m_lru.takeFirst();
        m_shadow.remove(oldest);
    }
}

void TextMirrorStore::touch(const QString &key)
{
    m_lru.removeOne(key);
    m_lru.append(key);
}

} // namespace poptools
