#pragma once

#include <QEvent>
#include <QImage>
#include <QObject>
#include <QPointer>
#include <QRect>
#include <QString>
#include <QWidget> // QPointer<QWidget> 需完整类型

#include "DesktopBandLogic.h"

// COM 前置声明（头文件不引 windows.h/d3d11.h/dcomp.h，实现文件内包含）
struct ID3D11Device;
struct ID3D11DeviceContext;
struct IDXGIDevice;
struct IDCompositionDevice;
struct IDCompositionTarget;
struct IDCompositionVisual;
struct IDCompositionSurface;

class QTimer;

namespace poptools {

namespace desktopband {

// 带内层序（自纠时维持 盒子 > 日历 的相对次序，与 DesktopStack 对齐）
enum class BandLayer
{
    Calendar = 0,
    Box = 1
};

// ============================================================
// DesktopBandHost——桌面壁纸带（WorkerW）驻入宿主（T-048）。
//
// 职责链（顺序不可反——硬前提：先 SetParent 进带再建 DComp target，
// 反序必隐形，P-031 同款失败签名）：
//   1. band 定位：Progman → SHELLDLL_DefView → WorkerW
//      （缺失时 0x052C 幂等触发分裂后重探）
//   2. 创建带规格窗口：WS_POPUP +
//      WS_EX_NOREDIRECTIONBITMAP|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE
//   3. SetParent 进壁纸带
//   4. DComp 初始化：D3D11 → DCompositionCreateDevice →
//      CreateTargetForHwnd → Visual；帧内容 =
//      BeginDraw → QI(ID3D11Texture2D) → UpdateSubresource(QImage
//      ARGB32_Premultiplied, bytesPerLine) → EndDraw → Commit
//      （GDI GetDC 更新路径禁用——隔次静默丢帧，已实证）
//
// 交互：带内窗口收不到真实鼠标（真实点击永远落在图标层），由
// BandMouseRouter 经共享 WH_MOUSE_LL 钩子命中判定后 PostMessage 到
// 本宿主 HWND，WndProc 合成 QMouseEvent/QContextMenuEvent 派发给
// 隐藏的内容控件（交互逻辑单一来源，不复制日历/盒子交互）。
//
// 失败降级：定位/DComp 初始化失败 → activate() 返回 false，调用方
// 走既有 DesktopStack 贴底路径（功能不丢，层级形态回退）。
//
// 自纠：5s 低频 QTimer 检查 GetAncestor(GA_ROOT) 归属与带内 Z 序，
// 零漂移时零开销；Explorer 重建带销毁宿主窗时整链重建，重定位失败
// 发 bandLost 交调用方降级。
// ============================================================
class DesktopBandHost : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY(DesktopBandHost)
public:
    explicit DesktopBandHost(QObject *parent = nullptr);
    ~DesktopBandHost() override;

    // 驻入壁纸带（懒执行：首次需要显示时才探测）。已激活则幂等
    //（仅显隐与几何归位）。失败返回 false 交调用方降级。
    // T-048-FIX（P1-1）：activate 只建链不显示——初始呈现统一走
    // showBand()（内容可见性门把关），防真隐藏态激活复活已关闭窗口。
    bool activate(const QRect &logicalRect, const QString &title,
                  BandLayer layer);
    void deactivate();
    bool active() const { return m_active; }
    bool bandVisible() const { return m_active && m_bandShown; }
    void showBand();
    void hideBand();

    // 内容控件（隐藏的真窗口）：事件派发目标 + 渲染来源
    void setContent(QWidget *content) { m_content = content; }
    QWidget *content() const { return m_content.data(); }

    // 渲染内容控件 + 同步几何 + 排队上传（单事件循环至多一次 Commit）
    void presentWidget(QWidget *content);
    // 直接上传一帧（内部规范为 ARGB32_Premultiplied）
    void presentImage(const QImage &image);

    // 内容控件 → 上传用图像（grab → ARGB32_Premultiplied）
    static QImage renderContentToImage(QWidget *content);

