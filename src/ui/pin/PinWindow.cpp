#include "PinWindow.h"

#include <QGuiApplication>
#include <QScreen>
#include <QPainter>
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
// 投影是内容矩形整体下移后的高斯模糊轮廓（真实 drop shadow），
// 与操作系统原生窗口投影同一视觉语言，呈现"浮于屏幕之上"。
// ============================================================

namespace snipaste {

namespace {

// 一维矩形阶跃（[start, start+len) = 1）经 3 次半径 radius 的盒模糊后的剖面，
// 返回 0..256 的整数（256 = 1.0）。盒模糊可分离，矩形轮廓的二维模糊 = 水平
// 剖面 × 垂直剖面的乘积，因此只需对两条一维信号卷积，无需整幅大图多遍卷积。
QList<int> blurredStepProfile(int n, int start, int len, int radius)
{
    QList<double> s(n, 0.0), t(n);
    for (int i = start; i < qMin(start + len, n); ++i)
        s[i] = 1.0;
    const double win = radius * 2 + 1;
    for (int pass = 0; pass < 3; ++pass) {
        double sum = 0;
        for (int i = 0; i <= radius && i < n; ++i)
            sum += s[i];
        for (int i = 0; i < n; ++i) {
            t[i] = sum / win;
            const int add = i + radius + 1;
            const int sub = i - radius;
            if (add < n)
                sum += s[add];
            if (sub >= 0)
                sum -= s[sub];
        }
        s.swap(t);
    }
    QList<int> profile(n);
    for (int i = 0; i < n; ++i)
        profile[i] = qBound(0, qRound(s[i] * 256.0), 256);
    return profile;
}

} // namespace

PinWindow::PinWindow(QWidget *parent)
    : QWidget(parent)
{
    // 置顶无边框工具窗口 + 透明背景（投影区透出桌面）
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    setAttribute(Qt::WA_TranslucentBackground);
    // 支持多张贴图并存：每次 F3 创建新实例，关闭（清除/销毁/双击）即销毁
    setAttribute(Qt::WA_DeleteOnClose);
}

void PinWindow::pin(const QPixmap &pixmap, const QPoint &globalTopLeft)
{
    m_pixmap = pixmap;
    m_scale = 1.0;

    // 无损 1:1 呈现：记录张贴屏幕的 dpr，绘制时物理像素与屏幕像素一一对应；
    // 尺寸保持选区原始逻辑尺寸、位置保持选区原始位置，不做任何缩小限制
    // （超大图也原样张贴，需要缩小时可滚轮调整）
    const QScreen *scr = QGuiApplication::screenAt(globalTopLeft);
    const qreal dpr = scr ? scr->devicePixelRatio() : 1.0;
    m_pixmap.setDevicePixelRatio(dpr);
    m_baseSize = (QSizeF(pixmap.size()) / dpr).toSize();

    remakeShadow();
    saveToDefaultDir(); // 张贴同时保存到默认文件夹（PNG 无损）

    // 内容左上角 = globalTopLeft；窗口四周是容纳投影衰减的透明边距
    resize(contentSize() + QSize(kMargin * 2, kMargin * 2));
    move(globalTopLeft - QPoint(kMargin, kMargin));
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
    if (m_pixmap.save(path)) // PNG：无损落盘
        m_savedPath = path;
}

QSize PinWindow::contentSize() const
{
    return (QSizeF(m_baseSize) * m_scale).toSize();
}

void PinWindow::remakeShadow()
{
    m_shadow = makeShadow(contentSize());
}

QImage PinWindow::makeShadow(const QSize &content) const
{
    const int w = content.width() + kMargin * 2;
    const int h = content.height() + kMargin * 2;
    // 轮廓 = 内容矩形整体下移（模拟上方光源；水平方向不偏移）
    const QList<int> px = blurredStepProfile(w, kMargin, content.width(), kShadowBlur);
    const QList<int> py =
        blurredStepProfile(h, kMargin + kShadowOffsetY, content.height(), kShadowBlur);

    QImage img(w, h, QImage::Format_Alpha8);
    for (int y = 0; y < h; ++y) {
        uchar *line = img.scanLine(y);
        const int ky = py[y];
        for (int x = 0; x < w; ++x)
            line[x] = uchar((kShadowAlpha * px[x] * ky) >> 16);
    }
    return img;
}

void PinWindow::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    QPainter p(this);

    // 投影：预生成的高斯模糊轮廓（窗口大小），Alpha8 图绘制即为黑色半透明
    if (!m_shadow.isNull())
        p.drawImage(0, 0, m_shadow);

    // 贴图内容：左上角固定于窗口内 (kMargin, kMargin) = pin() 传入的全局位置
    if (m_scale == 1.0) {
        // dpr 已记录在 pixmap 上 → 物理像素 1:1 blit，画面无损
        p.drawPixmap(kMargin, kMargin, m_pixmap);
    } else {
        p.setRenderHint(QPainter::SmoothPixmapTransform); // 缩放时平滑
        p.drawPixmap(QRect(kMargin, kMargin, contentSize().width(), contentSize().height()),
                     m_pixmap);
    }
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
    const QSize target = contentSize() + QSize(kMargin * 2, kMargin * 2);
    if (target != size()) {
        resize(target);
        remakeShadow(); // 内容尺寸变了 → 重算投影轮廓
    }
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
