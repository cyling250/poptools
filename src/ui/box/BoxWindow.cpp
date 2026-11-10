#include "BoxWindow.h"

#include <QContextMenuEvent>
#include <QDesktopServices>
#include <QDir>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFileSystemWatcher>
#include <QGuiApplication>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>

#include "core/box/BoxStore.h"
#include "core/box/IconExtractor.h"
#include "ui/shared/DesktopStack.h"

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace poptools {

namespace {
constexpr int kPad = 10;          // 卡片内边距
constexpr int kTitleHeight = 32;  // 标题栏高
constexpr int kCellW = 55;        // 单格宽（列数 × kCellW + 2×kPad = 卡宽）
constexpr int kCellH = 68;        // 单元高（图标 38 + 标签）
constexpr int kColsMax = 8;       // 列数上限
constexpr int kRowsMax = 8;       // 行数上限
constexpr int kRadius = 12;
} // namespace

BoxWindow::BoxWindow(BoxStore *store, const QString &boxId, QWidget *parent)
    : QWidget(parent)
    , m_store(store)
    , m_icons(&IconExtractor::instance())
    , m_boxId(boxId)
{
    // 与日历同款窗口纪律：无框透明 + Tool（不进 Alt-Tab）+ 显示不激活；
    // 不带置顶（贴桌 = Z 序经 DesktopStack 钉在日历层之上、普通窗口之下）
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setMouseTracking(true);
    setAcceptDrops(true);

    // 目录内容变化自动刷新
    m_watcher = new QFileSystemWatcher(this);
    connect(m_watcher, &QFileSystemWatcher::directoryChanged, this,
            [this] { refresh(); });

    // 仓库变化：盒子被删 → 关窗；名称/行列/折叠外改 → 同步
    connect(m_store, &BoxStore::changed, this, [this] {
        bool alive = false;
        for (const DesktopBox &box : m_store->boxes())
            if (box.id == m_boxId)
                alive = true;
        if (!alive) {
            close(); // 盒子已从仓库删除
            return;
        }
        syncMeta();
    });

    refresh();
}

// ---------- 几何 ----------

int BoxWindow::cardWidth() const
{
    return 2 * kPad + m_cols * kCellW;
}

QSize BoxWindow::windowSize() const
{
    if (m_collapsed)
        return QSize(cardWidth(), kPad + kTitleHeight + kPad);
    return QSize(cardWidth(), kPad + kTitleHeight + m_rows * kCellH + kPad);
}

void BoxWindow::applyGeometry()
{
    setFixedSize(windowSize());
    update();
}

// 重读目录并自适应。目录缺失时显示引导态。
void BoxWindow::refresh()
{
    // 从仓库取当前元数据（id 失效则关窗）
    const QList<DesktopBox> boxes = m_store->boxes();
    const DesktopBox *box = nullptr;
    for (const DesktopBox &b : boxes)
        if (b.id == m_boxId)
            box = &b;
    if (!box) {
        close();
        return;
    }
    m_name = box->name;
    m_dir = box->dir;
    m_cols = box->cols;
    m_rows = box->rows;
    m_collapsed = box->collapsed;
    setWindowTitle(m_name);

    const QStringList oldWatched = m_watcher->directories();
    if (!oldWatched.isEmpty())
        m_watcher->removePaths(oldWatched);
    if (QFileInfo::exists(m_dir))
        m_watcher->addPath(m_dir);

    m_items.clear();
    m_totalCount = 0;
    m_hoverItem = -1;
    if (QFileInfo::exists(m_dir)) {
        const QFileInfoList entries =
            QDir(m_dir).entryInfoList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot,
                                      QDir::DirsFirst | QDir::Name);
        m_totalCount = entries.size();
        m_items = entries.mid(0, m_rows * m_cols); // 几行几列的容量截断
    }

    applyGeometry();
}

// 仓库外改（含本窗口 setBoxXxx 触发的 changed）：同步元数据
void BoxWindow::syncMeta()
{
    const QList<DesktopBox> boxes = m_store->boxes();
    for (const DesktopBox &box : boxes) {
        if (box.id != m_boxId)
            continue;
        const bool geomChanged = box.cols != m_cols || box.rows != m_rows
                                 || box.collapsed != m_collapsed;
        m_name = box.name;
        m_cols = box.cols;
        m_rows = box.rows;
        m_collapsed = box.collapsed;
        setWindowTitle(m_name);
        if (geomChanged)
            applyGeometry();
        else
            update();
        return;
    }
}

