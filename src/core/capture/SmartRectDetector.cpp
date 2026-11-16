#include "SmartRectDetector.h"

#include <QMutex>
#include <QMutexLocker>

#include <algorithm>
#include <vector>
#include <cstdio>

#ifdef SMART_DEBUG
#define SDBG(...)                \
    do                           \
    {                            \
        fprintf(stderr, __VA_ARGS__); \
        fflush(stderr);          \
    } while (0)
#else
#define SDBG(...) \
    do            \
    {             \
    } while (0)
#endif

#if defined(Q_OS_WIN)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dwmapi.h>
#include <uiautomationclient.h>
#include <cmath>
#endif

namespace poptools {

// ============================================================
// 工作线程：持有 COM STA 与 IUIAutomation 会话，串行处理最新查询
// ============================================================
class SmartRectDetector::Worker : public QObject
{
    Q_OBJECT
public:
    explicit Worker(QObject *parent = nullptr)
        : QObject(parent)
    {
    }

    // 提交查询（任意线程调用）；忙时覆盖未处理查询（latest-wins）
    void submit(const QPoint &physPos, qreal dpr, quint64 gen, void *exclude)
    {
        {
            const QMutexLocker lock(&m_mux);
            m_query = Query { physPos, dpr, gen, exclude };
            m_hasQuery = true;
        }
        QMetaObject::invokeMethod(this, "drain", Qt::QueuedConnection);
    }

signals:
    void found(quint64 gen, const QRect &logicalRect);

public slots:
    // 兼容线程启动信号；会话创建实际由 ensureSession() 惰性完成
    void init() { ensureSession(); }

    void cleanup()
    {
#if defined(Q_OS_WIN)
        if (m_cache)
        {
            m_cache->Release();
            m_cache = nullptr;
        }
        if (m_trueCond)
        {
            m_trueCond->Release();
            m_trueCond = nullptr;
        }
        if (m_walker)
        {
            m_walker->Release();
            m_walker = nullptr;
        }
        if (m_uia)
        {
            m_uia->Release();
            m_uia = nullptr;
        }
        if (m_lastEl)
        {
            m_lastEl->Release();
            m_lastEl = nullptr;
        }
        // UIA 元素引用必须在 COM STA 关闭（CoUninitialize）之前释放：
        // 跨进程 marshaled 代理在公寓关闭后释放属未定义行为
        if (m_comInit)
        {
            CoUninitialize();
            m_comInit = false;
        }
        m_lastTop = nullptr;
#endif
    }

    // 串行消费最新查询；排队中的多余 drain 因队列为空而立即返回。
    // 检测异常兜底为空矩形：工作线程崩溃会带崩整个进程
    void drain()
    {
        ensureSession(); // 会话就绪后再处理查询（避免与线程启动信号竞态）
        for (;;)
        {
            Query q;
            {
                const QMutexLocker lock(&m_mux);
                if (!m_hasQuery)
                    return;
                m_hasQuery = false;
                q = m_query;
            }
            QRect r;
#if defined(Q_OS_WIN)
            try
            {
                r = detect(q);
            }
            catch (...)
            {
                SDBG("[detect] EXCEPTION caught\n");
                r = QRect();
            }
#else
            r = detect(q);
#endif
            emit found(q.gen, r);
        }
    }

private:
    // 惰性创建 COM/UIA 会话（仅工作线程调用，幂等；调用方线程早于线程启动
    // 信号提交查询时也能在首次 drain 前就绪）
    void ensureSession()
    {
#if defined(Q_OS_WIN)
        if (!m_comInit && SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)))
            m_comInit = true;
        if (m_comInit && !m_uia)
            CoCreateInstance(__uuidof(CUIAutomation), nullptr, CLSCTX_INPROC_SERVER,
                             __uuidof(IUIAutomation), reinterpret_cast<void **>(&m_uia));
        if (m_uia && !m_walker)
            m_uia->get_ControlViewWalker(&m_walker);
        if (m_uia && !m_trueCond)
            m_uia->CreateTrueCondition(&m_trueCond);
        if (m_uia && !m_cache)
        {
            m_uia->CreateCacheRequest(&m_cache);
            if (m_cache)
                m_cache->AddProperty(UIA_BoundingRectanglePropertyId);
        }
        SDBG("[init] session ready=%d\n",
             (m_uia && m_walker && m_trueCond && m_cache) ? 1 : 0);
#endif
    }

    struct Query
    {
        QPoint physPos;
        qreal dpr = 1.0;
        quint64 gen = 0;
        void *exclude = nullptr;
    };

    QRect detect(const Query &q);

    QMutex m_mux;
    Query m_query;
    bool m_hasQuery = false;

