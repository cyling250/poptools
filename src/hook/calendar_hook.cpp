// ============================================================
// 桌面日历 Explorer 注入 DLL（照搬 E:\rlqd DesktopHook/desktop_hook.cpp，
// DesktopCal 3.33 行为复刻；T-054 Qt 移植，跨进程标识改为 PopTools 命名）。
//
// 机制（与原版 dkdockexplorerx64.dll 一致）：
//   1. StartDock(hostWnd)：对 ProgMan→SHELLDLL_DefView→SysListView32 所在
//      explorer 线程安装 WH_CALLWNDPROC 钩子（本 DLL 随钩子注入 explorer）；
//   2. 注入后子类化 SHELLDLL_DefView：WM_ERASEBKGND 在内存 DC 合成
//      壁纸+共享内存位图后一次 BitBlt 上屏（日历位于壁纸之上、图标之下，
//      一次上屏消除"壁纸已刷、日历未贴"的中间帧闪烁）；SysListView32：
//      WM_ERASEBKGND 返回 1（透明）并转发鼠标消息（LVM_HITTEST 区分图标）；
//   3. 日历位图由应用渲染进命名共享内存 DIB（CreateDIBSection 绑定共享段）；
//   4. 1s 定时器守护：explorer 重启后自动重注入。
// ============================================================
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <stdint.h>
#include <stdio.h>
#include <stddef.h>

#include "desktop_protocol.h" // 跨进程协议常量唯一事实源（应用侧 ui/shared 同源，S-01）

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "msimg32.lib")

#pragma pack(push, 1)
struct SharedHeader {
    volatile LONG magic;
    volatile LONG width;
    volatile LONG height;
    volatile LONG ready;
    volatile LONG hostWnd;
    volatile LONG slotRect[POPTOOLS_DESKTOP_SHM_SLOTS][4]; // 每槽 L/T/R/B（空槽 0 尺寸，DLL 跳过）
    volatile LONG seq;
    volatile LONG frameIndex; // v4 双缓冲前台帧索引（0/1）：应用整帧写后台后原子翻转，
                              // 擦除侧按索引取帧——任何时刻读到的都是完整帧（撕裂归零）
};
#pragma pack(pop)

// 头部布局与应用侧 POPTOOLS_HDR_* 偏移逐字节对齐（漂移即编译失败，S-01）
static_assert(offsetof(SharedHeader, magic) == POPTOOLS_HDR_MAGIC, "SharedHeader layout drift: magic");
static_assert(offsetof(SharedHeader, width) == POPTOOLS_HDR_WIDTH, "SharedHeader layout drift: width");
static_assert(offsetof(SharedHeader, height) == POPTOOLS_HDR_HEIGHT, "SharedHeader layout drift: height");
static_assert(offsetof(SharedHeader, ready) == POPTOOLS_HDR_READY, "SharedHeader layout drift: ready");
static_assert(offsetof(SharedHeader, hostWnd) == POPTOOLS_HDR_HOSTWND, "SharedHeader layout drift: hostWnd");
static_assert(offsetof(SharedHeader, slotRect) == POPTOOLS_HDR_SLOTRECT0, "SharedHeader layout drift: slotRect");
static_assert(offsetof(SharedHeader, seq) == POPTOOLS_HDR_SEQ, "SharedHeader layout drift: seq");
static_assert(offsetof(SharedHeader, frameIndex) == POPTOOLS_HDR_FRAMEINDEX, "SharedHeader layout drift: frameIndex");
static_assert(sizeof(SharedHeader) == POPTOOLS_HDR_FRAMEINDEX + 4, "SharedHeader layout drift: total size");

static const LONG kMagic = POPTOOLS_DESKTOP_SHM_MAGIC; // CLK4（v4 槽位+双缓冲帧翻转：结构不兼容，失配拒画）

static HINSTANCE g_hInst = NULL;
static HHOOK g_hook = NULL;