QRect BoxWindow::titleRect() const
{
    return QRect(kPad, kPad, width() - 2 * kPad, kTitleHeight);
}

// 标题文字区（双击改名的判定范围）：名称绘制区域，避开右侧按钮
QRect BoxWindow::nameTextRect() const
{
    return titleRect().adjusted(4, 0, -66, 0);
}

QRect BoxWindow::gearRect() const
{
    return QRect(width() - kPad - 20, kPad + 5, 20, 22);
}

QRect BoxWindow::collapseRect() const
{
    return QRect(width() - kPad - 44, kPad + 5, 20, 22);
}

QList<QRect> BoxWindow::itemRects() const
{
    QList<QRect> rects;
    const int gridTop = kPad + kTitleHeight;
    for (int i = 0; i < m_items.size(); ++i) {
        const int r = i / m_cols, c = i % m_cols;
        rects << QRect(kPad + c * kCellW, gridTop + r * kCellH, kCellW, kCellH);
    }
    return rects;
}

int BoxWindow::hitItem(const QPoint &localPos) const
{
    const int gridTop = kPad + kTitleHeight;
    if (localPos.y() < gridTop)
        return -1;
    const int col = (localPos.x() - kPad) / kCellW;
    const int row = (localPos.y() - gridTop) / kCellH;
    if (col < 0 || col >= m_cols || row < 0)
        return -1;
    const int index = row * m_cols + col;
    return index < m_items.size() ? index : -1;
}

// ---------- 绘制 ----------

void BoxWindow::paintEvent(QPaintEvent * /*event*/)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);

    // 卡片背景：深色半透明圆角 + 1px 亮边；拖入时边框提亮提示落点
    QPainterPath card;
    card.addRoundedRect(rect().adjusted(0, 0, -1, -1), kRadius, kRadius);
    p.fillPath(card, QColor(24, 26, 30, 205));
    p.setPen(QPen(QColor(255, 255, 255, m_dropHover ? 110 : 32), 1));
    p.setBrush(Qt::NoBrush);
    p.drawPath(card);

    // 标题栏：名称 + 条目数
    const QRect title = titleRect();
    p.setPen(QColor(255, 255, 255, 235));
    QFont f = p.font();
    f.setPixelSize(12);
    f.setBold(true);
    p.setFont(f);
    p.drawText(nameTextRect(), Qt::AlignVCenter | Qt::AlignLeft,
               fontMetrics().elidedText(m_name, Qt::ElideRight, nameTextRect().width()));
    f.setBold(false);
    f.setPixelSize(10);
    p.setFont(f);
    p.setPen(QColor(255, 255, 255, 130));
    p.drawText(title.adjusted(0, 0, -52, 0), Qt::AlignVCenter | Qt::AlignRight,
               m_totalCount > 0 ? QStringLiteral("%1 项").arg(m_totalCount) : QString());

    // 标题栏右侧按钮：⚙ 几行几列、−/+ 折叠（自绘，避免字体差异）
    auto drawGear = [&](const QRect &r, bool active) {
        const QColor c = active ? QColor(0x5B, 0x9B, 0xFF) : QColor(255, 255, 255, 170);
        const QPointF c0 = QRectF(r).center();
        p.setPen(QPen(c, 1.6));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(c0, 4.5, 4.5);
        for (int i = 0; i < 6; ++i) {
            const qreal a = i * M_PI / 3.0;
            p.drawLine(c0 + QPointF(std::cos(a) * 4.5, std::sin(a) * 4.5),
                       c0 + QPointF(std::cos(a) * 7.5, std::sin(a) * 7.5));
        }
        p.drawEllipse(c0, 1.5, 1.5);
    };
    auto drawMinusPlus = [&](const QRect &r, bool plus) {
        const QColor c = QColor(255, 255, 255, 170);
        const QPointF c0 = QRectF(r).center();
        p.setPen(QPen(c, 1.8));
        p.drawLine(QPointF(c0.x() - 5, c0.y()), QPointF(c0.x() + 5, c0.y()));
        if (plus)
            p.drawLine(QPointF(c0.x(), c0.y() - 5), QPointF(c0.x(), c0.y() + 5));
    };
    drawGear(gearRect(), m_hoverGear);
    drawMinusPlus(collapseRect(), m_collapsed);

    // 折叠态：只留标题横条，剩余内容全部收起
    if (m_collapsed)
        return;

    if (m_items.isEmpty()) {
        p.setPen(QColor(255, 255, 255, 150));
        f.setPixelSize(11);
        p.setFont(f);
        const QString hint = QFileInfo::exists(m_dir)
                                 ? QStringLiteral("把桌面文件拖进来收纳")
                                 : QStringLiteral("目录不存在，右键标题可重新设置");
        p.drawText(rect().adjusted(kPad, kPad + kTitleHeight, -kPad, -kPad),
                   Qt::AlignCenter, hint);
        return;
    }

    const QList<QRect> rects = itemRects();
    for (int i = 0; i < rects.size(); ++i) {
        const QRect &cell = rects.at(i);
        const QFileInfo &info = m_items.at(i);

        if (i == m_hoverItem || (m_dropHover && i == 0)) {
            QPainterPath halo;
            halo.addRoundedRect(cell.adjusted(1, 1, -1, -1), 8, 8);
            p.fillPath(halo, QColor(255, 255, 255, 34));
        }

        const QPixmap icon = info.isDir()
                                 ? m_icons->folderIcon()
                                 : m_icons->appIcon(QDir::toNativeSeparators(info.filePath()));
        const QRect iconRect(cell.center().x() - 19, cell.top() + 3, 38, 38);
        p.save();
        QPainterPath clip;
        clip.addRoundedRect(iconRect, 8, 8);
        p.setClipPath(clip);
        p.drawPixmap(iconRect, icon, QRectF(0, 0, icon.width(), icon.height()));
        p.restore();

        p.setPen(QColor(255, 255, 255, 200));
        f.setPixelSize(10);
        p.setFont(f);
        p.drawText(QRect(cell.left(), cell.bottom() - 18, cell.width(), 14),
                   Qt::AlignCenter,
                   fontMetrics().elidedText(info.fileName(), Qt::ElideMiddle, cell.width()));
    }

    if (m_totalCount > m_items.size()) {
        p.setPen(QColor(255, 255, 255, 110));
        f.setPixelSize(10);
        p.setFont(f);
        p.drawText(rect().adjusted(kPad, 0, -kPad, -6), Qt::AlignBottom | Qt::AlignHCenter,
                   QStringLiteral("共 %1 项，右键标题打开目录查看全部").arg(m_totalCount));
    }
}

