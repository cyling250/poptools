#include "WebView2Host.h"

#ifdef Q_OS_WIN

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QLibrary>
#include <QResizeEvent>
#include <QShowEvent>

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

} // namespace

WebView2Host::WebView2Host(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_NativeWindow); // 提前落 HWND，供 controller 绑定
    setAttribute(Qt::WA_DontCreateNativeAncestors);
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
        m_webView->Navigate(reinterpret_cast<LPCWSTR>(url.utf16()));
        return;
    }
#endif
    m_pendingUrl = url; // 未就绪：ready 后补导航
}

void WebView2Host::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    ensureEnvironment(); // 首次可见时初始化（winId 与消息泵此时可用）
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
    const HRESULT hr = createEnvironment(
        nullptr, // browserExecutableFolder=nullptr → 用系统安装的 WebView2 运行时
        reinterpret_cast<LPCWSTR>(userDataDir.utf16()),
        nullptr, // 默认选项
        Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [this](HRESULT error, ICoreWebView2Environment *environment) -> HRESULT {
                if (FAILED(error) || !environment) {
                    emit initFailed(QStringLiteral("WebView2 环境创建失败（hr=0x%1）——"
                                                   "请确认系统已安装 WebView2 运行时")
                                        .arg(QString::number(static_cast<ulong>(error), 16)));
                    return S_OK;
                }
                m_environment = environment;
                const HWND hwnd = reinterpret_cast<HWND>(winId());
                m_environment->CreateCoreWebView2Controller(
                    hwnd,
                    Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [this](HRESULT error, ICoreWebView2Controller *controller) -> HRESULT {
                            if (FAILED(error) || !controller) {
                                emit initFailed(QStringLiteral("WebView2 控制器创建失败（hr=0x%1）")
                                                    .arg(QString::number(static_cast<ulong>(error), 16)));
                                return S_OK;
                            }
                            m_controller = controller;
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
                                        emit navigationCompleted(success == TRUE, url);
                                        return S_OK;
                                    })
                                    .Get(),
                                &m_navToken);
                            m_ready = true;
                            applyBounds();
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
    if (GetClientRect(hwnd, &rc))
        m_controller->put_Bounds(rc);
#endif
}

void WebView2Host::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    applyBounds();
}

} // namespace poptools