static bool g_inited = false;
static bool g_initializing = false;
static HWND g_defview = NULL;
static HWND g_listview = NULL;
static HWND g_msgWnd = NULL;
static HANDLE g_map = NULL;
static SharedHeader* g_hdr = NULL;
static void* g_bits = NULL;
// v4 双缓冲：两块 DIB 绑共享段两块帧区，BlendCalendar 按 header.frameIndex 取前台帧
static HDC g_memDC[2] = { NULL, NULL };
static HBITMAP g_dib[2] = { NULL, NULL };
static HGDIOBJ g_oldDib[2] = { NULL, NULL };
static void* g_bitsArr[2] = { NULL, NULL };
static int g_bmpW = 0, g_bmpH = 0;
static HDC g_bgDC = NULL;      // 双缓冲：壁纸+日历先合成在这里，再一次上屏
static HBITMAP g_bgBmp = NULL;
static HGDIOBJ g_bgOld = NULL;
static int g_bgW = 0, g_bgH = 0;

static LRESULT CALLBACK MsgWndProc(HWND, UINT, WPARAM, LPARAM);
static LRESULT CALLBACK DefViewProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
static LRESULT CALLBACK ListViewProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);

// 追加一行到 %TEMP%\PopToolsCalendarHook.log（诊断用，失败静默）
static void Log(const wchar_t* fmt, ...)
{
    wchar_t path[MAX_PATH];
    DWORD n = GetTempPathW(MAX_PATH, path);
    if (n == 0 || n + 40 >= MAX_PATH) return;
    lstrcatW(path, L"PopToolsCalendarHook.log");
    HANDLE f = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return;
    wchar_t buf[2048];
    int len = swprintf_s(buf, L"[%lu] ", GetCurrentProcessId());
    va_list ap;
    va_start(ap, fmt);
    len += vswprintf_s(buf + len, 2048 - len, fmt, ap);
    va_end(ap);
    if (len > 0) {
        DWORD wrote = 0;
        WriteFile(f, buf, (DWORD)(len * sizeof(wchar_t)), &wrote, NULL);
        WriteFile(f, L"\r\n", 4, &wrote, NULL);
    }
    CloseHandle(f);
}

// 找桌面图标列表：ProgMan→SHELLDLL_DefView→SysListView32 优先，失败再扫
// WorkerW 链（壁纸轮换工具会把 DefView 挪进 WorkerW）
static HWND FindDesktopListView()
{
    HWND prog = FindWindowW(L"ProgMan", NULL);
    if (prog) {
        HWND def = FindWindowExW(prog, NULL, L"SHELLDLL_DefView", NULL);
        if (def) {
            HWND lv = FindWindowExW(def, NULL, L"SysListView32", NULL);
            if (lv) return lv;
        }
    }
    HWND w = NULL;
    while ((w = FindWindowExW(NULL, w, L"WorkerW", NULL)) != NULL) {
        HWND def = FindWindowExW(w, NULL, L"SHELLDLL_DefView", NULL);
        if (def) {
            HWND lv = FindWindowExW(def, NULL, L"SysListView32", NULL);
            if (lv) return lv;
        }
    }
    return NULL;
}

// 释放合成缓冲 DC：先 SelectObject 还原旧位图再删，防删占用对象
static void ReleaseBgDC()
{
    if (g_bgDC && g_bgOld) SelectObject(g_bgDC, g_bgOld);
    if (g_bgBmp) DeleteObject(g_bgBmp);
    if (g_bgDC) DeleteDC(g_bgDC);
    g_bgDC = NULL; g_bgBmp = NULL; g_bgOld = NULL; g_bgW = 0; g_bgH = 0;
}

// 释放共享位图 DC（先还原旧位图再删），并级联释放 bg 合成缓冲
static void ReleaseDib()
{
    for (int i = 0; i < 2; ++i) {
        if (g_memDC[i] && g_oldDib[i]) SelectObject(g_memDC[i], g_oldDib[i]);
        if (g_dib[i]) DeleteObject(g_dib[i]);
        if (g_memDC[i]) DeleteDC(g_memDC[i]);
    }
    g_memDC[0] = g_memDC[1] = NULL;
    g_dib[0] = g_dib[1] = NULL;
    g_oldDib[0] = g_oldDib[1] = NULL;
    g_bits = NULL; g_bitsArr[0] = g_bitsArr[1] = NULL;
    g_bmpW = 0; g_bmpH = 0;
    ReleaseBgDC();
}

// 32bpp 顶行向下的 DIB 头（EnsureDib/EnsureBgDC 共用，S-03 纯提取）
static BITMAPINFO MakeArgbTopDownInfo(int w, int h)
{
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h; // 负高 = 顶行在前（与 QImage/共享位图布局一致）
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    return bi;
}