// ---------- 交互 ----------

void BoxWindow::mousePressEvent(QMouseEvent *event)
{
    const QPointF pos = event->position();
    if (event->button() != Qt::LeftButton)
        return;

    // 标题栏按钮
    if (gearRect().contains(pos.toPoint())) {
        openSizeMenu();
        return;
    }
    if (collapseRect().contains(pos.toPoint())) {
        m_store->setBoxCollapsed(m_boxId, !m_collapsed); // changed → syncMeta
        return;
    }
    // 标题栏按下 → 手动拖动窗口（贴桌窗口也可自由移动，位置落盘）
    if (titleRect().contains(pos.toPoint())) {
        m_draggingWindow = true;
        // 光标在窗口内的位置即拖动偏移（move(globalPos - offset) 用）
        m_dragOffset = pos.toPoint();
        return;
    }
    // 手机文件夹式：单击条目直接打开
    const int hit = hitItem(pos.toPoint());
    if (hit >= 0)
        QDesktopServices::openUrl(QUrl::fromLocalFile(m_items.at(hit).filePath()));
}

// 双击标题【文字】→ 内联改名（整条标题栏不触发，避免拖动误触）
void BoxWindow::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && nameTextRect().contains(event->position().toPoint()))
        startRename();
}

void BoxWindow::mouseMoveEvent(QMouseEvent *event)
{
    const QPointF pos = event->position();

    if (m_draggingWindow) {
        move(event->globalPosition().toPoint() - m_dragOffset);
        return;
    }

    // hover 反馈：条目 / 按钮
    const int hit = hitItem(pos.toPoint());
    const bool onGear = gearRect().contains(pos.toPoint());
    const bool onCollapse = collapseRect().contains(pos.toPoint());
    if (hit != m_hoverItem || onGear != m_hoverGear || onCollapse != m_hoverCollapse) {
        m_hoverItem = hit;
        m_hoverGear = onGear;
        m_hoverCollapse = onCollapse;
        setCursor((hit >= 0 || onGear || onCollapse) ? Qt::PointingHandCursor
                                                     : Qt::ArrowCursor);
        update();
    }
}

void BoxWindow::mouseReleaseEvent(QMouseEvent * /*event*/)
{
    if (m_draggingWindow) {
        m_draggingWindow = false;
        m_store->setBoxPos(m_boxId, x(), y()); // 拖完落盘
    }
}

