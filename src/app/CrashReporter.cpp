// T-051 崩溃自诊实现。崩溃路径纪律：禁堆、禁 Qt、禁 CRT 流——只用
// Win32 + dbghelp + 栈上/静态缓冲，保证堆损坏/栈溢出场景下仍能落盘。
#include "CrashReporter.h"

#include <windows.h>
#include <tlhelp32.h>
#include <dbghelp.h>

#include <csignal>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <exception>

#include <QMessageLogContext>
#include <QString>

#pragma comment(lib, "dbghelp.lib")

namespace poptools {
namespace {

constexpr int kMaxFrames = 64;
constexpr int kMaxOtherThreads = 32;

wchar_t g_logPath[MAX_PATH + 40] = {};
wchar_t g_dumpPath[MAX_PATH + 40] = {};
bool g_symReady = false;
bool g_inHook = false; // 递归护栏：崩溃处理器内再崩直接 _exit

// ---------- 日志（UTF-16，WriteFile 直写） ----------

void logLine(HANDLE file, const wchar_t *fmt, ...)
{
    if (!file || file == INVALID_HANDLE_VALUE)
        return;
    wchar_t buf[1024];
    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf_s(buf, 1024, _TRUNCATE, fmt, ap);
    va_end(ap);
    const size_t len = wcsnlen_s(buf, 1024);
    buf[len] = L'\r';
    buf[len + 1] = L'\n';
    DWORD written = 0;
    WriteFile(file, buf, DWORD((len + 2) * sizeof(wchar_t)), &written, nullptr);
}

// ---------- 路径：exe 旁 crash/PopTools-<时间戳>.{log,dmp} ----------

void preparePaths()
{
    if (g_logPath[0])
        return;
    wchar_t exe[MAX_PATH] = {};
    if (!GetModuleFileNameW(nullptr, exe, MAX_PATH))
        return;
    wchar_t *cut = wcsrchr(exe, L'\\');
    if (!cut)
        return;
    *cut = L'\0';
    wchar_t dir[MAX_PATH + 16];
    swprintf_s(dir, L"%s\\crash", exe);
    CreateDirectoryW(dir, nullptr);
    SYSTEMTIME st;
    GetLocalTime(&st);
    swprintf_s(g_dumpPath,
               L"%s\\PopTools-%04d%02d%02d-%02d%02d%02d.dmp", dir,
               st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    swprintf_s(g_logPath,
               L"%s\\PopTools-%04d%02d%02d-%02d%02d%02d.log", dir,
               st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
}

// ---------- MiniDump ----------

void writeDump(EXCEPTION_POINTERS *ep)
{
    if (!g_dumpPath[0])
        return;
    HANDLE f = CreateFileW(g_dumpPath, GENERIC_WRITE, 0, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE)
        return;
    MINIDUMP_EXCEPTION_INFORMATION mei;
    mei.ThreadId = GetCurrentThreadId();
    mei.ExceptionPointers = ep;
    mei.ClientPointers = FALSE;
    const MINIDUMP_TYPE type = static_cast<MINIDUMP_TYPE>(
        MiniDumpNormal | MiniDumpWithHandleData | MiniDumpWithProcessThreadData
        | MiniDumpWithUnloadedModules);
    MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), f, type,
                      ep ? &mei : nullptr, nullptr, nullptr);
    CloseHandle(f);
}

// ---------- 符号化调用栈 ----------

void ensureSymbols(HANDLE process)
{
    if (g_symReady)
        return;
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS
                  | SYMOPT_LOAD_LINES | SYMOPT_EXACT_SYMBOLS);
    g_symReady = SymInitializeW(process, nullptr, TRUE) != FALSE;
}

void logStackTrace(HANDLE file, HANDLE process, HANDLE thread, CONTEXT *ctx)
{
    ensureSymbols(process);
    if (!g_symReady) {
        logLine(file, L"  [栈] SymInitialize 失败 err=%lu", GetLastError());
        return;
    }
    STACKFRAME64 frame = {};
    frame.AddrPC.Offset = ctx->Rip;
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Offset = ctx->Rsp;
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Offset = ctx->Rsp;
    frame.AddrStack.Mode = AddrModeFlat;
    CONTEXT ctxCopy = *ctx;
    for (int i = 0; i < kMaxFrames; ++i) {
        if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, thread, &frame,
                         &ctxCopy, nullptr, SymFunctionTableAccess64,
                         SymGetModuleBase64, nullptr))
            break;
        const DWORD64 pc = frame.AddrPC.Offset;
        if (!pc)
            break;
        wchar_t modName[96] = L"?";
        const DWORD64 base = SymGetModuleBase64(process, pc);
        if (base) {
            IMAGEHLP_MODULEW64 mi = {};
            mi.SizeOfStruct = sizeof(mi);
            if (SymGetModuleInfoW64(process, base, &mi))
                lstrcpynW(modName, mi.ModuleName, 96);
        }
        wchar_t sym[260] = L"";
        DWORD64 symDisp = 0;
        alignas(SYMBOL_INFOW) unsigned char
            symBuf[sizeof(SYMBOL_INFOW) + 256 * sizeof(wchar_t)] = {};
        auto *si = reinterpret_cast<SYMBOL_INFOW *>(symBuf);
        si->SizeOfStruct = sizeof(SYMBOL_INFOW);
        si->MaxNameLen = 255;
        if (SymFromAddrW(process, pc, &symDisp, si))
            swprintf_s(sym, L"%s+0x%llx", si->Name,
                       static_cast<unsigned long long>(symDisp));
        wchar_t lineInfo[180] = L"";
        DWORD lineDisp = 0;
        IMAGEHLP_LINEW64 li = {};
        li.SizeOfStruct = sizeof(li);
        if (SymGetLineFromAddrW64(process, pc, &lineDisp, &li))
            swprintf_s(lineInfo, L"  (%s:%lu)", li.FileName,
                       static_cast<unsigned long>(li.LineNumber));
        logLine(file, L"  #%02d %s!%s%s", i, modName, sym, lineInfo);
    }
}