// 按头部 width/height 惰性建共享段 DIB + 内存 DC；尺寸 ≤0 或 >8192 防御性拒绝
//（防应用侧异常值），尺寸变化才重建（应用侧重建画布后头部尺寸随之更新）
static bool EnsureDib(HDC dc)
{
    if (!g_hdr || !g_map) return false;
    int w = g_hdr->width, h = g_hdr->height;
    if (w <= 0 || h <= 0 || w > 8192 || h > 8192) return false;
    if (g_memDC[0] && g_bmpW == w && g_bmpH == h) return true;
    ReleaseDib();
    BITMAPINFO bi = MakeArgbTopDownInfo(w, h);
    const SIZE_T frameSize = SIZE_T(w) * h * 4;
    for (int i = 0; i < 2; ++i) {
        void* bits = NULL;
        HBITMAP dib = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, g_map,
                                       POPTOOLS_DESKTOP_SHM_OFFSET + frameSize * SIZE_T(i));
        if (!dib) { Log(L"CreateDIBSection[%d] failed %lu", i, GetLastError()); ReleaseDib(); return false; }
        HDC mem = CreateCompatibleDC(dc);
        if (!mem) { DeleteObject(dib); ReleaseDib(); return false; }
        g_oldDib[i] = SelectObject(mem, dib);
        g_memDC[i] = mem; g_dib[i] = dib; g_bitsArr[i] = bits;
    }
    g_bits = g_bitsArr[0];
    g_bmpW = w; g_bmpH = h;
    Log(L"dib created x2 %d x %d bits0=%p bits1=%p (build 20261117d dblbuf+slots)",
        w, h, g_bitsArr[0], g_bitsArr[1]);
    return true;
}

// 惰性建与 DIB 同尺寸合成缓冲；EnsureDib 重建时经 ReleaseDib 级联重建
static bool EnsureBgDC(HDC dc)
{
    if (!g_hdr || !g_map) return false;
    if (g_bgDC && g_bgW == g_bmpW && g_bgH == g_bmpH) return true;
    ReleaseBgDC();
    BITMAPINFO bi = MakeArgbTopDownInfo(g_bmpW, g_bmpH);
    void* bits = NULL;
    HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!bmp) { Log(L"bg dib failed %lu", GetLastError()); return false; }
    HDC mem = CreateCompatibleDC(dc);
    if (!mem) { DeleteObject(bmp); return false; }
    g_bgOld = SelectObject(mem, bmp);
    g_bgDC = mem; g_bgBmp = bmp; g_bgW = g_bmpW; g_bgH = g_bmpH;
    return true;
}

// 整幅共享位图按 AC_SRC_ALPHA 预乘混合到目标 DC（依赖调用方已设裁剪）；
// 失败只记一次日志防刷屏
// 把指定槽矩形区域的画布内容 blend 到 dc 的同位置（dc 逻辑坐标=客户坐标，
// 客户原点即虚拟桌面原点，槽矩形可直接使用）。画布像素 (slotL,slotT) 对应屏幕
// (slotL,slotT)——画布是虚拟桌面并集位图，槽矩形即面板在桌面上的落点。
// 多屏复制/花屏根治：只 blend 槽矩形，绝不整幅位图乱贴
static void BlendSlot(HDC dc, LONG slotL, LONG slotT, LONG slotR, LONG slotB,
                      LONG orgX, LONG orgY)
{
    const int fi = (g_hdr && g_hdr->magic == kMagic) ? (g_hdr->frameIndex & 1) : 0;
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    const int w = int(slotR - slotL), h = int(slotB - slotT);
    // 源：画布位图槽区域（g_memDC 视区原点恒 0，逻辑=位图）；目标逻辑 = 设备 - org
    if (!AlphaBlend(dc, int(slotL - orgX), int(slotT - orgY), w, h,
                    g_memDC[fi], slotL, slotT, w, h, bf)) {
        static bool once = false;
        if (!once) { once = true; Log(L"AlphaBlend failed %lu", GetLastError()); }
    }
}