// ⚙：几行几列设置菜单（列数/行数各 1-8，当前值打勾）
void BoxWindow::openSizeMenu()
{
    QMenu menu;
    auto buildSub = [&](const QString &title, int current, auto setter) {
        QMenu *sub = menu.addMenu(title);
        for (int n = 1; n <= 8; ++n) {
            QAction *a = sub->addAction(QString::number(n));
            a->setCheckable(true);
            a->setChecked(n == current);
            connect(a, &QAction::triggered, this, [this, n, setter] { setter(n); });
        }
    };
    buildSub(QStringLiteral("列数"), m_cols, [this](int n) {
        m_store->setBoxCols(m_boxId, n);
    });
    buildSub(QStringLiteral("行数"), m_rows, [this](int n) {
        m_store->setBoxRows(m_boxId, n);
    });

    // 弹在 ⚙ 按钮上方（全局坐标），不出主屏
    const QPoint anchor = mapToGlobal(gearRect().bottomLeft());
    menu.exec(QPoint(anchor.x() - 60, anchor.y() + 4));
}

// 双击标题改名：内联编辑框盖在标题文字上（需要打字时显式唤起焦点，
// 同日历内联输入的做法）
void BoxWindow::startRename()
{
    if (m_collapsed)
        return;
    if (!m_renameEdit) {
        m_renameEdit = new QLineEdit(this);
        m_renameEdit->setObjectName("boxRenameEdit");
        connect(m_renameEdit, &QLineEdit::editingFinished, this, [this] {
            finishRename(true);
        });
        m_renameEdit->installEventFilter(this);
    }
    m_renameEdit->setGeometry(nameTextRect().adjusted(0, 2, 0, -2));
    m_renameEdit->setText(m_name);
    m_renameEdit->show();
    m_renameEdit->raise();
    m_renameEdit->setFocus();
    m_renameEdit->selectAll();
    activateWindow(); // 不抢焦点窗口要打字时临时唤起（日历同款）
}

void BoxWindow::finishRename(bool commit)
{
    if (!m_renameEdit || !m_renameEdit->isVisible())
        return;
    const QString text = m_renameEdit->text().trimmed();
    m_renameEdit->hide();
    if (commit && !text.isEmpty() && text != m_name)
        m_store->renameBox(m_boxId, text);
}

