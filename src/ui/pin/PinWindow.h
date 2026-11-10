#pragma once

#include <QWidget>
#include <QPixmap>
#include <QImage>
#include <QPoint>
#include <QSharedPointer>
#include <QSize>
#include <QString>
#include <QTimer>

#include <atomic>

namespace poptools {

// ============================================================
// 贴图窗口（F3）
//
// 将截图（带批注）以置顶悬浮窗贴到屏幕最前方：
//   * 无损 1:1 呈现：内容按屏幕 dpr 精确映射，物理像素与屏幕像素一一对应；
//     初始位置 = 截图选区位置、尺寸 = 选区尺寸，与原画面严丝合缝
//   * 张贴同时保存到设置中的默认文件夹（PNG 无损）
//   * 按住拖动移动位置
    //   * 鼠标滚轮缩放（0.2x ~ 5x，锚点 = 鼠标位置：光标下的内容点缩放前后不动）
    //   * 右键菜单：清除（仅清除屏幕贴图）/ 销毁（清除屏幕 + 删除文件）
    //   * 投影 = 内容矩形整体下移后的高斯模糊轮廓（真实 drop shadow，
    //     与操作系统原生窗口投影同源的视觉语言），呈现"浮于屏幕之上"
// ============================================================
class PinWindow : public QWidget
{
    Q_OBJECT
public:
    explicit PinWindow(QWidget *parent = nullptr);

    // 将截图贴到屏幕上：保存到默认文件夹 + 按 globalTopLeft 定位（内容左上角）+ 置顶显示。
    // sourceDpr：内容源屏的 devicePixelRatio（截图链路可提供；纯剪贴板图源屏未知传 0，
    // 维持旧行为按张贴屏 dpr 反推逻辑尺寸）
    void pin(const QPixmap &pixmap, const QPoint &globalTopLeft, qreal sourceDpr = 0.0);

signals:
    // 右键菜单「识别文字 / 翻译」：携带贴图内容（物理分辨率），
    // 由 PinModule 经共享 OCR 服务与翻译服务处理（结果走托盘气泡 + 剪贴板）
    void ocrRequested(const QPixmap &pixmap);
    void translateRequested(const QPixmap &pixmap);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;

private:
    void saveToDefaultDir();          // 张贴同时保存到默认文件夹
    QSize contentSize() const;        // 内容显示尺寸（baseSize × scale）
    void remakeShadow();              // 按当前内容尺寸重算投影轮廓
    QImage makeShadow(const QSize &content) const; // 生成内容矩形的高斯模糊投影

    QPixmap m_pixmap;      // 贴图内容（物理分辨率，dpr = 内容源屏 dpr，未知时为张贴屏 dpr）
    QSize m_baseSize;      // 贴图原始逻辑尺寸（缩放基准）
    QPoint m_dragOffset;   // 拖动时鼠标相对窗口左上角偏移
    bool m_dragging = false;
    qreal m_scale = 1.0;   // 缩放比例
    QString m_savedPath;   // 本次贴图保存的文件路径（落盘前预登记，销毁时删除）
    QSharedPointer<std::atomic_bool> m_saveAborted; // 落盘中止标记：销毁先于落盘完成时
                                                    // 置位，worker 落盘后补删文件防复活
    QImage m_shadow;       // 预生成的投影轮廓（Format_Alpha8，窗口大小）
    QTimer m_shadowDebounce; // 投影重算防抖：滚轮连滚只在停歇 50ms 后重算轮廓

    // 投影参数（逻辑像素），量级对齐操作系统原生窗口投影
    static constexpr int kShadowBlur = 12;    // 盒模糊半径 ≈ 高斯 σ
    static constexpr int kShadowOffsetY = 6;  // 投影整体下移（模拟上方光源）
    static constexpr int kShadowAlpha = 102;  // 投影峰值不透明度 ≈ 40%
    // 窗口四周透明边距：容纳投影向外衰减（约 2σ + 偏移），不留硬边
    static constexpr int kMargin = kShadowBlur * 2 + kShadowOffsetY + 2;
};

} // namespace poptools