#if defined(Q_OS_WIN)
    bool m_comInit = false;
    IUIAutomation *m_uia = nullptr;
    IUIAutomationTreeWalker *m_walker = nullptr;
    IUIAutomationCondition *m_trueCond = nullptr;
    IUIAutomationCacheRequest *m_cache = nullptr;
    std::vector<HWND> m_poked;   // 已发过 OBJID_CLIENT 激活的窗口
    std::vector<HWND> m_noBatch; // 已确认批量查询不可用的窗口（Chromium/Qt 系）
    IUIAutomationElement *m_lastEl = nullptr; // 暖启动：上次命中的元素（持有引用）
    RECT m_lastRect = RECT { 0, 0, -1, -1 };  // invalidRect()（此处置于其定义之前）
    HWND m_lastTop = nullptr;
#endif
};

#if defined(Q_OS_WIN)

namespace {

// 系统窗口（桌面/任务栏）不作为检测对象（维持原智能框选行为）
bool isSystemWindowClass(HWND hwnd)
{
    wchar_t cls[64] = {};
    if (!GetClassNameW(hwnd, cls, 64))
        return false;
    const std::wstring name(cls);
    return name == L"Progman" || name == L"WorkerW" ||
           name == L"Shell_TrayWnd" || name == L"Shell_SecondaryTrayWnd";
}

// 点击穿透的悬浮层不应遮挡真实目标
bool isClickThrough(HWND hwnd)
{
    const LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    return (ex & WS_EX_TRANSPARENT) != 0;
}

// Win10/11：挂起 UWP、其它虚拟桌面窗口 IsWindowVisible 仍为 true，需按 cloaked 过滤
bool isCloaked(HWND hwnd)
{
    int cloaked = 0;
    return SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) &&
           cloaked != 0;
}

// 可见窗口边界：DWM 扩展边界（GetWindowRect 在 Win10 下含左右约 7px 不可见 resize 边框）
RECT frameRectOf(HWND hwnd)
{
    RECT r {};
    if (FAILED(DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &r, sizeof(r))))
        GetWindowRect(hwnd, &r);
    return r;
}

bool containsPt(const RECT &r, LONG x, LONG y)
{
    return x >= r.left && x < r.right && y >= r.top && y < r.bottom;
}

LONGLONG areaOf(const RECT &r)
{
    return LONGLONG(r.right - r.left) * LONGLONG(r.bottom - r.top);
}

RECT invalidRect()
{
    return RECT { 0, 0, -1, -1 };
}

// 有效矩形：宽高均为正（注意不能用 areaOf>0 判断——invalidRect 的"负×负"面积为正）
bool validRect(const RECT &r)
{
    return r.right > r.left && r.bottom > r.top;
}

struct TopLevelCtx
{
    POINT pt;
    HWND exclude;
    HWND found = nullptr;
};

// EnumWindows 按 Z 序自顶向下：首个几何命中即鼠标下最上层窗口
BOOL CALLBACK topLevelProc(HWND hwnd, LPARAM lparam)
{
    auto *ctx = reinterpret_cast<TopLevelCtx *>(lparam);
    if (hwnd == ctx->exclude || !IsWindowVisible(hwnd))
        return TRUE;
    if (isClickThrough(hwnd) || isCloaked(hwnd) || isSystemWindowClass(hwnd))
        return TRUE;
    const RECT r = frameRectOf(hwnd);
    if (r.right - r.left < 8 || r.bottom - r.top < 8)
        return TRUE; // 微型辅助窗口
    if (!containsPt(r, ctx->pt.x, ctx->pt.y))
        return TRUE;
    ctx->found = hwnd;
    return FALSE;
}

struct ChildCtx
{
    POINT pt;
    RECT best = invalidRect();
    int count = 0;
};

