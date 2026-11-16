// ============================================================
// 桌面日历 Explorer 注入 DLL（照搬 E:\rlqd DesktopHook/desktop_hook.cpp，
// DesktopCal 3.33 行为复刻；T-054 Qt 移植，跨进程标识改为 PopTools 命名）。
//
// 机制（与原版 dkdockexplorerx64.dll 一致）：
//   1. StartDock(hostWnd)：对 ProgMan→SHELLDLL_DefView→SysListView32 所在
//      explorer 线程安装 WH_CALLWNDPROC 钩子（本 DLL 随钩子注入 explorer）；
//   2. 注入后子类化 SHELLDLL_DefView：WM_ERASEBKGND 先画壁纸再 AlphaBlend
//      共享内存位图（日历位于壁纸之上、图标之下）；SysListView32：
//      WM_ERASEBKGND 返回 1（透明）并转发鼠标消息（LVM_HITTEST 区分图标）；
//   3. 日历位图由应用渲染进命名共享内存 DIB（CreateDIBSection 绑定共享段）；
//   4. 1s 定时器守护：explorer 重启后自动重注入。
// ============================================================
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <stdint.h>
#include <stdio.h>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "msimg32.lib")

#define SHM_NAME L"Local\\PopToolsCalendarShared_v1"
#define SHM_OFFSET 4096
#define WM_CLONE_UPDATE (WM_APP + 0x101)
#define WM_CLONE_STOP   (WM_APP + 0x102)
#define WM_CLONE_EVENT  (WM_APP + 0x103)

#define EV_MOVE    1
#define EV_DOWN    2
#define EV_UP      3
#define EV_DBLCLK  4
#define EV_LEAVE   5

#pragma pack(push, 1)
struct SharedHeader {
    volatile LONG magic;
    volatile LONG width;
    volatile LONG height;
    volatile LONG ready;
    volatile LONG hostWnd;
    volatile LONG rectLeft;
    volatile LONG rectTop;
    volatile LONG rectRight;
    volatile LONG rectBottom;
    volatile LONG seq;
};
#pragma pack(pop)

static const LONG kMagic = 0x434C4B31; // CLK1（与 rlqd 同值，协议不变）

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
static HDC g_memDC = NULL;
static HBITMAP g_dib = NULL;
static HGDIOBJ g_oldDib = NULL;
static int g_bmpW = 0, g_bmpH = 0;

static LRESULT CALLBACK MsgWndProc(HWND, UINT, WPARAM, LPARAM);
static LRESULT CALLBACK DefViewProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
static LRESULT CALLBACK ListViewProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);

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

static void ReleaseDib()
{
    if (g_memDC && g_oldDib) SelectObject(g_memDC, g_oldDib);
    if (g_dib) DeleteObject(g_dib);
    if (g_memDC) DeleteDC(g_memDC);
    g_memDC = NULL; g_dib = NULL; g_oldDib = NULL; g_bits = NULL; g_bmpW = 0; g_bmpH = 0;
}

static bool EnsureDib(HDC dc)
{
    if (!g_hdr || !g_map) return false;
    int w = g_hdr->width, h = g_hdr->height;
    if (w <= 0 || h <= 0 || w > 8192 || h > 8192) return false;
    if (g_memDC && g_bmpW == w && g_bmpH == h) return true;
    ReleaseDib();
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = NULL;
    HBITMAP dib = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, g_map, SHM_OFFSET);
    if (!dib) { Log(L"CreateDIBSection failed %lu", GetLastError()); return false; }
    HDC mem = CreateCompatibleDC(dc);
    if (!mem) { DeleteObject(dib); return false; }
    HGDIOBJ old = SelectObject(mem, dib);
    g_memDC = mem; g_dib = dib; g_oldDib = old; g_bits = bits; g_bmpW = w; g_bmpH = h;
    Log(L"dib created %d x %d bits=%p", w, h, bits);
    return true;
}

static void ForwardEvent(UINT ev, int extra, POINT pt)
{
    if (!g_hdr) return;
    HWND host = (HWND)(INT_PTR)g_hdr->hostWnd;
    if (!host || !IsWindow(host)) return;
    PostMessageW(host, WM_CLONE_EVENT, (WPARAM)(ev | (extra << 16)),
                 MAKELPARAM((short)pt.x, (short)pt.y));
}

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