// 鼠标事件打包转发宿主窗：wParam=ev|extra<<16（extra=点在图标上），
// lParam=MAKELPARAM(屏幕坐标)；解码端 DesktopSurfaceHost::nativeEvent，
// 两侧口径必须一致（协议常量见 desktop_protocol.h）
static void ForwardEvent(UINT ev, int extra, POINT pt)
{
    if (!g_hdr) return;
    HWND host = (HWND)(INT_PTR)g_hdr->hostWnd;
    if (!host || !IsWindow(host)) return;
    PostMessageW(host, POPTOOLS_MSG_CLONE_EVENT, (WPARAM)(ev | (extra << 16)),
                 MAKELPARAM((short)pt.x, (short)pt.y));
}

// 光标处 LVM_HITTEST 是否命中桌面图标（决定 click/dblclk 是否穿透给系统）
static bool IconAtCursor(POINT* screenPt)
{
    GetCursorPos(screenPt);
    if (!g_listview || !IsWindow(g_listview)) return false;
    POINT c = *screenPt;
    ScreenToClient(g_listview, &c);
    LVHITTESTINFO hti = {};
    hti.pt = c;
    SendMessageW(g_listview, LVM_HITTEST, 0, (LPARAM)&hti);
    return hti.iItem != -1;
}

