#include "WindowDetector.h"

#ifdef Q_OS_WIN
#include <windows.h>
#include <oleacc.h>
#include <string>
#endif

namespace snipaste {

#ifdef Q_OS_WIN
namespace {

struct EnumCtx
{
    POINT pt;             // 鼠标点（物理像素）
    HWND exclude;         // 需排除的窗口（截图画布自身）
    HWND best = nullptr;  // 命中的最小面积窗口
    RECT bestRect{};      // 该窗口矩形（物理像素）
    int bestArea = INT_MAX;
};

// 系统窗口（桌面/任务栏等）不应作为检测对象
bool isSystemWindow(HWND hwnd)
{
    wchar_t cls[64] = {};
    if (!GetClassNameW(hwnd, cls, 64))
        return false;
    const std::wstring name(cls);
    return name == L"Progman" || name == L"WorkerW" ||
           name == L"Shell_TrayWnd" || name == L"Shell_SecondaryTrayWnd";
}

BOOL CALLBACK enumWindowProc(HWND hwnd, LPARAM lparam)
{
    auto *ctx = reinterpret_cast<EnumCtx *>(lparam);
    if (hwnd == ctx->exclude || !IsWindowVisible(hwnd))
        return TRUE;
    if (isSystemWindow(hwnd))
        return TRUE;

    RECT r{};
    if (!GetWindowRect(hwnd, &r))
        return TRUE;
    if (ctx->pt.x < r.left || ctx->pt.x > r.right ||
        ctx->pt.y < r.top || ctx->pt.y > r.bottom)
        return TRUE;

    const int area = (r.right - r.left) * (r.bottom - r.top);
    if (area < ctx->bestArea)
    {
        ctx->bestArea = area;
        ctx->best = hwnd;
        ctx->bestRect = r;
    }
    return TRUE;
}

// 找到鼠标下（物理像素）最小面积的可见顶层窗口；无则返回 nullptr
HWND findWindowUnder(const POINT &pt, HWND exclude)
{
    EnumCtx ctx;
    ctx.pt = pt;
    ctx.exclude = exclude;
    EnumWindows(enumWindowProc, reinterpret_cast<LPARAM>(&ctx));
    return ctx.best;
}

QRectF toLogical(const RECT &r, qreal dpr)
{
    // 物理像素 → 逻辑坐标（右/下边界含端点 → 宽度 = right-left+1）
    return QRectF(r.left / dpr, r.top / dpr,
                  (r.right - r.left + 1) / dpr, (r.bottom - r.top + 1) / dpr);
}

// 确保 MSAA 所需 COM 已初始化（主线程 STA）
bool ensureComInitialized()
{
    static const bool ok = [] {
        const HRESULT hr = OleInitialize(nullptr);
        return SUCCEEDED(hr) || hr == S_FALSE || hr == RPC_E_CHANGED_MODE;
    }();
    return ok;
}

} // namespace
#endif

QRect detectWindowRect(const QPoint &logicalPos, void *excludeHwnd, qreal dpr)
{
#ifdef Q_OS_WIN
    if (dpr <= 0.0)
        dpr = 1.0;
    const POINT pt = { qRound(logicalPos.x() * dpr), qRound(logicalPos.y() * dpr) };
    const HWND hwnd = findWindowUnder(pt, static_cast<HWND>(excludeHwnd));
    if (!hwnd)
        return QRect();
    RECT r{};
    if (!GetWindowRect(hwnd, &r))
        return QRect();
    return toLogical(r, dpr).toRect();
#else
    Q_UNUSED(logicalPos)
    Q_UNUSED(excludeHwnd)
    Q_UNUSED(dpr)
    return QRect();
#endif
}

QRect detectUiElementRect(const QPoint &logicalPos, void *excludeHwnd, qreal dpr)
{
#ifdef Q_OS_WIN
    if (dpr <= 0.0)
        dpr = 1.0;
    if (!ensureComInitialized())
        return QRect();

    // 1) 先定位鼠标下的应用窗口（排除截图画布自身）
    const POINT pt = { qRound(logicalPos.x() * dpr), qRound(logicalPos.y() * dpr) };
    const HWND win = findWindowUnder(pt, static_cast<HWND>(excludeHwnd));
    if (!win)
        return QRect();

    // 2) 对该窗口客户区做 MSAA hit-test，取鼠标下 UI 元素的精确矩形
    IAccessible *acc = nullptr;
    if (FAILED(AccessibleObjectFromWindow(win, OBJID_CLIENT, IID_IAccessible,
                                          reinterpret_cast<void **>(&acc))) ||
        !acc)
        return QRect();

    VARIANT child;
    VariantInit(&child);
    const HRESULT hr = acc->accHitTest(pt.x, pt.y, &child);
    if (FAILED(hr))
    {
        VariantClear(&child);
        acc->Release();
        return QRect();
    }

    // 命中元素定位（MSAA 规范）：
    //   * VT_DISPATCH：命中子元素（按钮/输入框/面板等）——须 QI 提升为独立
    //     IAccessible 后以 CHILDID_SELF 取坐标，直接传原 child 会 E_INVALIDARG；
    //   * VT_I4 数值：childID（lVal==CHILDID_SELF 表示命中窗口自身空白处 → 无控件）；
    //   * 其他类型：忽略。
    IAccessible *elem = acc;
    VARIANT elChild = child;
    if (child.vt == VT_DISPATCH && child.pdispVal)
    {
        IAccessible *q = nullptr;
        if (SUCCEEDED(child.pdispVal->QueryInterface(IID_IAccessible,
                                                     reinterpret_cast<void **>(&q))) && q)
        {
            elem = q;
            elChild.vt = VT_I4;
            elChild.lVal = CHILDID_SELF;
        }
    }
    else if (child.vt == VT_I4 && child.lVal == CHILDID_SELF)
    {
        VariantClear(&child);
        acc->Release();
        return QRect();
    }

    long left = 0, top = 0, w = 0, h = 0;
    const HRESULT lhr = elem->accLocation(&left, &top, &w, &h, elChild);
    if (elem != acc)
        elem->Release();
    VariantClear(&child);
    acc->Release();
    if (FAILED(lhr) || w <= 0 || h <= 0)
        return QRect();

    // 3) 过滤过小元素（字符/图标等碎块），回退窗口检测
    if (w < 20 * dpr || h < 20 * dpr)
        return QRect();
    RECT r{ left, top, left + w - 1, top + h - 1 };
    return toLogical(r, dpr).toRect();
#else
    Q_UNUSED(logicalPos)
    Q_UNUSED(excludeHwnd)
    Q_UNUSED(dpr)
    return QRect();
#endif
}

} // namespace snipaste
