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
#include <QThreadPool>

#include "core/settings/Settings.h"

// ============================================================
// 贴图窗口（F3）：置顶悬浮显示截图，支持拖动 / 滚轮缩放 /
// 右键清除与销毁；贴图同时落盘到默认文件夹。
// 投影是内容矩形整体下移后的高斯模糊轮廓（真实 drop shadow），
// 与操作系统原生窗口投影同一视觉语言，呈现"浮于屏幕之上"。
// ============================================================

namespace poptools {

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

    // 投影重算防抖：滚轮缩放每格都要 resize 顶层窗口（DWM 重分配），投影轮廓
    // 再逐格全量重算会让连滚掉帧；50ms 停歇后重算（绘制暂用旧轮廓，视觉无感）
    m_shadowDebounce.setSingleShot(true);
    m_shadowDebounce.setInterval(50);
    connect(&m_shadowDebounce, &QTimer::timeout, this, [this]
    {
        remakeShadow();
        update();
    });
}

void PinWindow::pin(const QPixmap &pixmap, const QPoint &globalTopLeft, qreal sourceDpr)
{
    m_pixmap = pixmap;
    m_scale = 1.0;

    // 无损 1:1 呈现：记录内容源屏的 dpr，绘制时物理像素与屏幕像素一一对应；
    // 尺寸保持选区原始逻辑尺寸、位置保持选区原始位置，不做任何缩小限制
    // （超大图也原样张贴，需要缩小时可滚轮调整）
    const QScreen *scr = QGuiApplication::screenAt(globalTopLeft);
    const qreal dpr = scr ? scr->devicePixelRatio() : 1.0;
    // 跨屏张贴（R3 #2）：剪贴板位图无 DPR 元数据，内容是源屏物理像素。源屏 dpr
    // 已知（截图链路传入）时按源屏 dpr 反推逻辑尺寸 = 原选区逻辑尺寸；未知
    // （纯剪贴板图）维持旧行为按张贴屏 dpr 反推。
    // TODO(真机验证)：混合 DPI 多屏张贴的实际观感未真机复核
    const qreal contentDpr = sourceDpr > 0.0 ? sourceDpr : dpr;
    m_pixmap.setDevicePixelRatio(contentDpr);
    m_baseSize = (QSizeF(pixmap.size()) / contentDpr).toSize();

    remakeShadow();
    saveToDefaultDir(); // 张贴同时保存到默认文件夹（PNG 无损，异步落盘）

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
    const QString name = QStringLiteral("Pin_%1.png")
                             .arg(QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss_zzz"));
    // 路径在主线程预登记（销毁菜单即刻可删；落盘失败时 remove 对不存在文件无害）
    m_savedPath = QDir(dir).filePath(name);

    // PNG 编码 + 落盘挪线程池（红线 1：大图编码与磁盘 IO 禁入主线程，整幅选区
    // PNG 编码可秒级阻塞）。QPixmap 禁止跨线程，先在主线程转 QImage（一次像素拷贝）；
    // lambda 只捕值与共享标记、不回投本窗口，窗口先销毁（WA_DeleteOnClose）也无悬垂
    const QImage img = m_pixmap.toImage();
    if (img.isNull())
        return;
    auto aborted = QSharedPointer<std::atomic_bool>::create(false);
    m_saveAborted = aborted;
    QThreadPool::globalInstance()->start([img, dir, path = m_savedPath, aborted] {
        QDir().mkpath(dir);
        if (!img.save(path, "PNG"))
            QFile::remove(path); // 落盘失败：清掉半截文件
        // 销毁先于落盘完成：删掉刚写出的文件，防"销毁后文件复活"
        if (aborted->load())
            QFile::remove(path);
    });
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
        for (int x = 0; x < w; ++x) {
            // px/ky ∈ [0,256]（256=1.0），256*256=2^16，右移 16 位即把
            // (alpha*px*ky)/65536 归一化到 [0,alpha]
            line[x] = uchar((kShadowAlpha * px[x] * ky) >> 16);
        }
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
    // 锚点 = 鼠标位置：光标下的内容点缩放前后保持不动（符合"对准哪里放大哪里"直觉）
    // cursor = 光标在内容区内的偏移（旧比例）；下方新左上角按 (m_scale/oldScale)
    // 缩放该偏移，即"内容点随比例缩放后仍落在光标下"
    const QPointF cursor = event->globalPosition().toPoint()
                           - (frameGeometry().topLeft() + QPoint(kMargin, kMargin));
    const qreal oldScale = m_scale;
    const qreal delta = (event->angleDelta().y() / 120.0) * 0.1;
    m_scale = qBound(0.2, m_scale + delta, 5.0);
    if (qFuzzyCompare(m_scale, oldScale))
        return; // 已到缩放边界

    // 新内容左上角 = 光标 - 光标下内容点偏移 × 新旧比例（内容点随比例缩放）
    const QPointF newContentTopLeft =
        event->globalPosition().toPoint() - cursor * (m_scale / oldScale);
    const QSize target = contentSize() + QSize(kMargin * 2, kMargin * 2);
    if (target != size())
        resize(target);
    move(newContentTopLeft.toPoint() - QPoint(kMargin, kMargin));
    m_shadowDebounce.start(); // 投影轮廓停歇后重算（防抖）
    update();
}

void PinWindow::mouseDoubleClickEvent(QMouseEvent *event)
{
    // T-062 修复: 此前未检查按键，右键的第二击也会关窗；右键语义是菜单。
    // 与 mousePressEvent 只认左键的口径对齐
    if (event->button() != Qt::LeftButton)
        return;
    // 双击 = 清除（仅清除屏幕贴图，不删除文件）
    close();
}

void PinWindow::contextMenuEvent(QContextMenuEvent *event)
{
    QMenu menu(this);
    // OCR/翻译：贴图内容直接识别（结果经 PinModule 走托盘气泡 + 剪贴板）
    QAction *ocrAct = menu.addAction(QStringLiteral("识别文字"));
    QAction *translateAct = menu.addAction(QStringLiteral("翻译"));
    if (m_pixmap.isNull()) { // 无内容（理论不可达）时禁用
        ocrAct->setEnabled(false);
        translateAct->setEnabled(false);
    }
    menu.addSeparator();
    QAction *clearAct = menu.addAction("清除");   // 仅清除屏幕上的该截图
    QAction *destroyAct = menu.addAction("销毁"); // 清除屏幕和文件夹中的图
    QAction *chosen = menu.exec(event->globalPos());
    if (!chosen)
        return;

    if (chosen == ocrAct) {
        emit ocrRequested(m_pixmap);
        return;
    }
    if (chosen == translateAct) {
        emit translateRequested(m_pixmap);
        return;
    }
    if (chosen == destroyAct && !m_savedPath.isEmpty()) {
        if (m_saveAborted)
            m_saveAborted->store(true); // 落盘仍在进行：worker 完成后补删，防文件复活
        QFile::remove(m_savedPath); // 删除默认文件夹中的文件
    }
    if (chosen == clearAct || chosen == destroyAct)
        close(); // WA_DeleteOnClose → 关闭即销毁本实例，不影响其它贴图
}

} // namespace poptools
