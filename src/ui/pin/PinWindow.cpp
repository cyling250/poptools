#include "PinWindow.h"

#include <QGuiApplication>
#include <QScreen>
#include <QPainter>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QContextMenuEvent>
#include <QMenu>
#include <QFile>
#include <QDir>
#include <QDateTime>

#include "core/settings/Settings.h"

// ============================================================
// 贴图窗口（F3）：置顶悬浮显示截图，支持拖动 / 滚轮缩放 /
// 右键清除与销毁；贴图同时落盘到默认文件夹。
// 阴影在内容下边与左边外侧手绘渐变（光从右上照射的投影效果）。
// ============================================================

namespace snipaste {

PinWindow::PinWindow(QWidget *parent)
    : QWidget(parent)
{
    // 置顶无边框工具窗口 + 透明背景（阴影区透出桌面）
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    setAttribute(Qt::WA_TranslucentBackground);
    // 支持多张贴图并存：每次 F3 创建新实例，关闭（清除/销毁/双击）即销毁
    setAttribute(Qt::WA_DeleteOnClose);
}

void PinWindow::pin(const QPixmap &pixmap, const QPoint &globalTopLeft)
{
    m_pixmap = pixmap;
    m_scale = 1.0;

    // 逻辑尺寸 = 物理像素 / 屏幕 dpr（窗口以逻辑坐标显示）
    const QScreen *scr = QGuiApplication::screenAt(globalTopLeft);
    const qreal dpr = scr ? scr->devicePixelRatio() : 1.0;
    QSize logical = (QSizeF(pixmap.size()) / dpr).toSize();
    // 初始尺寸限制在屏幕 80% 内，防止超大图越界
    const QRect avail = scr ? scr->availableGeometry() : QGuiApplication::primaryScreen()->availableGeometry();
    logical = logical.boundedTo(avail.size() * 4 / 5);
    m_baseSize = logical;

    // 张贴同时保存到默认文件夹（用户可在设置中自定义）
    saveToDefaultDir();

    // 内容左上角 = globalTopLeft；左侧阴影占 kShadow → 窗口向左偏移
    resize(QSize(m_baseSize.width() + kShadow, m_baseSize.height() + kShadow));
    move(globalTopLeft - QPoint(kShadow, 0));
    show();
    raise();
    activateWindow();
}

void PinWindow::saveToDefaultDir()
{
    m_savedPath.clear();
    const QString dir = Settings::instance().pinSaveDir();
    if (dir.trimmed().isEmpty())
        return;
    if (!QDir().mkpath(dir))
        return;
    const QString name = QStringLiteral("Pin_%1.png")
                             .arg(QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss_zzz"));
    const QString path = QDir(dir).filePath(name);
    if (m_pixmap.save(path))
        m_savedPath = path;
}

QSize PinWindow::contentSize() const
{
    return (QSizeF(m_baseSize) * m_scale).toSize();
}

void PinWindow::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    QPainter p(this);
    p.setRenderHint(QPainter::SmoothPixmapTransform); // 缩放时平滑
    const QSize content = contentSize();

    // 左侧阴影：三阶渐变，光从右上照射，左边缘最深
    QLinearGradient leftGrad(0, 0, kShadow, 0);
    leftGrad.setColorAt(0, QColor(0, 0, 0, 0));
    leftGrad.setColorAt(0.5, QColor(0, 0, 0, 60));
    leftGrad.setColorAt(1, QColor(0, 0, 0, 110));
    p.fillRect(QRect(0, 0, kShadow, content.height()), QBrush(leftGrad));

    // 底部阴影：三阶渐变，下边缘最深
    QLinearGradient bottomGrad(0, content.height(), 0, content.height() + kShadow);
    bottomGrad.setColorAt(0, QColor(0, 0, 0, 110));
    bottomGrad.setColorAt(0.5, QColor(0, 0, 0, 60));
    bottomGrad.setColorAt(1, QColor(0, 0, 0, 0));
    p.fillRect(QRect(0, content.height(), kShadow + content.width(), kShadow), QBrush(bottomGrad));

    // 贴图内容（左上角固定于窗口内 (kShadow, 0)，即传入的 globalTopLeft）
    p.drawPixmap(QRect(kShadow, 0, content.width(), content.height()), m_pixmap);
}

void PinWindow::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
    {
        m_dragging = true;
        m_dragOffset = event->globalPosition().toPoint() - frameGeometry().topLeft();
    }
}

void PinWindow::mouseMoveEvent(QMouseEvent *event)
{
    if (m_dragging && event->buttons() & Qt::LeftButton)
        move(event->globalPosition().toPoint() - m_dragOffset);
}

void PinWindow::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
        m_dragging = false;
}

void PinWindow::wheelEvent(QWheelEvent *event)
{
    // 滚轮缩放：每格 ±0.1（避免跨度太大），0.2x ~ 5x；
    // 锚点 = 内容左上角（resize 保持 topLeft，内容区域左上角固定）
    const qreal delta = (event->angleDelta().y() / 120.0) * 0.1;
    m_scale = qBound(0.2, m_scale + delta, 5.0);
    const QSize content = contentSize();
    resize(QSize(content.width() + kShadow, content.height() + kShadow));
    update();
}

void PinWindow::mouseDoubleClickEvent(QMouseEvent *event)
{
    Q_UNUSED(event)
    // 双击 = 清除（仅清除屏幕贴图，不删除文件）
    close();
}

void PinWindow::contextMenuEvent(QContextMenuEvent *event)
{
    QMenu menu(this);
    QAction *clearAct = menu.addAction("清除");   // 仅清除屏幕上的该截图
    QAction *destroyAct = menu.addAction("销毁"); // 清除屏幕和文件夹中的图
    QAction *chosen = menu.exec(event->globalPos());

    if (chosen == destroyAct && !m_savedPath.isEmpty())
        QFile::remove(m_savedPath); // 删除默认文件夹中的文件
    if (chosen == clearAct || chosen == destroyAct)
        close(); // WA_DeleteOnClose → 关闭即销毁本实例，不影响其它贴图
}

} // namespace snipaste