// SHELLDLL_DefView 子类：WM_ERASEBKGND 双缓冲合成（壁纸+日历一次上屏），
// WM_NOTIFY 转发 listview 的 click/dblclk（LVM_HITTEST 判图标定穿透）
static LRESULT CALLBACK DefViewProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                    UINT_PTR uIdSubclass, DWORD_PTR)
{
    if (msg == WM_ERASEBKGND) {
        HDC dc = (HDC)wParam;
        // 槽位并集（±24px：投影 ±18 与发光外扩 ±11 的溢出）——面板只存在于槽矩形内
        RECT slotsUnion = { 0, 0, 0, 0 };
        BOOL haveSlots = FALSE;
        if (g_hdr && g_hdr->magic == kMagic && g_hdr->ready) {
            for (int s = 0; s < POPTOOLS_DESKTOP_SHM_SLOTS; ++s) {
                const LONG l = g_hdr->slotRect[s][0], t = g_hdr->slotRect[s][1];
                const LONG r = g_hdr->slotRect[s][2], b = g_hdr->slotRect[s][3];
                if (r > l && b > t) {
                    if (!haveSlots) {
                        slotsUnion.left = l - 24; slotsUnion.top = t - 24;
                        slotsUnion.right = r + 24; slotsUnion.bottom = b + 24;
                        haveSlots = TRUE;
                    } else {
                        slotsUnion.left = min(slotsUnion.left, l - 24);
                        slotsUnion.top = min(slotsUnion.top, t - 24);
                        slotsUnion.right = max(slotsUnion.right, r + 24);
                        slotsUnion.bottom = max(slotsUnion.bottom, b + 24);
                    }
                }
            }
        }
        bool atomic = false;
        if (haveSlots && EnsureDib(dc)) {
            RECT box;
            const int cb = GetClipBox(dc, &box);
            if (cb != NULLREGION && cb != ERROR && EnsureBgDC(dc)) {
                POINT org = { 0, 0 };
                GetViewportOrgEx(dc, &org);
                // explorer 按区域（带各自视区原点）分块擦除：clip（逻辑）→ 设备 = L+org。
                // 槽位并集外区域与本次擦除不相交时，完全交还系统原生擦除——严禁把
                // 画布位图整幅乱贴（多屏复制/花屏的根因）
                const RECT clipDev = { box.left + org.x, box.top + org.y,
                                       box.right + org.x, box.bottom + org.y };
                RECT inter;
                if (!IntersectRect(&inter, &clipDev, &slotsUnion)) {
                    return DefSubclassProc(hwnd, msg, wParam, lParam);
                }
                HRGN rgnSlots = CreateRectRgnIndirect(&slotsUnion);
                // 第一遍：槽位并集之外的原生擦除（壁纸正确、无面板）
                const int saved = SaveDC(dc);
                if (rgnSlots) {
                    ExtSelectClipRgn(dc, rgnSlots, RGN_DIFF);
                    DeleteObject(rgnSlots);
                }
                DefSubclassProc(hwnd, msg, (WPARAM)dc, lParam);
                RestoreDC(dc, saved);
                // 第二遍：槽位并集区域内双缓冲合成，一次 BitBlt 上屏。
                // 坐标口径：dc 逻辑 L → 设备（客户）D = L + org；bg 位图像素 = 客户坐标；
                // bg 视区原点复制 org 后壁纸按 dc 同款逻辑落位 → bg 位图 = 客户坐标 ✓
                HRGN clipBg = CreateRectRgnIndirect(&inter); // SelectClipRgn 用设备坐标
                if (clipBg) {
                    SelectClipRgn(g_bgDC, clipBg);
                    DeleteObject(clipBg);
                }
                SetViewportOrgEx(g_bgDC, org.x, org.y, NULL);
                POINT brushOrg;
                if (GetBrushOrgEx(dc, &brushOrg)) SetBrushOrgEx(g_bgDC, brushOrg.x, brushOrg.y, NULL);
                DefSubclassProc(hwnd, msg, (WPARAM)g_bgDC, lParam); // 壁纸 → bg
                for (int s = 0; s < POPTOOLS_DESKTOP_SHM_SLOTS; ++s) {
                    const LONG l = g_hdr->slotRect[s][0], t = g_hdr->slotRect[s][1];
                    const LONG r = g_hdr->slotRect[s][2], b = g_hdr->slotRect[s][3];
                    if (r > l && b > t)
                        BlendSlot(g_bgDC, l, t, r, b, org.x, org.y); // 画布槽区域 → bg
                }
                BitBlt(dc, inter.left - org.x, inter.top - org.y,
                       inter.right - inter.left, inter.bottom - inter.top,
                       g_bgDC, inter.left - org.x, inter.top - org.y, SRCCOPY);
                SelectClipRgn(g_bgDC, NULL);
                atomic = true;
            }
        }
        if (!atomic) {
            // 兜底：双缓冲不可用（无裁剪区/内存位图失败）时原生擦除 + 逐槽直贴
            DefSubclassProc(hwnd, msg, wParam, lParam);
            if (g_hdr && g_hdr->magic == kMagic && g_hdr->ready && EnsureDib(dc)) {
                POINT org = { 0, 0 };
                GetViewportOrgEx(dc, &org);
                for (int s = 0; s < POPTOOLS_DESKTOP_SHM_SLOTS; ++s) {
                    const LONG l = g_hdr->slotRect[s][0], t = g_hdr->slotRect[s][1];
                    const LONG r = g_hdr->slotRect[s][2], b = g_hdr->slotRect[s][3];
                    if (r > l && b > t)
                        BlendSlot(dc, l, t, r, b, org.x, org.y);
                }
            }
        }
        return 1;
    }
    if (msg == WM_NOTIFY) {
        NMHDR* nh = (NMHDR*)lParam;
        if (nh && g_listview && nh->hwndFrom == g_listview) {
            if (nh->code == NM_DBLCLK) {
                POINT pt = {};
                int icon = IconAtCursor(&pt) ? 1 : 0;
                ForwardEvent(POPTOOLS_EV_DBLCLK, icon, pt);
            } else if (nh->code == NM_CLICK) {
                POINT pt = {};
                int icon = IconAtCursor(&pt) ? 1 : 0;
                ForwardEvent(POPTOOLS_EV_UP, icon, pt);
            }
        }
    }
    if (msg == WM_NCDESTROY) {
        RemoveWindowSubclass(hwnd, DefViewProc, uIdSubclass);
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

// SysListView32 子类：擦除返 1（透明，桌面由 DefView 层合成）；鼠标事件打包
// 转发宿主窗；双击先 LVM_HITTEST 判图标定穿透
static LRESULT CALLBACK ListViewProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                     UINT_PTR uIdSubclass, DWORD_PTR)
{
    switch (msg) {
        case WM_ERASEBKGND:
            return 1;
        case WM_MOUSEMOVE: {
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ClientToScreen(hwnd, &pt);
            ForwardEvent(POPTOOLS_EV_MOVE, 0, pt);
            break;
        }
        case WM_MOUSELEAVE: {
            POINT pt = { 0, 0 };
            GetCursorPos(&pt);
            ForwardEvent(POPTOOLS_EV_LEAVE, 0, pt);
            break;
        }
        case WM_LBUTTONDOWN: {
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ClientToScreen(hwnd, &pt);
            ForwardEvent(POPTOOLS_EV_DOWN, 0, pt);
            break;
        }
        case WM_LBUTTONUP: {
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ClientToScreen(hwnd, &pt);
            ForwardEvent(POPTOOLS_EV_UP, 0, pt);
            break;
        }
        case WM_LBUTTONDBLCLK: {
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            LVHITTESTINFO hti = {};
            hti.pt = pt;
            SendMessageW(hwnd, LVM_HITTEST, 0, (LPARAM)&hti);
            int icon = (hti.iItem != -1) ? 1 : 0;
            ClientToScreen(hwnd, &pt);
            ForwardEvent(POPTOOLS_EV_DBLCLK, icon, pt);
            break;
        }
        case WM_NCDESTROY:
            RemoveWindowSubclass(hwnd, ListViewProc, uIdSubclass);
            break;
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

// 幂等清理：定时器/消息窗 → 双子类 → GDI/映射 → 重画桌面恢复原貌；STOP、
// WM_DESTROY、进程退出多入口可达，全局清空保证可重入
static void CleanupExplorer()
{
    if (g_msgWnd) {
        KillTimer(g_msgWnd, 1);
        DestroyWindow(g_msgWnd);
        g_msgWnd = NULL;
    }
    HWND def = g_defview;
    HWND lv = g_listview;
    if (g_defview && IsWindow(g_defview)) RemoveWindowSubclass(g_defview, DefViewProc, 0xC1C1);
    if (g_listview && IsWindow(g_listview)) RemoveWindowSubclass(g_listview, ListViewProc, 0xC1C2);
    g_defview = NULL;
    g_listview = NULL;
    ReleaseDib();
    if (g_hdr) { UnmapViewOfFile(g_hdr); g_hdr = NULL; }
    if (g_map) { CloseHandle(g_map); g_map = NULL; }
    g_inited = false;
    if (def && IsWindow(def)) {
        InvalidateRect(def, NULL, TRUE);
        UpdateWindow(def);
    }
    if (lv && IsWindow(lv)) {
        InvalidateRect(lv, NULL, TRUE);
        UpdateWindow(lv);
    }
}

// 注入后初始化（可重入）：子类存活即短路；窗口失效先 CleanupExplorer 再重查
// 重挂（explorer 重启场景）；DLL 消息窗 + 1s 看门狗在此创建
static bool EnsureInit()
{
    if (g_initializing) return g_inited;
    g_initializing = true;
    struct Guard { bool* p; ~Guard() { *p = false; } } guard{ &g_initializing };
    if (g_inited) {
        if (g_defview && IsWindow(g_defview) && g_listview && IsWindow(g_listview)) return true;
        CleanupExplorer();
    }
    HWND lv = FindDesktopListView();
    if (!lv) return false;
    HWND def = GetParent(lv);
    if (!def) return false;
    if (!g_map) {
        g_map = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, POPTOOLS_DESKTOP_SHM_NAME);
        if (!g_map) {
            static bool once = false;
            if (!once) { once = true; Log(L"OpenFileMapping failed %lu", GetLastError()); }
            return false;
        }
        g_hdr = (SharedHeader*)MapViewOfFile(g_map, FILE_MAP_ALL_ACCESS, 0, 0, 0);
        if (!g_hdr) { CloseHandle(g_map); g_map = NULL; return false; }
        Log(L"mapped shared header magic=%08X w=%d h=%d ready=%d host=%08X",
            g_hdr->magic, g_hdr->width, g_hdr->height, g_hdr->ready, g_hdr->hostWnd);
    }
    if (!g_msgWnd) {
        WNDCLASSEXW wc = { sizeof(wc) };
        wc.lpfnWndProc = MsgWndProc;
        wc.hInstance = g_hInst;
        wc.lpszClassName = L"PopToolsCalendarHook_Framework";
        RegisterClassExW(&wc);
        g_msgWnd = CreateWindowExW(0, L"PopToolsCalendarHook_Framework", L"PopToolsCalendarHook_Title",
                                   WS_POPUP, 0, 0, 0, 0, NULL, NULL, g_hInst, NULL);
        SetTimer(g_msgWnd, 1, 1000, NULL);
        Log(L"msg window=%p", g_msgWnd);
    }
    g_defview = def;
    g_listview = lv;
    bool s1 = SetWindowSubclass(def, DefViewProc, 0xC1C1, 0) ? true : false;
    bool s2 = SetWindowSubclass(lv, ListViewProc, 0xC1C2, 0) ? true : false;
    Log(L"subclass def=%p lv=%p ok=%d/%d", def, lv, s1, s2);
    /* T-062 修复: BUG-05 原来无条件 g_inited=true——部分子类化失败（如 s1 败
       s2 成）时 EnsureInit 早退、1s 看门狗不再重试，鼠标转发活着但永不渲染。
       改为 s1&&s2：SetWindowSubclass 同 id 重装即替换，看门狗每秒重试幂等安全 */
    g_inited = s1 && s2;
    InvalidateRect(def, NULL, TRUE);
    InvalidateRect(lv, NULL, TRUE);
    return true;
}

// DLL 消息窗过程：应用 UPDATE 按槽失效、STOP 清理自毁、TIMER(1s) 看门狗重初始化
static LRESULT CALLBACK MsgWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
        case POPTOOLS_MSG_CLONE_UPDATE:
        {
            // wParam!=0：全失效兜底（确认恢复/槽位切换等场景，窄区域遗漏一次性补齐）
            if (wParam != 0) {
                if (g_defview && IsWindow(g_defview))
                    InvalidateRect(g_defview, NULL, TRUE);
                if (g_listview && IsWindow(g_listview))
                    InvalidateRect(g_listview, NULL, TRUE);
                return 0;
            }
            // 脏矩形失效（红线 4）：按各面板槽矩形 + 24px 边距逐槽失效（覆盖
            // 投影 ±18px 与发光外扩 ±11px 的溢出）；全部槽皆空时兜底全失效。
            // 整桌面失效会让 hover 这类高频重绘连壁纸一起重刷，肉眼闪烁。
            //
            // 拖动/改列数场景（T-060 花屏修复）：共享位图整帧原子更新后旧位置
            // 像素已清空，但若只失效新矩形，explorer 不重绘旧位置——屏幕上
            // 残留上一次混合的旧画面（花屏残影）。故每槽记上次失效矩形，
            // 本次失效 = 当前 ∪ 上次（变了才多失效一次），残影无处可留
            static RECT s_prev[POPTOOLS_DESKTOP_SHM_SLOTS] = {};
            static BOOL s_prevValid[POPTOOLS_DESKTOP_SHM_SLOTS] = {};
            RECT rc[POPTOOLS_DESKTOP_SHM_SLOTS * 2];
            int nRect = 0;
            if (g_hdr && g_hdr->ready) {
                for (int s = 0; s < POPTOOLS_DESKTOP_SHM_SLOTS; ++s) {
                    const LONG l = g_hdr->slotRect[s][0], t = g_hdr->slotRect[s][1];
                    const LONG r = g_hdr->slotRect[s][2], b = g_hdr->slotRect[s][3];
                    const BOOL curValid = (r > l && b > t) ? TRUE : FALSE;
                    const RECT cur = { l, t, r, b };
                    const BOOL prevChanged =
                        s_prevValid[s] != curValid
                        || !s_prevValid[s]
                        || memcmp(&s_prev[s], &cur, sizeof(RECT)) != 0;
                    if (curValid) {
                        rc[nRect].left = l - 24;
                        rc[nRect].top = t - 24;
                        rc[nRect].right = r + 24;
                        rc[nRect].bottom = b + 24;
                        ++nRect;
                    }
                    if (s_prevValid[s] && (!curValid || prevChanged)) {
                        // 旧位置已不在位图上（移动/缩放/隐藏）——一并失效清残影
                        rc[nRect].left = s_prev[s].left - 24;
                        rc[nRect].top = s_prev[s].top - 24;
                        rc[nRect].right = s_prev[s].right + 24;
                        rc[nRect].bottom = s_prev[s].bottom + 24;
                        ++nRect;
                    }
                    s_prev[s] = cur;
                    s_prevValid[s] = curValid;
                }
            }
            // 一条矩形都没算出（全部空槽/未就绪）→ NULL 全失效兜底，保证首帧必定上屏
            const int count = nRect > 0 ? nRect : 1;
            for (int i = 0; i < count; ++i) {
                const BOOL haveRect = nRect > 0 ? TRUE : FALSE;
                if (g_defview && IsWindow(g_defview))
                    InvalidateRect(g_defview, haveRect ? &rc[i] : NULL, TRUE);
                if (g_listview && IsWindow(g_listview))
                    InvalidateRect(g_listview, haveRect ? &rc[i] : NULL, TRUE);
            }
            return 0;
        }
        case POPTOOLS_MSG_CLONE_STOP:
        {
            HWND self = g_msgWnd;
            g_msgWnd = NULL;
            CleanupExplorer();
            if (self && IsWindow(self)) DestroyWindow(self);
            return 0;
        }
        case WM_TIMER:
            if (wParam == 1) {
                if (!g_inited || !g_defview || !IsWindow(g_defview)) {
                    EnsureInit();
                }
            }
            return 0;
        case WM_DESTROY:
            g_msgWnd = NULL;
            CleanupExplorer();
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// 钩子过程：WH_CALLWNDPROC 首条消息到达时跑 EnsureInit——这是 DLL 真正完成
// 注入初始化的触发点（钩子回调即代表本 DLL 已被加载进 explorer）
static LRESULT CALLBACK HookProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode >= 0) EnsureInit();
    return CallNextHookEx(NULL, nCode, wParam, lParam);
}

