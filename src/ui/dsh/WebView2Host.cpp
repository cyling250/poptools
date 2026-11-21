#include "WebView2Host.h"

#ifdef Q_OS_WIN

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLibrary>
#include <QResizeEvent>
#include <QShowEvent>
#include <QStandardPaths>
#include <QTime>

#include <wrl/event.h>

#include <windows.h>
#endif // Q_OS_WIN（includes 区）

namespace poptools {

namespace {

// loader 解析顺序：exe 同目录（正式部署形态）→ 源码树 tools/webview2/x64
//（dsh_poc 从 build/Release 跑时的开发形态）。QLibrary 动态加载，
// 免 WebView2LoaderStatic.lib 的 CRT 匹配问题
QString resolveLoaderPath()
{
    const QString exeDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        exeDir + "/WebView2Loader.dll",
        QDir::cleanPath(exeDir + "/../../../tools/webview2/x64/WebView2Loader.dll"),
        QDir::cleanPath(exeDir + "/../../tools/webview2/x64/WebView2Loader.dll"),
    };
    for (const QString &path : candidates)
        if (QFileInfo::exists(path))
            return path;
    return QString();
}

using CreateEnvironmentWithOptionsFn = HRESULT (__stdcall *)(
    PCWSTR browserExecutableFolder, PCWSTR userDataFolder,
    ICoreWebView2EnvironmentOptions *environmentOptions,
    ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler *environmentCreatedHandler);

// 嵌入层调试日志（黑框排查期暂留；稳定后撤）：%LOCALAPPDATA%/PopTools/
// webview2_debug.log——GUI 程序的失败原因无法远程读取，靠文件留痕
void hostLog(const QString &line)
{
    // 嵌入层调试日志：黑框排查期曾落盘 webview2_debug.log，稳定后撤离（T-074
    // M3）——保留函数签名免动全部调用点，实现为 no-op
    Q_UNUSED(line);
}

} // namespace

WebView2Host::WebView2Host(QWidget *parent)
    : QWidget(parent)
{
    // 不预置 WA_NativeWindow：控件构造期顶层窗尚未 native 化，提前落子
    // HWND 会挂在不存在的父链上（WA_DontCreateNativeAncestors 更是把祖先
    // 一并跳过——实测黑框根因）。winId() 在 ensureEnvironment 里按需创建
    // 子 HWND 并由 Qt 正确落父链
}

WebView2Host::~WebView2Host()
{
#ifdef Q_OS_WIN
    if (m_webView) // 事件挂在 WebView 上（controller 无该注册口）
        m_webView->remove_NavigationCompleted(m_navToken);
    if (m_controller)
        m_controller->Close();
#endif
}

void WebView2Host::setUserDataDir(const QString &dir)
{
    m_userDataDir = dir;
}

void WebView2Host::navigate(const QString &url)
{
#ifdef Q_OS_WIN
    if (m_ready && m_webView) {
        hostLog(QStringLiteral("Navigate %1").arg(url));
        m_webView->Navigate(reinterpret_cast<LPCWSTR>(url.utf16()));
        return;
    }
#endif
    hostLog(QStringLiteral("navigate 暂存（未就绪）：%1").arg(url));
    m_pendingUrl = url; // 未就绪：ready 后补导航
}

void WebView2Host::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    ensureEnvironment(); // 兜底：未被显式 initialize 过则在此触发
    applyBounds();       // 显示时刷新边界：隐藏期布局未激活，创建期矩形可能为 0
                         //（黑框第二根因：put_Bounds 只在创建回调里做过一次）
}

void WebView2Host::initialize()
{
    ensureEnvironment(); // 常驻窗预热入口：构造后即可调（winId 按需落 HWND）
}

