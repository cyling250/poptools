#include "ClipboardListener.h"

#include <QDateTime>
#include <QJsonObject>
#include <QPointer>
#include <QThreadPool>
#include <QTimer>

#include "ActivityStore.h"
#include "OwnWriteGuard.h"

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#endif

namespace poptools {

namespace {

// T-047-FIX(P3-3)：burst 合并 300ms（原 200ms）。假设：OwnWriteGuard 的
// noteSelfWrite 在自写调用结束后立即执行，正常自写全程 <300ms，防抖到点
// 时自写标记必然已可命中。残余风险：单次自写超过 300ms（超大内容/慢速
// 应用）时防抖先触发，自写内容可能被记为一条外部事件——依赖 OpenClipboard
// 竞争兜底（自写进程仍持有剪贴板时 OpenClipboard 失败，本条放弃），方向
// 无害（宁少记不多记）
constexpr int kDebounceMs = 300;
constexpr int kPreviewChars = 200; // 头尾预览各 200 字符
constexpr int kMaxPaths = 5;       // CF_HDROP 路径条数上限
constexpr int kOpenRetry = 5;      // OpenClipboard 竞争重试
constexpr int kOpenRetryGapMs = 15;

#ifdef Q_OS_WIN

constexpr wchar_t kWndClassName[] = L"PopToolsClipboardMonitorWnd";

ClipboardListener *g_listener = nullptr; // 单实例（ForegroundTracker 同款）

LRESULT CALLBACK clipWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_CLIPBOARDUPDATE && g_listener) {
        g_listener->noteUpdate(); // 零耗时：只记 seq + 重启防抖
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ---- 剪贴板载荷提取（worker 线程专用）----
// 纪律：OpenClipboard 与其它进程竞争（重试几次），读取按
// 文本 > 文件 > 图片 优先级；文本只切片（头/尾/哈希前段），图片只读
// BITMAPINFOHEADER 40 字节——大文本/大图零整块拷贝，不落内容。

// 语义与 QString::size 一致的 UTF-16 码元长度（到首 NUL 为止）
qint64 wcsLenCapped(const wchar_t *ptr, SIZE_T maxChars)
{
    qint64 n = 0;
    while (SIZE_T(n) < maxChars && ptr[n] != L'\0')
        ++n;
    return n;
}

QString headSlice(const wchar_t *ptr, qint64 len)
{
    return QString::fromWCharArray(ptr, static_cast<int>(qMin<qint64>(len, kPreviewChars)));
}

ClipboardCodec::ClipboardPayload readTextPayload()
{
    ClipboardCodec::ClipboardPayload p;
    HANDLE h = GetClipboardData(CF_UNICODETEXT);
    // T-047-FIX(P3-8①)：畸形数据大小校验先行（对照 SelectionTranslator
    // R3 #27 先例）：块内无有效字节时不加锁直返
    if (!h || GlobalSize(h) < sizeof(wchar_t))
        return p;
    const wchar_t *ptr = static_cast<const wchar_t *>(GlobalLock(h));
    if (!ptr)
        return p;
    const qint64 len = wcsLenCapped(ptr, GlobalSize(h) / sizeof(wchar_t));
    if (len > 0) {
        p.kind = ClipboardCodec::ClipboardPayload::Kind::Text;
        p.chars = len;
        // 哈希只算前 65536 字节（T-047-FIX(P3-4)：kHashHeadBytes，UTF-8
        // 字节语义；大文本不做全文换码/哈希）+ 总长
        // 先取 65536 个 UTF-16 码元（≥ 所需 UTF-8 字节量），contentHash 内
        // 再按 UTF-8 字节截断——净效果即"前 64KiB 字节"（中间量只多不少）
        const QString head64k = QString::fromWCharArray(
            ptr, static_cast<int>(qMin<qint64>(len, ClipboardCodec::kHashHeadBytes)));
        p.hash = ClipboardCodec::contentHash(head64k.toUtf8(), len);
        p.head = headSlice(ptr, len);
        if (len > kPreviewChars)
            p.tail = QString::fromWCharArray(ptr + len - kPreviewChars, kPreviewChars);
    }
    GlobalUnlock(h);
    return p;
}

ClipboardCodec::ClipboardPayload readFilesPayload()
{
    ClipboardCodec::ClipboardPayload p;
    HANDLE h = GetClipboardData(CF_HDROP);
    if (!h)
        return p;
    // 0xFFFFFFFF = DragQueryFileW 的"查总数"专用 item 下标
    const UINT count = DragQueryFileW(reinterpret_cast<HDROP>(h), 0xFFFFFFFF, nullptr, 0);
    if (count == 0)
        return p;
    p.kind = ClipboardCodec::ClipboardPayload::Kind::Files;
    p.pathsTruncated = count > kMaxPaths;
    wchar_t buf[MAX_PATH * 2] = {};
    for (UINT i = 0; i < count && i < UINT(kMaxPaths); ++i) {
        if (DragQueryFileW(reinterpret_cast<HDROP>(h), i, buf, MAX_PATH * 2) > 0)
            p.paths << QString::fromWCharArray(buf);
    }
    const QByteArray joined = p.paths.join(QLatin1Char('\n')).toUtf8();
    p.hash = ClipboardCodec::contentHash(joined, count);
    return p;
}

ClipboardCodec::ClipboardPayload readImagePayload()
{
    ClipboardCodec::ClipboardPayload p;
    const UINT fmt = IsClipboardFormatAvailable(CF_DIB) ? CF_DIB
                     : IsClipboardFormatAvailable(CF_DIBV5)
                         ? CF_DIBV5
                         : 0;
    if (!fmt)
        return p;
    HANDLE h = GetClipboardData(fmt);
    // T-047-FIX(P3-8①)：GlobalSize 不足以容纳 BITMAPINFOHEADER 时直返
    //（畸形/截断的 DIB 块；对照 SelectionTranslator R3 #27 先例）
    if (!h || GlobalSize(h) < sizeof(BITMAPINFOHEADER))
        return p;
    // 只读 BITMAPINFOHEADER 40 字节定元数据，DIB 数据块不触碰
    //（截图级 DIB 数十 MB，整块拷贝是重活，禁入一切线程热点路径）
    const BITMAPINFOHEADER *bi =
        static_cast<const BITMAPINFOHEADER *>(GlobalLock(h));
    if (!bi || bi->biSize < sizeof(BITMAPINFOHEADER)) {
        if (bi)
            GlobalUnlock(h);
        return p;
    }
    p.kind = ClipboardCodec::ClipboardPayload::Kind::Image;
    p.width = bi->biWidth;
    p.height = bi->biHeight < 0 ? -bi->biHeight : bi->biHeight; // 顶/底向上都归正
    const long bpp = bi->biBitCount > 0 ? bi->biBitCount : 32;
    p.bytes = bi->biSizeImage > 0
                  ? bi->biSizeImage
                  : qint64(p.width) * p.height * bpp / 8; // BI_RGB 无 biSizeImage 时估算
    p.hash = ClipboardCodec::contentHash(
        QStringLiteral("%1x%2:%3").arg(p.width).arg(p.height).arg(p.bytes).toUtf8(),
        p.bytes);
    GlobalUnlock(h);
    return p;
}

ClipboardCodec::ClipboardPayload readClipboardPayload()
{
    bool opened = false;
    for (int i = 0; i < kOpenRetry && !opened; ++i) {
        opened = OpenClipboard(nullptr) != 0; // 与其它进程竞争时短暂重试
        if (!opened)
            Sleep(kOpenRetryGapMs);
    }
    ClipboardCodec::ClipboardPayload p;
    if (!opened)
        return p; // 竞争失败：放弃本条（下条更新自然补上）
    if (IsClipboardFormatAvailable(CF_UNICODETEXT))
        p = readTextPayload();
    if (p.kind == ClipboardCodec::ClipboardPayload::Kind::Empty
        && IsClipboardFormatAvailable(CF_HDROP))
        p = readFilesPayload();
    if (p.kind == ClipboardCodec::ClipboardPayload::Kind::Empty)
        p = readImagePayload();
    CloseClipboard();
    return p;
}
#endif // Q_OS_WIN

} // namespace

// 防抖定时器随本对象；store 指针由模块保证先建后用
ClipboardListener::ClipboardListener(ActivityStore *store, QObject *parent)
    : QObject(parent)
    , m_store(store)
    , m_deduper()
{
    m_debounce = new QTimer(this);
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(kDebounceMs);
    connect(m_debounce, &QTimer::timeout, this, &ClipboardListener::onDebounce);
}

// 析构即 stop：卸监听 + 冲账挂起 repeat
ClipboardListener::~ClipboardListener()
{
    stop();
}

void ClipboardListener::noteUpdate()
{
#ifdef Q_OS_WIN
    m_pendingSeq = static_cast<qint64>(GetClipboardSequenceNumber());
    m_debounce->start(kDebounceMs);
#endif
}

bool ClipboardListener::start()
{
#ifdef Q_OS_WIN
    // T-062 修复: 单实例防重入运行期守卫（对齐 GlobalKeyboardHook R6-C4 /
    // ForegroundTracker 口径）——Release 构建下无此守卫时，第二实例 start
    // 会覆盖 g_listener，而第一实例的 message-only 窗口仍在收
    // WM_CLIPBOARDUPDATE，wndProc 经 g_listener 把事件路由到第二实例，
    // 两套防抖/去重状态机互相踩踏
    if (g_listener && g_listener != this) {
        qWarning("ClipboardListener: 已有其他监听器 %p，拒绝二次启动",
                 static_cast<void *>(g_listener));
        return false;
    }
#endif
    if (m_listening)
        return true;
    m_stopped = false; // T-047-FIX(P3-2)：重新开启后恢复在途载荷接收
#ifdef Q_OS_WIN
    WNDCLASSW wc = {};
    wc.lpfnWndProc = clipWndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kWndClassName;
    RegisterClassW(&wc); // 重复注册报 ERROR_CLASS_ALREADY_EXISTS，无害
    // message-only 窗口：不进任务栏/枚举，WM_CLIPBOARDUPDATE 直投本窗口
    m_hwnd = CreateWindowExW(0, kWndClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE,
                             nullptr, wc.hInstance, nullptr);
    const HWND hwnd = static_cast<HWND>(m_hwnd);
    if (!m_hwnd) {
        qWarning("ClipboardListener: 创建 message-only 窗口失败 err=%lu", GetLastError());
        return false;
    }
    // 先挂回调指针再注册监听：首条更新不落空（挂指针前到达的更新
    // 走 DefWindowProc 静默丢弃，可能性极低且无害）
    g_listener = this;
    if (!AddClipboardFormatListener(hwnd)) {
        qWarning("ClipboardListener: AddClipboardFormatListener 失败 err=%lu",
                 GetLastError());
        g_listener = nullptr;
        DestroyWindow(hwnd);
        m_hwnd = nullptr;
        return false;
    }
    m_listening = true;
    return true;
#else
    return false;
#endif
}

void ClipboardListener::stop()
{
    // T-047-FIX(P3-2)：先置停止标志——此后到达的在途提取结果一律丢弃，
    // 防"监控已关仍落一行 entry"与去重/repeat 计数错位
    m_stopped = true;
#ifdef Q_OS_WIN
    if (g_listener == this)
        g_listener = nullptr; // 先摘回调指针，再卸监听（防窗口销毁期间回调悬垂）
    if (m_hwnd) {
        const HWND h = static_cast<HWND>(m_hwnd);
        RemoveClipboardFormatListener(h); // 完全卸载：监控关即零监听
        DestroyWindow(h);
        m_hwnd = nullptr;
    }
#endif
    if (m_debounce)
        m_debounce->stop();
    m_extracting = false;
    m_pendingSeq = 0;
    if (m_listening) {
        m_listening = false;
        flushPendingRepeat(); // 停止前冲账挂起的 repeat（store 仍在运行）
    }
}

// 防抖到点：把最近一次更新的 seq 取走并投入线程池提取（busy 门卫：提取
// 在途时丢弃本次堆积，提取完成后由回包侧补一拍 50ms 防抖）。
// m_pendingSeq==0 说明上一次提取已覆盖本次，无事可做
void ClipboardListener::onDebounce()
{
    if (m_extracting || m_pendingSeq == 0)
        return;
    const qint64 seq = m_pendingSeq;
    m_pendingSeq = 0;
    m_extracting = true; // busy 门卫：提取在途时新触发丢弃（防堆积）
    const QPointer<ClipboardListener> self(this);
    QThreadPool::globalInstance()->start([self, seq] {
        const ClipboardCodec::ClipboardPayload p = readClipboardPayload();
        // 注（T-062 B-4 登记）：下方 if(!self) 与 invokeMethod 解引用之间存在
        // 极窄竞争窗口（销毁恰撞回包投递），代码库已接受同款模式
        //（MonitorQueryTool/FsTool 口径），保持现状防误改
        if (!self)
            return; // 监听器已销毁：结果作废
        QMetaObject::invokeMethod(
            self.data(),
            [self, p, seq] {
                if (!self)
                    return;
                self->m_extracting = false;
                // T-047-FIX(P3-2)：监控已关 → 在途载荷整条丢弃（GUI 线程
                // 判定，无跨线程读取）
                if (self->m_stopped)
                    return;
                self->handlePayload(p, seq);
                if (self->m_pendingSeq != 0)
                    self->m_debounce->start(50); // 提取期间又有更新：补一拍
            },
            Qt::QueuedConnection);
    });
}

// 提取回包落点（GUI 线程）：停止后整条丢弃 → 自写排除 → 空载荷跳过（不动
// 去重状态）→ 去重状态机裁决：内容变化先冲账 repeat 行再落新条目。seq 仅
// 用于自写判定，载荷归属即本次提取结果
void ClipboardListener::handlePayload(const ClipboardCodec::ClipboardPayload &p,
                                      qint64 seq)
{
    // T-047-FIX(P3-2)：停止后不再落任何一行（入口兜底，双保险）
    if (m_stopped)
        return;
    if (OwnWriteGuard::isSelfWrite(seq))
        return; // PopTools 自身写入：整条跳过（防抖结束时 noteSelfWrite 必已执行）
    if (p.kind == ClipboardCodec::ClipboardPayload::Kind::Empty)
        return; // 清空/不可读格式：跳过且不动去重状态
    emit clipboardUpdated(); // 外部内容变化（T-066：文本镜像的粘贴触发源）

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const auto o = m_deduper.observe(p.hash);
    if (!o.repeatHash.isEmpty() && o.repeatCount >= 2) {
        // 内容变化：先冲账上一内容的连续重复（count=总次数，含首条 entry）
        writeRepeatRow(o.repeatHash, o.repeatCount);
    }
    if (o.entry)
        m_store->append(QStringLiteral("clipboard"),
                        ClipboardCodec::buildClipboardEvent(now, p, m_app, m_title));
}

// repeat 冲账行（kind/hash/count/ts）：handlePayload 与 flushPendingRepeat
// 共用（T-062 S-2）；ts 取冲账时刻——两处原实现语义一致
void ClipboardListener::writeRepeatRow(const QString &hash, int count)
{
    QJsonObject r;
    r.insert(QStringLiteral("kind"), QStringLiteral("repeat"));
    r.insert(QStringLiteral("hash"), hash);
    r.insert(QStringLiteral("count"), count);
    r.insert(QStringLiteral("ts"), QDateTime::currentMSecsSinceEpoch());
    m_store->append(QStringLiteral("clipboard"), r);
}

void ClipboardListener::flushPendingRepeat()
{
    if (!m_store)
        return;
    const auto o = m_deduper.flush();
    if (o.repeatHash.isEmpty() || o.repeatCount < 2)
        return;
    writeRepeatRow(o.repeatHash, o.repeatCount);
}

} // namespace poptools
