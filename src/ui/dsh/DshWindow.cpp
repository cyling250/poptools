#include "DshWindow.h"

#include <QCloseEvent>
#include <QHideEvent>
#include <QStackedWidget>
#include <QVBoxLayout>

#include "core/settings/Settings.h"

namespace poptools {

DshWindow::DshWindow(QWidget *parent)
    : QWidget(parent)
{
    setWindowTitle(QStringLiteral("Goody 管家"));
    resize(1080, 720);
    setMinimumSize(720, 520);
    // 几何记忆（dsh/windowGeometry）：无记忆 → 默认尺寸
    restoreGeometry(Settings::instance().dshWindowGeometry());

    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    // 堆叠页：web（dsh 原版界面）/ 状态层（启动中/失败）。状态页整页覆盖，
    // 不与 web 同布局挤压（ squeezed 底条既难看清也让 web 区永远黑着）
    auto *stack = new QStackedWidget(this);
    m_web = new WebView2Host(stack);
    stack->addWidget(m_web);
    m_status = new QLabel(stack);
    m_status->setObjectName("dshStatus");
    m_status->setAlignment(Qt::AlignCenter);
    m_status->setWordWrap(true);
    m_status->setStyleSheet(QStringLiteral(
        "QLabel#dshStatus { background: palette(window); color: palette(windowText);"
        "  font-size: 14px; padding: 24px; }"));
    rebuildStatus();
    stack->addWidget(m_status);
    lay->addWidget(stack, 1);
    stack->setCurrentWidget(m_status); // 启动期先显状态页

    connect(&m_runtime, &DshRuntime::webUrlReady, this, [this, stack](const QString &url) {
        m_web->navigate(url);
        stack->setCurrentWidget(m_web);
        emit workingChanged(false);
    });
    // WebView2 嵌入层初始化失败（loader 缺失/运行时缺失/控制器失败）直接进
    // 状态页——不接通的后果是整块黑屏无解释（2026-11-21 黑框排查教训）
    connect(m_web, &WebView2Host::initFailed, this, [this, stack](const QString &reason) {
        m_lastFailure = reason;
        rebuildStatus();
        stack->setCurrentWidget(m_status);
        emit workingChanged(false);
    });
    connect(&m_runtime, &DshRuntime::failed, this, [this, stack](const QString &reason) {
        m_lastFailure = reason;
        rebuildStatus();
        stack->setCurrentWidget(m_status);
        emit workingChanged(false);
    });
    connect(&m_runtime, &DshRuntime::stateChanged, this,
            [this, stack](DshRuntime::State state) {
                if (state == DshRuntime::State::Starting) {
                    rebuildStatus();
                    stack->setCurrentWidget(m_status);
                    emit workingChanged(true);
                }
            });
}

void DshWindow::setStatusVisible(bool visible)
{
    // 状态层切换走 QStackedWidget 当前页（openWindow 失败态复显用）
    if (QWidget *stack = m_status->parentWidget()) {
        auto *sw = qobject_cast<QStackedWidget *>(stack);
        if (sw)
            sw->setCurrentWidget(visible ? static_cast<QWidget *>(m_status)
                                         : static_cast<QWidget *>(m_web));
    }
}

void DshWindow::configure(const QString &nodeExe, const QString &dshBinJs, const QString &dshHome,
                          const QString &agentsHome, const QString &apiKey, const QString &userDataDir)
{
    m_runtime.configure(nodeExe, dshBinJs, dshHome, agentsHome, apiKey);
    if (!userDataDir.isEmpty())
        m_web->setUserDataDir(userDataDir);
    m_runtime.start(); // PopTools 存续期常驻：模块 init 即拉起（窗口可不可见）
    // 注意：这里刻意不预初始化 WebView2——隐藏期 winId() 预建的原生窗不会
    // 被 Qt 同步几何/可见性（实测 rc 恒 100×30 + WS_VISIBLE 未设 → 黑框）。
    // 原生工作全部推迟到首次显示（openWindow/showEvent），标准路径
}

void DshWindow::openWindow()
{
    if (m_runtime.state() == DshRuntime::State::Failed && !m_lastFailure.isEmpty()) {
        rebuildStatus();
        setStatusVisible(true);
    }
    show();
    raise();
    activateWindow();
    // 首次显示后才建 controller（环境/控制器在可见 HWND 上创建——回调里的
    // put_Bounds/IsVisible 立即有效）；后续打开走 showEvent 兜底，秒开
    m_web->initialize();
}

void DshWindow::rebuildStatus()
{
    if (!m_status)
        return;
    if (m_lastFailure.isEmpty())
        m_status->setText(QStringLiteral("dsh 后台服务启动中…（首次启动需数秒）"));
    else
        m_status->setText(QStringLiteral("dsh 后台服务失败：%1\n（修复 tools/node、tools/dsh 后重启 PopTools；"
                                         "详情见 PoC 日志或 sidecar 输出）")
                              .arg(m_lastFailure));
}

void DshWindow::closeEvent(QCloseEvent *event)
{
    // 关闭=隐藏回后台：sidecar 与在途回合不随窗口生死（用户拍板的常驻语义）
    event->ignore();
    hide();
}

void DshWindow::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
    Settings::instance().setDshWindowGeometry(saveGeometry());
    emit windowHidden();
}

} // namespace poptools