// 深度无感知：EnumChildWindows 枚举全部后代，取包含点的最小可见子窗口
BOOL CALLBACK childProc(HWND hwnd, LPARAM lparam)
{
    auto *ctx = reinterpret_cast<ChildCtx *>(lparam);
    if (++ctx->count > 20000)
        return FALSE;
    if (!IsWindowVisible(hwnd) || isClickThrough(hwnd))
        return TRUE;
    RECT r {};
    if (!GetWindowRect(hwnd, &r))
        return TRUE;
    if (r.right - r.left < 2 || r.bottom - r.top < 2)
        return TRUE;
    if (!containsPt(r, ctx->pt.x, ctx->pt.y))
        return TRUE;
    if (ctx->best.right <= ctx->best.left || areaOf(r) < areaOf(ctx->best))
        ctx->best = r;
    return TRUE;
}

// UIA 下行上下文与工具
namespace {

constexpr int kMaxDepth = 32;
constexpr int kMaxNodes = 4000;

struct DescentCtx
{
    IUIAutomationTreeWalker *walker;
    IUIAutomationCondition *trueCond;
    IUIAutomationCacheRequest *cache;
    POINT pt;
    ULONGLONG deadline;
    int nodes = 0;
    bool *findAllBroken = nullptr; // 批量不可用标记（指向调用方持有的持久标记）
};

struct UiKid
{
    IUIAutomationElement *el; // 持有引用
    RECT rect;
};

// 批量收集 node 下包含查询点的子元素（按面积升序，引用转移给调用方）。
// 使用 FindAllBuildCache（XAML/UWP/WPF 等原生 UIA 平台高效）；失败或为空时
// 返回 false，调用方回退到兄弟链遍历（Chromium/Qt 等实现不全的 Provider）。
bool CollectContainingKidsBatch(DescentCtx &cx, IUIAutomationElement *node,
                                std::vector<UiKid> &out)
{
    IUIAutomationElementArray *kids = nullptr;
    if (SUCCEEDED(node->FindAllBuildCache(TreeScope_Children, cx.trueCond, cx.cache, &kids)) && kids)
    {
        int n = 0;
        kids->get_Length(&n);
        for (int i = 0; i < n; ++i)
        {
            IUIAutomationElement *kid = nullptr;
            if (FAILED(kids->GetElement(i, &kid)) || !kid)
                continue;
            RECT r = invalidRect();
            if (SUCCEEDED(kid->get_CachedBoundingRectangle(&r)) && containsPt(r, cx.pt.x, cx.pt.y))
                out.push_back({ kid, r });
            else
                kid->Release();
        }
        kids->Release();
        std::stable_sort(out.begin(), out.end(), [](const UiKid &a, const UiKid &b) {
            return areaOf(a.rect) < areaOf(b.rect);
        });
    }
    return !out.empty();
}

// 带回溯的 DFS：返回 node 分支内包含查询点的最深元素（至少是 node 自身，
// 引用转移给调用方），*outRect 为其矩形。
// 关键点一：Chromium 窗口的子元素里混着无子节点的渲染表面（Intermediate D3D
// Window 等），面积常与真正的内容容器并列最小——死胡同分支必须回退试下一个
// 兄弟，否则永远停在整窗矩形。
// 关键点二：兄弟链路径（批量查询不可用的 Provider）遇到第一个包含点的孩子
// 就立即下行——Web 布局兄弟极少重叠，而"扫完全部兄弟再取最小"在大页面上
// 会耗尽时间预算，永远走不深。
IUIAutomationElement *DescendDeep(DescentCtx &cx, IUIAutomationElement *node,
                                  const RECT &nodeRect, int depth, RECT *outRect)
{
    *outRect = nodeRect;
    if (depth >= kMaxDepth || ++cx.nodes > kMaxNodes || GetTickCount64() > cx.deadline)
        return node;

    IUIAutomationElement *fallback = nullptr; // 首个死胡同孩子（兜底返回值）
    RECT fallbackRect = nodeRect;

    // 路径一：批量读取（原生 UIA 平台）→ 面积升序逐个尝试 + 回退。
    // 一旦批量不可用且兄弟链可用，标记本次下行不再尝试批量（省去每层的无效跨进程调用）
    std::vector<UiKid> kids;
    if (!(cx.findAllBroken && *cx.findAllBroken) &&
        CollectContainingKidsBatch(cx, node, kids))
    {
        for (UiKid &kid : kids)
        {
            RECT sub = invalidRect();
            IUIAutomationElement *deeper = DescendDeep(cx, kid.el, kid.rect, depth + 1, &sub);
            if (deeper != kid.el)
            {
                // 命中更深元素：deeper 是递归转出的新引用；本层全部孩子
                // （含 kid.el 自己——它既未转移也未被返回）一并释放
                for (const UiKid &k : kids)
                    k.el->Release();
                *outRect = sub;
                return deeper;
            }
            if (!fallback)
            {
                fallback = kid.el;
                fallbackRect = kid.rect;
            }
            else
            {
                kid.el->Release();
            }
        }
        for (const UiKid &k : kids)
            if (k.el != fallback)
                k.el->Release();
        *outRect = fallbackRect;
        return fallback ? fallback : node;
    }

    // 路径二：兄弟链遍历（取下一个兄弟在转移/释放当前节点之前完成，避免游标丢失）
    IUIAutomationElement *ch = nullptr;
    if (FAILED(cx.walker->GetFirstChildElementBuildCache(node, cx.cache, &ch)) || !ch)
        return node;
    if (cx.findAllBroken)
        *cx.findAllBroken = true; // 批量不可用但兄弟链有孩子 → 该 Provider 只认兄弟链
    for (int i = 0; i < 1000; ++i)
    {
        if (GetTickCount64() > cx.deadline)
            break;
        RECT r = invalidRect();
        const bool contains = SUCCEEDED(ch->get_CachedBoundingRectangle(&r)) &&
                              containsPt(r, cx.pt.x, cx.pt.y);
        if (!contains)
        {
            IUIAutomationElement *nx = nullptr;
            const HRESULT nhr = cx.walker->GetNextSiblingElementBuildCache(ch, cx.cache, &nx);
            ch->Release();
            ch = nx;
            if (FAILED(nhr) || !ch)
                break;
            continue;
        }
        RECT sub = invalidRect();
        IUIAutomationElement *deeper = DescendDeep(cx, ch, r, depth + 1, &sub);
        IUIAutomationElement *nx = nullptr;
        const HRESULT nhr = cx.walker->GetNextSiblingElementBuildCache(ch, cx.cache, &nx);
        if (deeper != ch)
        {
            // 命中更深元素：释放当前与兜底，立即采纳
            ch->Release();
            if (fallback)
                fallback->Release();
            if (nx)
                nx->Release();
            *outRect = sub;
            return deeper;
        }
        if (!fallback)
        {
            fallback = ch; // 死胡同兜底（引用转移）
            fallbackRect = r;
        }
        else
            ch->Release();
        ch = nx;
        if (FAILED(nhr) || !ch)
            break;
    }
    if (ch)
        ch->Release();
    if (fallback)
    {
        *outRect = fallbackRect;
        return fallback;
    }
    return node;
}

// UIA 下行一次。startEl 非空时从其（暖启动缓存）开始下行——其引用【转移】给
// 本函数（无论成败，调用方此后不得使用）；命中元素经 outHit 带出一个独立
// 引用（调用方负责释放），返回其矩形。
RECT uiaRectAt(IUIAutomation *uia, IUIAutomationTreeWalker *walker,
               IUIAutomationCondition *trueCond, IUIAutomationCacheRequest *cache,
               IUIAutomationElement *startEl, RECT startRect, HWND top, POINT pt,
               ULONGLONG deadline, bool *findAllBroken, IUIAutomationElement **outHit)
{
    *outHit = nullptr;
    if (!uia || !walker || !trueCond || !cache)
    {
        SDBG("[uia] session missing uia=%p walker=%p cond=%p cache=%p\n", (void *)uia,
             (void *)walker, (void *)trueCond, (void *)cache);
        if (startEl)
            startEl->Release();
        return invalidRect();
    }

    IUIAutomationElement *node = nullptr;
    RECT nodeRect = invalidRect();
    if (startEl)
    {
        node = startEl; // 接管引用
        nodeRect = startRect;
    }
    else
    {
        const HRESULT ehr = uia->ElementFromHandle(top, &node);
        if (FAILED(ehr) || !node)
        {
            SDBG("[uia] ElementFromHandle hr=0x%08X\n", ehr);
            return invalidRect();
        }
        if (FAILED(node->get_CurrentBoundingRectangle(&nodeRect)))
        {
            SDBG("[uia] root rect failed\n");
            node->Release();
            return invalidRect();
        }
    }

    DescentCtx cx { walker, trueCond, cache, pt, deadline, 0, findAllBroken };
    RECT hitRect = invalidRect();
    IUIAutomationElement *hit = DescendDeep(cx, node, nodeRect, 0, &hitRect);

    // DescendDeep 转移来的引用归 outHit；node 自身的引用仅在未被返回时释放
    if (hit)
        *outHit = hit;
    if (node != hit)
        node->Release();
    return hitRect;
}

} // namespace（UIA 下行工具）

} // namespace（Win32/UIA 辅助函数）