static LRESULT CALLBACK DefViewProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                    UINT_PTR uIdSubclass, DWORD_PTR)
{
    if (msg == WM_ERASEBKGND) {
        DefSubclassProc(hwnd, msg, wParam, lParam);
        if (g_hdr && g_hdr->magic == kMagic && g_hdr->ready) {
            HDC dc = (HDC)wParam;
            if (EnsureDib(dc)) {
                BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
                BOOL ok = AlphaBlend(dc, 0, 0, g_bmpW, g_bmpH, g_memDC, 0, 0, g_bmpW, g_bmpH, bf);
                if (!ok) {
                    static bool once = false;
                    if (!once) { once = true; Log(L"AlphaBlend failed %lu", GetLastError()); }
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
                ForwardEvent(EV_DBLCLK, icon, pt);
            } else if (nh->code == NM_CLICK) {
                POINT pt = {};
                int icon = IconAtCursor(&pt) ? 1 : 0;
                ForwardEvent(EV_UP, icon, pt);
            }
        }
    }
    if (msg == WM_NCDESTROY) {
        RemoveWindowSubclass(hwnd, DefViewProc, uIdSubclass);
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

static LRESULT CALLBACK ListViewProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                     UINT_PTR uIdSubclass, DWORD_PTR)
{
    switch (msg) {
        case WM_ERASEBKGND:
            return 1;
        case WM_MOUSEMOVE: {
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ClientToScreen(hwnd, &pt);
            ForwardEvent(EV_MOVE, 0, pt);
            break;
        }
        case WM_MOUSELEAVE: {
            POINT pt = { 0, 0 };
            GetCursorPos(&pt);
            ForwardEvent(EV_LEAVE, 0, pt);
            break;
        }
        case WM_LBUTTONDOWN: {
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ClientToScreen(hwnd, &pt);
            ForwardEvent(EV_DOWN, 0, pt);
            break;
        }
        case WM_LBUTTONUP: {
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ClientToScreen(hwnd, &pt);
            ForwardEvent(EV_UP, 0, pt);
            break;
        }
        case WM_LBUTTONDBLCLK: {
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            LVHITTESTINFO hti = {};
            hti.pt = pt;
            SendMessageW(hwnd, LVM_HITTEST, 0, (LPARAM)&hti);
            int icon = (hti.iItem != -1) ? 1 : 0;
            ClientToScreen(hwnd, &pt);
            ForwardEvent(EV_DBLCLK, icon, pt);
            break;
        }
        case WM_NCDESTROY:
            RemoveWindowSubclass(hwnd, ListViewProc, uIdSubclass);
            break;
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

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
        g_map = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, SHM_NAME);
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
    g_inited = true;
    InvalidateRect(def, NULL, TRUE);
    InvalidateRect(lv, NULL, TRUE);
    return true;
}

static LRESULT CALLBACK MsgWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
        case WM_CLONE_UPDATE:
            if (g_defview && IsWindow(g_defview)) InvalidateRect(g_defview, NULL, TRUE);
            if (g_listview && IsWindow(g_listview)) InvalidateRect(g_listview, NULL, TRUE);
            return 0;
        case WM_CLONE_STOP:
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

static LRESULT CALLBACK HookProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode >= 0) EnsureInit();
    return CallNextHookEx(NULL, nCode, wParam, lParam);
}

extern "C" __declspec(dllexport) int StartDock(HWND hostWnd)
{
    Log(L"StartDock host=%p", hostWnd);
    HANDLE m = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, SHM_NAME);
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

extern "C" __declspec(dllexport) int StopDock()
{
    if (g_hook) { UnhookWindowsHookEx(g_hook); g_hook = NULL; }
    HWND w = FindWindowW(L"PopToolsCalendarHook_Framework", L"PopToolsCalendarHook_Title");
    if (w) PostMessageW(w, WM_CLONE_STOP, 0, 0);
    return 1;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        g_hInst = hModule;
        DisableThreadLibraryCalls(hModule);
    } else if (reason == DLL_PROCESS_DETACH) {
        CleanupExplorer();
    }
    return TRUE;
}