// ---------- 其余线程现场（钩子/worker 线程可能在关键路径上） ----------

void logOtherThreads(HANDLE file, HANDLE process)
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE)
        return;
    THREADENTRY32 te = {};
    te.dwSize = sizeof(te);
    const DWORD pid = GetCurrentProcessId();
    const DWORD curTid = GetCurrentThreadId();
    int walked = 0;
    if (Thread32First(snap, &te)) {
        do {
            if (te.th32OwnerProcessID != pid || te.th32ThreadID == curTid)
                continue;
            if (++walked > kMaxOtherThreads)
                break;
            HANDLE t = OpenThread(
                THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT
                    | THREAD_QUERY_INFORMATION,
                FALSE, te.th32ThreadID);
            if (!t)
                continue;
            if (SuspendThread(t) != DWORD(-1)) {
                CONTEXT ctx = {};
                ctx.ContextFlags = CONTEXT_FULL;
                if (GetThreadContext(t, &ctx)) {
                    logLine(file, L"-- 线程 %lu --",
                            static_cast<unsigned long>(te.th32ThreadID));
                    logStackTrace(file, process, t, &ctx);
                }
                ResumeThread(t);
            }
            CloseHandle(t);
        } while (Thread32Next(snap, &te));
    }
    CloseHandle(snap);
}

// ---------- 汇报主体 ----------

void report(const wchar_t *headline, EXCEPTION_POINTERS *ep)
{
    if (g_inHook)
        _exit(3);
    g_inHook = true;
    preparePaths();
    HANDLE f = g_logPath[0]
                   ? CreateFileW(g_logPath, GENERIC_WRITE, 0, nullptr,
                                 CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr)
                   : INVALID_HANDLE_VALUE;
    logLine(f, L"== PopTools 崩溃日志 ==");
    logLine(f, L"原因: %s", headline);
    if (ep && ep->ExceptionRecord) {
        const EXCEPTION_RECORD *rec = ep->ExceptionRecord;
        logLine(f, L"异常 code=0x%08lX at 0x%p",
                static_cast<unsigned long>(rec->ExceptionCode),
                rec->ExceptionAddress);
        if (rec->ExceptionCode == 0xC0000005 && rec->NumberParameters >= 2)
            logLine(f, L"访问违例: %s 0x%p",
                    rec->ExceptionInformation[0] == 0
                        ? L"读取"
                        : (rec->ExceptionInformation[0] == 1 ? L"写入"
                                                             : L"DEP执行"),
                    reinterpret_cast<void *>(rec->ExceptionInformation[1]));
    }
    writeDump(ep);
    HANDLE process = GetCurrentProcess();
    if (ep && ep->ContextRecord)
        logStackTrace(f, process, GetCurrentThread(), ep->ContextRecord);
    logOtherThreads(f, process);
    if (f != INVALID_HANDLE_VALUE)
        CloseHandle(f);
}

// ---------- 各类中止入口 ----------

LONG WINAPI unhandledFilter(EXCEPTION_POINTERS *ep)
{
    report(L"未处理异常（SetUnhandledExceptionFilter）", ep);
    return EXCEPTION_EXECUTE_HANDLER;
}

[[noreturn]] void abortHook(const char *why)
{
    wchar_t headline[256];
    swprintf_s(headline, L"%hs", why);
    report(headline, nullptr);
    _exit(3);
}

void __cdecl onTerminate()
{
    abortHook("std::terminate()（qFatal / 未捕获 C++ 异常？）");
}

void __cdecl onPureCall()
{
    abortHook("纯虚函数调用（对象析构顺序/双重释放？）");
}

void __cdecl onInvalidParam(const wchar_t *, const wchar_t *,
                            const wchar_t *, unsigned int, uintptr_t)
{
    abortHook("CRT 非法参数");
}

void __cdecl onSigAbrt(int)
{
    abortHook("abort()（qFatal/断言）");
}

// ---------- Qt 日志文件重定向（调试基建：qWarning 不再随控制台丢失） ----------

void qtMessageLog(QtMsgType type, const QMessageLogContext &ctx,
                  const QString &msg)
{
    (void)type;
    (void)ctx;
    char path[MAX_PATH];
    if (!GetTempPathA(MAX_PATH, path))
        return;
    lstrcatA(path, "poptools_qt.log");
    HANDLE f = CreateFileA(path, FILE_APPEND_DATA, FILE_SHARE_READ, nullptr,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE)
        return;
    const QByteArray line = msg.toLocal8Bit();
    DWORD written = 0;
    WriteFile(f, line.constData(), DWORD(line.size()), &written, nullptr);
    WriteFile(f, "\r\n", 2, &written, nullptr);
    CloseHandle(f);
}

} // namespace

void CrashReporter::install()
{
    SetUnhandledExceptionFilter(unhandledFilter);
    _set_purecall_handler(onPureCall);
    _set_invalid_parameter_handler(onInvalidParam);
    std::set_terminate(onTerminate);
    signal(SIGABRT, onSigAbrt);
    // abort 的 WER 上报由我们自己的留档替代
    _set_abort_behavior(0, _CALL_REPORTFAULT);
    qInstallMessageHandler(qtMessageLog);
}

} // namespace poptools