QRect SmartRectDetector::Worker::detect(const Query &q)
{
    const POINT pt { q.physPos.x(), q.physPos.y() };
    SDBG("[detect] pt=(%ld,%ld)\n", pt.x, pt.y);

    // 1) 顶层窗口（基准矩形）
    TopLevelCtx tctx { pt, static_cast<HWND>(q.exclude) };
    EnumWindows(topLevelProc, reinterpret_cast<LPARAM>(&tctx));
    if (!tctx.found)
    {
        SDBG("[detect] no top-level\n");
        return {};
    }
    RECT frame = frameRectOf(tctx.found);
    SDBG("[detect] top=%p frame=(%ld,%ld,%ld,%ld)\n", (void *)tctx.found, frame.left, frame.top,
         frame.right, frame.bottom);
    RECT best = frame;
    if (!containsPt(best, pt.x, pt.y))
    {
        SDBG("[detect] frame not contains\n");
        return {};
    }

    // 2) 原生子控件
    ChildCtx cctx { pt };
    EnumChildWindows(tctx.found, childProc, reinterpret_cast<LPARAM>(&cctx));
    if (validRect(cctx.best) && areaOf(cctx.best) < areaOf(best))
        best = cctx.best;

    // 3) UIA 控件树（目标窗口挂起时跳过：跨进程调用会被其阻塞）
    if (!IsHungAppWindow(tctx.found))
    {
        // Chromium 系应用（Edge/Electron/WebView2）默认不构建渲染进程无障碍树，
        // 需先以读屏客户端身份发一次 WM_GETOBJECT(OBJID_CLIENT) 激活；每窗口一次。
        // 树在目标进程内异步构建，首次查询可能仍为空，鼠标继续移动即可命中。
        if (m_uia && std::find(m_poked.begin(), m_poked.end(), tctx.found) == m_poked.end())
        {
            DWORD_PTR res = 0;
            SendMessageTimeoutW(tctx.found, WM_GETOBJECT, 0, OBJID_CLIENT, SMTO_ABORTIFHUNG, 200,
                                &res);
            m_poked.push_back(tctx.found);
            if (m_poked.size() > 64)
                m_poked.erase(m_poked.begin()); // 防无限增长（>64 循环复用）
        }

        ULONGLONG budget = 800;
        if (const int env = qEnvironmentVariableIntValue("POPTOOLS_SMART_BUDGET"); env > 0)
            budget = ULONGLONG(env);

        // 暖启动：悬停在空间上连续，鼠标仍在上次命中的元素内时从该元素继续下行，
        // 跳过昂贵的顶层宽层；移出后从头走。所有权：startEl 的引用转移给
        // uiaRectAt（无论成败此后不得使用 startEl）；命中元素以独立引用经
        // outHit 带回并由缓存收下。
        IUIAutomationElement *startEl = nullptr;
        RECT startRect = invalidRect();
        if (m_lastEl && m_lastTop == tctx.found && validRect(m_lastRect) &&
            containsPt(m_lastRect, pt.x, pt.y))
        {
            startEl = m_lastEl; // 引用转移
            startRect = m_lastRect;
        }
        else if (m_lastEl)
        {
            m_lastEl->Release();
        }
        m_lastEl = nullptr;

        const ULONGLONG deadline = GetTickCount64() + budget;
        const ULONGLONG t0 = GetTickCount64();
        RECT hitRect = invalidRect();
        IUIAutomationElement *hit = nullptr;
        bool noBatch = std::find(m_noBatch.begin(), m_noBatch.end(), tctx.found) != m_noBatch.end();
        const bool knownNoBatch = noBatch; // 查询前的既有登记（T-062 修复：区分新旧）
        hitRect = uiaRectAt(m_uia, m_walker, m_trueCond, m_cache, startEl, startRect, tctx.found,
                            pt, deadline, &noBatch, &hit);
        startEl = nullptr; // 所有权已由 uiaRectAt 接管
        // T-062 修复（R1 疑似BUG 3）：仅当本次 UIA 下行"新发现"批量不可用才登记。
        // noBatch 的初值来自上面的 membership 查询，uiaRectAt 只在"批量失败但
        // 兄弟链可用"时置位——若不区分，已在名单的窗口每次查询都重复 push，
        // 逐出策略会挤掉其他窗口的记录，64 容量退化为最近查询窗口的重复表
        if (noBatch && !knownNoBatch)
        {
            m_noBatch.push_back(tctx.found);
            if (m_noBatch.size() > 64)
                m_noBatch.erase(m_noBatch.begin());
        }

        if (validRect(hitRect))
        {
            best = hitRect;
            m_lastEl = hit; // 收下 outHit 的独立引用，供下次暖启动
            hit = nullptr;
            m_lastRect = hitRect;
        }
        else if (hit)
        {
            hit->Release();
            hit = nullptr;
        }
        m_lastTop = tctx.found;
        if (validRect(best))
            m_lastRect = best;
        SDBG("[detect] uia=(%ld,%ld,%ld,%ld) took=%llums budget=%llums\n", best.left, best.top,
             best.right, best.bottom, GetTickCount64() - t0, budget);
    }

    // 高亮裁剪到顶层窗口可见范围（滚动容器等虚拟布局矩形可能伸出屏幕外）
    best.left = std::max(best.left, frame.left);
    best.top = std::max(best.top, frame.top);
    best.right = std::min(best.right, frame.right);
    best.bottom = std::min(best.bottom, frame.bottom);

    // 物理 → 逻辑全局（ScreenCapturer 约定：物理 = 逻辑 × 所在屏 dpr）
    const qreal dpr = q.dpr > 0.0 ? q.dpr : 1.0;
    SDBG("[detect] best=(%ld,%ld,%ld,%ld) dpr=%g\n", best.left, best.top, best.right, best.bottom,
         dpr);
    return QRect(qRound(qreal(best.left) / dpr), qRound(qreal(best.top) / dpr),
                 qRound(qreal(best.right - best.left) / dpr),
                 qRound(qreal(best.bottom - best.top) / dpr));
}