void WebView2Host::ensureEnvironment()
{
#ifdef Q_OS_WIN
    if (m_environmentRequested)
        return;
    m_environmentRequested = true;

    m_loaderPath = resolveLoaderPath();
    if (m_loaderPath.isEmpty()) {
        emit initFailed(QStringLiteral("WebView2Loader.dll 未找到：请运行 scripts/install_webview2.py"));
        return;
    }
    QLibrary loader(m_loaderPath);
    if (!loader.load()) {
        emit initFailed(QStringLiteral("WebView2Loader.dll 加载失败：%1").arg(loader.errorString()));
        return;
    }
    const auto createEnvironment = reinterpret_cast<CreateEnvironmentWithOptionsFn>(
        loader.resolve("CreateCoreWebView2EnvironmentWithOptions"));
    if (!createEnvironment) {
        emit initFailed(QStringLiteral("WebView2Loader.dll 缺少 CreateCoreWebView2EnvironmentWithOptions 导出"));
        return;
    }

    const QString userDataDir = m_userDataDir.isEmpty()
        ? QCoreApplication::applicationDirPath() + "/webview2-data"
        : m_userDataDir;
    hostLog(QStringLiteral("env 请求：userDataDir=%1").arg(userDataDir));
    const HRESULT hr = createEnvironment(
        nullptr, // browserExecutableFolder=nullptr → 用系统安装的 WebView2 运行时
        reinterpret_cast<LPCWSTR>(userDataDir.utf16()),
        nullptr, // 默认选项
        Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [this](HRESULT error, ICoreWebView2Environment *environment) -> HRESULT {
                if (FAILED(error) || !environment) {
                    hostLog(QStringLiteral("env 失败 hr=0x%1")
                                .arg(QString::number(static_cast<ulong>(error), 16)));
                    emit initFailed(QStringLiteral("WebView2 环境创建失败（hr=0x%1）——"
                                                   "请确认系统已安装 WebView2 运行时")
                                        .arg(QString::number(static_cast<ulong>(error), 16)));
                    return S_OK;
                }
                m_environment = environment;
                const HWND hwnd = reinterpret_cast<HWND>(winId());
                hostLog(QStringLiteral("env 就绪，controller 请求 hwnd=%1")
                            .arg(QString::number(reinterpret_cast<quintptr>(hwnd))));
                m_environment->CreateCoreWebView2Controller(
                    hwnd,
                    Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [this](HRESULT error, ICoreWebView2Controller *controller) -> HRESULT {
                            if (FAILED(error) || !controller) {
                                hostLog(QStringLiteral("controller 失败 hr=0x%1")
                                            .arg(QString::number(static_cast<ulong>(error), 16)));
                                emit initFailed(QStringLiteral("WebView2 控制器创建失败（hr=0x%1）")
                                                    .arg(QString::number(static_cast<ulong>(error), 16)));
                                return S_OK;
                            }
                            m_controller = controller;
                            controller->put_IsVisible(TRUE); // 默认未必可见——不显式开=黑
                            controller->get_CoreWebView2(&m_webView);
                            m_webView->add_NavigationCompleted(
                                Microsoft::WRL::Callback<ICoreWebView2NavigationCompletedEventHandler>(
                                    [this](ICoreWebView2 *sender,
                                           ICoreWebView2NavigationCompletedEventArgs *args) -> HRESULT {
                                        BOOL success = FALSE;
                                        args->get_IsSuccess(&success);
                                        LPWSTR uri = nullptr;
                                        sender->get_Source(&uri); // 当前文档 URI（Source 属性）
                                        const QString url = uri ? QString::fromWCharArray(uri) : QString();
                                        if (uri)
                                            CoTaskMemFree(uri);
                                        hostLog(QStringLiteral("导航%1：%2")
                                                    .arg(success == TRUE ? "完成" : "失败", url));
                                        emit navigationCompleted(success == TRUE, url);
                                        return S_OK;
                                    })
                                    .Get(),
                                &m_navToken);
                            m_ready = true;
                            applyBounds();
                            hostLog("controller 就绪");
                            emit ready();
                            if (!m_pendingUrl.isEmpty()) {
                                navigate(m_pendingUrl);
                                m_pendingUrl.clear();
                            }
                            return S_OK;
                        })
                        .Get());
                return S_OK;
            })
            .Get());
    hostLog(QStringLiteral("env 请求返回 hr=0x%1")
                .arg(QString::number(static_cast<ulong>(hr), 16)));
    // 创建请求本身的立即失败（如参数错误）；运行时缺失走 Completed 回调分支
    if (FAILED(hr))
        emit initFailed(QStringLiteral("WebView2 环境请求失败（hr=0x%1）")
                            .arg(QString::number(static_cast<ulong>(hr), 16)));
#endif
}

void WebView2Host::applyBounds()
{
#ifdef Q_OS_WIN
    if (!m_controller)
        return;
    const HWND hwnd = reinterpret_cast<HWND>(winId());
    RECT rc{};
    if (GetClientRect(hwnd, &rc)) {
        m_controller->put_Bounds(rc);
        hostLog(QStringLiteral("bounds：rc=%1×%2 widget=%3×%4 parent=%5×%6 top=%7×%8 visible=%9")
                    .arg(rc.right - rc.left)
                    .arg(rc.bottom - rc.top)
                    .arg(width()).arg(height())
                    .arg(parentWidget() ? parentWidget()->width() : -1)
                    .arg(parentWidget() ? parentWidget()->height() : -1)
                    .arg(window()->width()).arg(window()->height())
                    .arg(IsWindowVisible(hwnd) ? "yes" : "no"));
    }
#endif
}

void WebView2Host::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    applyBounds();
}

} // namespace poptools
