#pragma once

#include <QWidget>
#include <QPixmap>
#include <QPoint>
#include <QSize>
#include <QString>

namespace snipaste {

// ============================================================
// 贴图窗口（F3）
//
// 将截图（带批注）以置顶悬浮窗贴到屏幕最前方：
//   * 初始位置 = 截图选区所在位置（全局逻辑坐标）
//   * 张贴同时保存到设置中的默认文件夹（用户可自定义）
//   * 按住拖动移动位置
//   * 鼠标滚轮缩放（0.2x ~ 5x，锚点 = 内容左上角）
//   * 右键菜单：清除（仅清除屏幕贴图）/ 销毁（清除屏幕 + 删除文件）
//   * 内容下边与左边外侧手绘渐变阴影，实现"浮在屏幕上"的视觉效果
// ============================================================
class PinWindow : public QWidget
{
    Q_OBJECT
public:
    explicit PinWindow(QWidget *parent = nullptr);

    // 将截图贴到屏幕上：保存到默认文件夹 + 按 globalTopLeft 定位（内容左上角）+ 置顶显示
    void pin(const QPixmap &pixmap, const QPoint &globalTopLeft);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;

private:
    void saveToDefaultDir(); // 张贴同时保存到默认文件夹
    QSize contentSize() const; // 内容显示尺寸（baseSize × scale）

    QPixmap m_pixmap;      // 贴图内容（物理分辨率）
    QSize m_baseSize;      // 贴图原始逻辑尺寸（缩放基准）
    QPoint m_dragOffset;   // 拖动时鼠标相对窗口左上角偏移
    bool m_dragging = false;
    qreal m_scale = 1.0;   // 缩放比例
    QString m_savedPath;   // 本次贴图保存的文件路径（销毁时删除）

    static constexpr int kShadow = 14; // 下边/左边外侧阴影宽度（像素）
};

} // namespace snipaste