    // ---- BandMouseRouter 访问接口（物理坐标域） ----
    void *nativeWindow() const { return m_hwnd; }
    void *bandWindow() const { return m_band; }
    const QRectF &nativeRect() const { return m_nativeRect; }
    int routeId() const { return m_routeId; }
    BandLayer layer() const { return m_layer; }

    // 原生窗口消息入口（仅 cpp 内 WndProc 调用；公开以便WndProc 访问）
    qintptr nativeMessage(int message, quintptr wParam, qintptr lParam);

signals:
    // 转发消息已派发给内容控件（参数为 desktopband::RouteMsg 枚举值）
    void interactionRouted(int routeMsg);
    // 带归属丢失且重定位失败——调用方应降级到 DesktopStack 贴底路径
    void bandLost();

private:
    friend class BandMouseRouter;
    void setRouteId(int id) { m_routeId = id; }

    void forwardMouse(int nativeMsg, quintptr wParam, qintptr lParam);
    void dispatchMouseEvent(QEvent::Type type, Qt::MouseButton button,
                            Qt::MouseButtons buttons, const QPointF &local,
                            const QPointF &globalLogical);
    void dispatchContextMenu(const QPointF &local,
                             const QPointF &globalLogical);
    // T-048-FIX（P2-2）：滚轮派发（clientPhys 为宿主客户区物理坐标，
    // delta 为 wParam 高字的原始滚轮增量，120 的倍数）
    void dispatchWheel(const QPointF &clientPhys, int delta);
    void sendLeave();

    bool createBandWindow(const QRect &logicalRect, const QString &title);
    void destroyBandWindow();
    void placeWindow(const QRect &logicalRect);
    void refreshBandOffset(); // 带客户区原点（子窗坐标换算基准）
    QRect nativeRectForLogical(const QRect &logicalRect) const;
    void updateDprForRect(const QRect &logicalRect);
    // T-048-FIX（P1-1）：showBand 的门旁路——heal 整链重建按重建前呈现态
    // 恢复显示（此时内容控件处于双表示隐藏态，内容可见性门会误拦）
    void showBandInternal();

    bool initDComp();
    void releaseDComp();
    bool ensureSurface(const QSize &physSize);
    void uploadImage(const QImage &image);
    void flushPendingImage();
    void syncGeometryFromWidget(QWidget *content);
    void checkBandHealth(); // 5s 自纠

    QPointer<QWidget> m_content;
    QString m_title;
    void *m_hwnd = nullptr;  // 带规格窗口（WS_POPUP 子窗）
    void *m_band = nullptr;  // 壁纸带 WorkerW
    QPoint m_bandOffset;     // 带客户区原点的屏幕物理坐标（子窗坐标换算）
    QRect m_logicalRect;     // 内容控件全局逻辑几何
    QRectF m_nativeRect;     // 宿主窗口屏幕物理矩形（路由命中域）
    qreal m_dpr = 1.0;
    BandLayer m_layer = BandLayer::Calendar;
    bool m_active = false;
    bool m_bandShown = false;
    int m_routeId = -1;

    // DComp/D3D 链（Release 配对见 releaseDComp，顺序 surf→visual→
    // target→dcomp→dxgi→ctx→d3d）
    ID3D11Device *m_d3d = nullptr;
    ID3D11DeviceContext *m_ctx = nullptr;
    IDXGIDevice *m_dxgi = nullptr;
    IDCompositionDevice *m_dcomp = nullptr;
    IDCompositionTarget *m_target = nullptr;
    IDCompositionVisual *m_visual = nullptr;
    IDCompositionSurface *m_surface = nullptr;
    QSize m_surfaceSize;

    QImage m_lastImage;      // 最近一帧（自纠整链重建后重传）
    UploadCoalescer m_coalescer;
    QTimer *m_healTimer = nullptr;
};

} // namespace desktopband
} // namespace poptools
