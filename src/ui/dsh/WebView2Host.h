#pragma once

#include <QWidget>

#ifdef Q_OS_WIN
// WebView2 COM 头（tools/webview2/include，scripts/install_webview2.py 装入）。
// WebView2.h 只自含 rpc.h——`interface` 宏（MIDL 前置声明用）在 objbase.h 链
//（combaseapi.h）定义；WIN32_LEAN_AND_MEAN 会把 ole2/objbase 排除出 windows.h，
// 必须显式引入，否则 typedef interface X 前向声明全炸（C2146）
#include <objbase.h>
#include <WebView2.h>
#include <wrl/client.h>
#endif

namespace poptools {

// ============================================================
// WebView2Host：WebView2 宿主控件（T-074 dsh 原样移植 M1）。
//
// Chromium 子 HWND 填满本控件客户区；navigate(url) 加载 dsh webserver
//（启动 URL 含 ?token= 登录态，首访种 30 天 cookie）。
//
// 落地方式：
//   * loader 不链 .lib——WebView2Loader.dll 经 QLibrary 动态解析
//     CreateCoreWebView2EnvironmentWithOptions（免静态 CRT 匹配问题）；
//   * 首次 showEvent 触发异步初始化（COM 回调回到创建线程=Qt 主线程，
//     Qt 事件循环泵消息即够）；回调链 Environment→Controller→WebView
//     就绪后补导航 pending URL；
//   * resizeEvent 同步 put_Bounds（客户区矩形）。
// ============================================================
class WebView2Host : public QWidget
{
    Q_OBJECT
public:
    explicit WebView2Host(QWidget *parent = nullptr);
    ~WebView2Host() override;

    void setUserDataDir(const QString &dir); // 浏览器用户数据目录（须绝对路径；多进程勿共用）
    void navigate(const QString &url);       // 未就绪时暂存，ready 后补导航
    bool isReady() const { return m_ready; }

signals:
    void ready();                            // controller 就绪，可导航
    void navigationCompleted(bool ok, const QString &url);
    void initFailed(const QString &reason);  // 环境/控制器创建失败（缺运行时/loader 等）

protected:
    void showEvent(QShowEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void ensureEnvironment();
    void applyBounds();

#ifdef Q_OS_WIN
    Microsoft::WRL::ComPtr<ICoreWebView2Environment> m_environment;
    Microsoft::WRL::ComPtr<ICoreWebView2Controller> m_controller;
    Microsoft::WRL::ComPtr<ICoreWebView2> m_webView;
    EventRegistrationToken m_navToken{};
#endif
    QString m_userDataDir;
    QString m_pendingUrl;
    QString m_loaderPath;
    bool m_environmentRequested = false;
    bool m_ready = false;
};

} // namespace poptools