#else // !Q_OS_WIN

QRect SmartRectDetector::Worker::detect(const Query &q)
{
    Q_UNUSED(q)
    return {};
}

#endif

// ============================================================
// 门面（GUI 线程）：持有工作线程并转发
// ============================================================

SmartRectDetector::SmartRectDetector(QObject *parent)
    : QObject(parent)
    // 无 parent：QObject 带 parent 时 moveToThread 按 Qt 规则静默失败，
    // 整套 COM/UIA 检测会滞留 GUI 线程执行（线程形同虚设）
    , m_worker(new Worker(nullptr))
{
    m_worker->moveToThread(&m_thread);
    connect(&m_thread, &QThread::started, m_worker, &Worker::init);
    connect(&m_thread, &QThread::finished, m_worker, &Worker::cleanup);
    connect(m_worker, &Worker::found, this, &SmartRectDetector::found);
    m_thread.start();
}

SmartRectDetector::~SmartRectDetector()
{
    m_thread.quit();
    m_thread.wait(); // finished 已发射，cleanup 已在工作线程执行完毕
    delete m_worker; // 无 parent，显式回收（线程已停，无并发访问）
    m_worker = nullptr;
}

void SmartRectDetector::submit(const QPoint &physPos, qreal dpr, quint64 gen, void *excludeHwnd)
{
    m_worker->submit(physPos, dpr, gen, excludeHwnd); // Worker::submit 线程安全
}

} // namespace poptools

#include "SmartRectDetector.moc"