// 安装 WH_CALLWNDPROC 钩子触发注入：先把宿主 HWND 写进共享头（与应用侧
// setHostWindow 互为冗余保险），再对桌面 listview 所在线程装钩子，末尾
// WM_NULL 强制钩子立即执行以完成注入；重复调用先摘旧钩子（幂等）。
// 返回 1=钩子已装（注入在钩子回调 EnsureInit 中完成），0=无桌面 listview 或
// 装钩失败
extern "C" __declspec(dllexport) int StartDock(HWND hostWnd)
{
    Log(L"StartDock host=%p", hostWnd);
    HANDLE m = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, POPTOOLS_DESKTOP_SHM_NAME);
    if (m) {
        SharedHeader* h = (SharedHeader*)MapViewOfFile(m, FILE_MAP_ALL_ACCESS, 0, 0, 0);
        if (h) { h->hostWnd = (LONG)(INT_PTR)hostWnd; UnmapViewOfFile(h); }
        CloseHandle(m);
    }
    HWND lv = FindDesktopListView();
    if (!lv) { Log(L"StartDock: no listview"); return 0; }
    DWORD tid = GetWindowThreadProcessId(lv, NULL);
    if (g_hook) { UnhookWindowsHookEx(g_hook); g_hook = NULL; }
    g_hook = SetWindowsHookExW(WH_CALLWNDPROC, HookProc, g_hInst, tid);
    Log(L"hook=%p tid=%lu err=%lu", g_hook, tid, GetLastError());
    if (!g_hook) return 0;
    PostMessageW(lv, WM_NULL, 0, 0);
    // WH_CALLWNDPROC 只拦 SendMessage 投递的消息——显式发一条无害 WM_NULL
    // 强制目标线程立刻执行钩子过程（触发 DLL 注入），不等环境消息
    DWORD_PTR sentRet = 0;
    SendMessageTimeoutW(lv, WM_NULL, 0, 0, SMTO_ABORTIFHUNG, 1000, &sentRet);
    return 1;
}