// 内联编辑框的 Esc 取消
bool BoxWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_renameEdit && event->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(event);
        if (ke->key() == Qt::Key_Escape) {
            finishRename(false);
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void BoxWindow::leaveEvent(QEvent * /*event*/)
{
    m_hoverItem = -1;
    m_hoverGear = m_hoverCollapse = false;
    setCursor(Qt::ArrowCursor);
    update();
}

// ---------- 右键菜单 ----------

// 右键菜单：标题 → 盒子操作；条目 → 条目操作；空白 → 透出桌面菜单
void BoxWindow::contextMenuEvent(QContextMenuEvent *event)
{
    const QPoint pos = event->pos();
    if (titleRect().contains(pos) && !gearRect().contains(pos)
        && !collapseRect().contains(pos)) {
        QMenu menu;
        menu.addAction(QStringLiteral("重命名…"), this, [this] { startRename(); });
        menu.addAction(QStringLiteral("打开目录"), this, [this] {
            QDesktopServices::openUrl(QUrl::fromLocalFile(m_dir));
        });
        menu.addSeparator();
        menu.addAction(QStringLiteral("解散收纳盒（文件移回桌面）"), this, [this] {
            if (QMessageBox::question(
                    this, QStringLiteral("解散收纳盒"),
                    QStringLiteral("解散「%1」？\n盒内全部文件将移回桌面，随后删除盒子"
                                   "与已腾空的目录。")
                        .arg(m_name))
                == QMessageBox::Yes)
                dissolve();
        });
        menu.addAction(QStringLiteral("删除收纳盒（保留磁盘文件）"), this, [this] {
            if (QMessageBox::question(this, QStringLiteral("删除收纳盒"),
                                      QStringLiteral("删除「%1」？\n目录与文件保留在磁盘上。")
                                          .arg(m_name))
                == QMessageBox::Yes)
                m_store->removeBox(m_boxId);
        });
        menu.exec(event->globalPos());
        return;
    }

    const int hit = hitItem(pos);
    if (hit >= 0) {
        const QFileInfo info = m_items.at(hit);
        QMenu menu;
        menu.addAction(QStringLiteral("打开"), this, [info] {
            QDesktopServices::openUrl(QUrl::fromLocalFile(info.filePath()));
        });
        menu.addAction(QStringLiteral("打开所在位置"), this, [info] {
            QDesktopServices::openUrl(QUrl::fromLocalFile(info.absolutePath()));
        });
        menu.addSeparator();
        menu.addAction(QStringLiteral("移出到桌面"), this, [this, info] {
            const QString desktop = QStandardPaths::writableLocation(
                QStandardPaths::DesktopLocation);
            const QString target = BoxStore::uniqueTargetPath(desktop, info.fileName());
            if (!QFile::rename(info.filePath(), target))
                emit storageError(QStringLiteral("无法移出 %1").arg(info.fileName()));
        });
        menu.exec(event->globalPos());
        refresh();
        return;
    }

    // 空白处：透出系统桌面右键菜单（日历同款转发）
#ifdef Q_OS_WIN
    HWND progman = FindWindowW(L"Progman", nullptr);
    HWND defview = progman ? FindWindowExW(progman, nullptr, L"SHELLDLL_DefView", nullptr)
                           : nullptr;
    if (defview) {
        const QPointF phys = QPointF(event->globalPos()) * devicePixelRatioF();
        const LPARAM lParam = MAKELPARAM(static_cast<SHORT>(phys.x()),
                                         static_cast<SHORT>(phys.y()));
        SendMessageTimeoutW(defview, WM_CONTEXTMENU, reinterpret_cast<WPARAM>(defview),
                            lParam, SMTO_NORMAL, 1000, nullptr);
        event->accept();
        return;
    }
#endif
    QWidget::contextMenuEvent(event);
}

// 解散：仓库层移动文件 → 失败转气泡；盒子随 changed 自动关窗
void BoxWindow::dissolve()
{
    const QStringList failures = m_store->dissolveBox(m_boxId);
    if (!failures.isEmpty())
        emit storageError(QStringLiteral("有文件未能移回桌面（%1），"
                                         "盒子已保留。")
                              .arg(failures.join(QStringLiteral("、"))));
}

// ---------- 拖拽收纳 ----------

void BoxWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (!event->mimeData()->hasUrls() || m_collapsed)
        return;
    event->acceptProposedAction();
    m_dropHover = true;
    update();
}

void BoxWindow::dragMoveEvent(QDragMoveEvent *event)
{
    event->acceptProposedAction();
}

void BoxWindow::dragLeaveEvent(QDragLeaveEvent * /*event*/)
{
    if (m_dropHover) {
        m_dropHover = false;
        update();
    }
}

void BoxWindow::dropEvent(QDropEvent *event)
{
    const QList<QUrl> urls = event->mimeData()->urls();
    m_dropHover = false;
    update();
    if (urls.isEmpty())
        return;
    event->acceptProposedAction();
    const QStringList failures = BoxStore::moveUrlsIntoDir(m_dir, urls);
    if (!failures.isEmpty())
        emit storageError(QStringLiteral("无法收纳：%1").arg(failures.join(QStringLiteral("、"))));
    refresh();
}

// ---------- 贴桌与原生事件 ----------

// 贴桌：Z 序经 DesktopStack 钉在日历层之上（1s 全局守护归位）
void BoxWindow::glueIfFixed()
{
    if (!isVisible())
        return;
    desktopstack::attach(this, desktopstack::Layer::Box);
}

void BoxWindow::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    // 显示完成后贴桌（延迟到事件循环，避开 show 过程的原生窗口状态期）
    QTimer::singleShot(0, this, [this] { glueIfFixed(); });
}

void BoxWindow::hideEvent(QHideEvent *event)
{
    desktopstack::detach(this);
    QWidget::hideEvent(event);
}

bool BoxWindow::nativeEvent(const QByteArray &eventType, void *message, qintptr *result)
{
#ifdef Q_OS_WIN
    if (eventType == "windows_generic_MSG") {
        MSG *msg = static_cast<MSG *>(message);
        if (msg->message == WM_MOUSEACTIVATE) {
            // 点击不激活窗口（不抬窗、不抢焦点）——贴桌窗口必须抑制，
            // 否则被点的盒子提到前面、随守护回落形成横跳
            *result = MA_NOACTIVATE;
            return true;
        }
    }
#endif
    Q_UNUSED(eventType)
    Q_UNUSED(message)
    Q_UNUSED(result)
    return QWidget::nativeEvent(eventType, message, result);
}

} // namespace poptools