// 摘钩子并投 WM_CLONE_STOP 让 DLL 消息窗自毁（异步完成子类/GDI/映射清理与
// 桌面重画；立即返回，不等待清理完成）
extern "C" __declspec(dllexport) int StopDock()
{
    if (g_hook) { UnhookWindowsHookEx(g_hook); g_hook = NULL; }
    HWND w = FindWindowW(L"PopToolsCalendarHook_Framework", L"PopToolsCalendarHook_Title");
    if (w) PostMessageW(w, POPTOOLS_MSG_CLONE_STOP, 0, 0);
    return 1;
}

// DLL_PROCESS_DETACH 走 CleanupExplorer：进程终止期调用有风险，见下方修复注释（BUG-06）
BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpvReserved)
{
    if (reason == DLL_PROCESS_ATTACH) {
        g_hInst = hModule;
        DisableThreadLibraryCalls(hModule);
    } else if (reason == DLL_PROCESS_DETACH) {
        /* T-062 修复: BUG-06 进程终止期（lpvReserved 非空）loader lock 持有、
           comctl32 可能已卸载、其他线程已死——RemoveWindowSubclass/DestroyWindow/
           UpdateWindow 均不可靠，强行清理可能给 explorer 退出引入 AV（留 WER
           记录/拖慢关机）。进程消亡自然释放一切句柄，终止期直接跳过清理；
           FreeLibrary 路径（lpvReserved==NULL）保留完整清理 */
        if (lpvReserved == NULL)
            CleanupExplorer();
    }
    return TRUE;
}
