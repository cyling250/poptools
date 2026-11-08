#include "CalendarWindow.h"

#include "ItemDelegate.h"
#include "ItemEditDialog.h"
#include "MonthView.h"
#include "core/calendar/CalendarStore.h"
#include "core/calendar/LunarCalendar.h"
#include "core/settings/Settings.h"

#include <QCalendarWidget>
#include <QCursor>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>
#include <QShortcut>
#include <QSplitter>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#ifdef Q_OS_WIN
#include <windows.h>
#include <windowsx.h>
#endif

namespace poptools {

namespace {

constexpr int kTitleBarHeight = 36;
constexpr int kSnapTriggerPx = 18;   // 距屏幕边缘多少像素内触发贴边收起
constexpr qint64 kSnapCooldownMs = 1500;

// 由背景色亮度决定文字用深色还是浅色（用户可改背景色，不能跟着写死黑字）
bool darkTextFor(const QColor &bg)
{
    const qreal lum = 0.299 * bg.redF() + 0.587 * bg.greenF() + 0.114 * bg.blueF();
    return lum > 0.6;
}

// 按 id 在仓库中找条目
bool findItem(CalendarStore *store, const QString &id, CalendarItem *out)
{
    for (const CalendarItem &it : store->items()) {
        if (it.id == id) {
            *out = it;
            return true;
        }
    }
    return false;
}

// 拖拽重排：dropEvent 正常执行后回调回写顺序
class ReorderList : public QListWidget
{
public:
    using QListWidget::QListWidget;
    std::function<void()> reordered;

protected:
    void dropEvent(QDropEvent *event) override
    {
        QListWidget::dropEvent(event);
        if (event->isAccepted() && reordered)
            reordered();
    }
};

// 指定日期小对话框（安排到……）
QDate pickDate(QWidget *parent, const QDate &initial)
{
    QDialog dlg(parent);
    dlg.setWindowTitle(QStringLiteral("选择日期"));
    auto *lay = new QVBoxLayout(&dlg);
    auto *cal = new QCalendarWidget(&dlg);
    cal->setSelectedDate(initial.isValid() ? initial : QDate::currentDate());
    lay->addWidget(cal);
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    lay->addWidget(bb);
    QObject::connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    QObject::connect(cal, &QCalendarWidget::clicked, &dlg, &QDialog::accept);
    if (dlg.exec() == QDialog::Accepted)
        return cal->selectedDate();
    return QDate();
}

} // namespace

// ============================================================
// 贴边小条（file-local）：悬停/点击展开主窗口
// ============================================================
class CalendarWindow::EdgeTab : public QWidget
{
public:
    EdgeTab()
    {
        setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint
                       | Qt::NoDropShadowWindowHint);
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_ShowWithoutActivating);
        setCursor(Qt::PointingHandCursor);
        m_hoverTimer.setSingleShot(true);
        m_hoverTimer.setInterval(280);
        connect(&m_hoverTimer, &QTimer::timeout, this, [this] {
            if (underMouse() && expandRequested)
                expandRequested();
        });
    }

    // 展开请求回调（file-local 类不进 moc，用 std::function 替代信号）
    std::function<void()> expandRequested;

    void showFor(Edge edge, const QRect &anchor, const QColor &accent)
    {
        m_edge = edge;
        m_accent = accent;
        const QScreen *screen = QGuiApplication::screenAt(anchor.center());
        const QRect avail = screen ? screen->availableGeometry()
                                   : QGuiApplication::primaryScreen()->availableGeometry();
        if (edge == Edge::Top) {
            const int w = qBound(120, anchor.width() / 3, 180);
            resize(w, 9);
            move(avail.left() + (avail.width() - width()) / 2, avail.top());
        } else {
            const int h = qBound(80, anchor.height() / 2, 160);
            resize(9, h);
            const int y = qBound(avail.top() + 8, anchor.center().y() - h / 2,
                                 avail.bottom() - h - 8);
            move(edge == Edge::Left ? avail.left() : avail.right() - width() + 1, y);
        }
        show();
    }

    Edge edge() const { return m_edge; }

protected:
    void paintEvent(QPaintEvent * /*event*/) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(m_accent);
        p.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 4, 4);
    }
    void enterEvent(QEnterEvent *event) override
    {
        m_hoverTimer.start();
        QWidget::enterEvent(event);
    }
    void leaveEvent(QEvent *event) override
    {
        m_hoverTimer.stop();
        QWidget::leaveEvent(event);
    }
    void mousePressEvent(QMouseEvent *event) override
    {
        if (expandRequested)
            expandRequested();
        QWidget::mousePressEvent(event);
    }

private:
    Edge m_edge = Edge::None;
    QColor m_accent;
    QTimer m_hoverTimer;
};

// ============================================================
// CalendarWindow
// ============================================================

CalendarWindow::CalendarWindow(CalendarStore *store, QWidget *parent)
    : QWidget(parent)
    , m_store(store)
{
    setObjectName("calendarWindow");
    setWindowIcon(QIcon(":/app_icon.svg"));
    setAttribute(Qt::WA_TranslucentBackground);
    setMinimumSize(520, 360);
    resize(680, 440);

    // 上次位置/尺寸与分栏状态
    if (!restoreGeometry(Settings::instance().calendarGeometry())) {
        const QRect avail = QGuiApplication::primaryScreen()->availableGeometry();
        move(avail.center() - rect().center());
    }

    buildUi();
    applySettings();

    connect(m_store, &CalendarStore::changed, this, &CalendarWindow::storeChanged);
    connect(m_monthView, &MonthView::selectedDateChanged, this, [this] {
        refreshLists();
        refreshDots();
        refreshTitle();
    });
    connect(m_monthView, &MonthView::dayActivated, this,
            [this] { m_todoInput->setFocus(); });

    storeChanged();
}

void CalendarWindow::buildUi()
{
    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(1, 1, 1, 1);
    rootLayout->setSpacing(0);

    // ---- 标题栏 ----
    auto *titleBar = new QWidget(this);
    titleBar->setObjectName("calendarTitleBar");
    titleBar->setFixedHeight(kTitleBarHeight);
    auto *titleLayout = new QHBoxLayout(titleBar);
    titleLayout->setContentsMargins(12, 0, 8, 0);
    titleLayout->setSpacing(4);

    auto *titleLabel = new QLabel(QStringLiteral("桌面日历"), titleBar);
    titleLabel->setObjectName("calendarTitleLabel");

    m_pinBtn = new QToolButton(titleBar);
    m_pinBtn->setObjectName("calPinBtn");
    m_pinBtn->setText(QStringLiteral("📌"));
    m_pinBtn->setCheckable(true);
    m_pinBtn->setToolTip(QStringLiteral("窗口置顶"));
    m_pinBtn->setFixedSize(28, 26);
    m_pinBtn->setChecked(Settings::instance().calendarAlwaysOnTop());
    connect(m_pinBtn, &QToolButton::toggled, this, [](bool on) {
        Settings::instance().setCalendarAlwaysOnTop(on);
    });

    m_miniBtn = new QToolButton(titleBar);
    m_miniBtn->setObjectName("calMiniBtn");
    m_miniBtn->setText(QStringLiteral("—"));
    m_miniBtn->setToolTip(QStringLiteral("收起到屏幕右上角（只显示今日待办）"));
    m_miniBtn->setFixedSize(28, 26);
    connect(m_miniBtn, &QToolButton::clicked, this, [this] {
        hide();
        emit miniRequested();
    });

    m_closeBtn = new QToolButton(titleBar);
    m_closeBtn->setObjectName("calCloseBtn");
    m_closeBtn->setText(QStringLiteral("✕"));
    m_closeBtn->setToolTip(QStringLiteral("关闭（可从托盘菜单重新打开）"));
    m_closeBtn->setFixedSize(28, 26);
    connect(m_closeBtn, &QToolButton::clicked, this, &QWidget::hide);

    titleLayout->addWidget(titleLabel);
    titleLayout->addStretch();
    titleLayout->addWidget(m_pinBtn);
    titleLayout->addWidget(m_miniBtn);
    titleLayout->addWidget(m_closeBtn);
    rootLayout->addWidget(titleBar);

    // ---- 内容区：左月历 | 右（日程 / 待办） ----
    m_hSplit = new QSplitter(Qt::Horizontal, this);
    m_hSplit->setObjectName("calHSplit");
    m_hSplit->setChildrenCollapsible(false);
    m_hSplit->setHandleWidth(4);

    // 左：月历列
    auto *calPanel = new QWidget(m_hSplit);
    m_hSplit->addWidget(calPanel);
    auto *calLayout = new QVBoxLayout(calPanel);
    calLayout->setContentsMargins(10, 2, 6, 10);
    calLayout->setSpacing(6);

    auto *navRow = new QWidget(calPanel);
    auto *navLayout = new QHBoxLayout(navRow);
    navLayout->setContentsMargins(0, 0, 0, 0);
    navLayout->setSpacing(4);
    auto *prevBtn = new QToolButton(navRow);
    prevBtn->setObjectName("calNavBtn");
    prevBtn->setText(QStringLiteral("‹"));
    auto *nextBtn = new QToolButton(navRow);
    nextBtn->setObjectName("calNavBtn");
    nextBtn->setText(QStringLiteral("›"));
    auto *todayBtn = new QToolButton(navRow);
    todayBtn->setObjectName("calNavBtn");
    todayBtn->setText(QStringLiteral("今天"));
    m_monthLabel = new QLabel(navRow);
    m_monthLabel->setObjectName("calMonthLabel");
    connect(prevBtn, &QToolButton::clicked, this, [this] {
        m_monthView->setSelectedDate(m_monthView->selectedDate().addMonths(-1));
    });
    connect(nextBtn, &QToolButton::clicked, this, [this] {
        m_monthView->setSelectedDate(m_monthView->selectedDate().addMonths(1));
    });
    connect(todayBtn, &QToolButton::clicked, this, [this] {
        m_monthView->setSelectedDate(QDate::currentDate());
    });
    navLayout->addWidget(prevBtn);
    navLayout->addWidget(m_monthLabel, 1, Qt::AlignCenter);
    navLayout->addWidget(nextBtn);
    navLayout->addWidget(todayBtn);

    m_monthView = new MonthView(calPanel);
    calLayout->addWidget(navRow);
    calLayout->addWidget(m_monthView, 1);

    // 右：日程 / 待办（垂直分栏）
    m_vSplit = new QSplitter(Qt::Vertical, this);
    m_vSplit->setObjectName("calVSplit");
    m_vSplit->setChildrenCollapsible(false);
    m_vSplit->setHandleWidth(4);
    m_hSplit->addWidget(m_vSplit);

    const QLocale zhLocale(QLocale::Chinese, QLocale::China);

    auto buildPanel = [this, &zhLocale](const QString &title, bool todo) -> QWidget * {
        auto *panel = new QWidget(m_vSplit);
        m_vSplit->addWidget(panel);
        auto *lay = new QVBoxLayout(panel);
        lay->setContentsMargins(4, 2, 8, 8);
        lay->setSpacing(4);

        QLabel **labelOut = todo ? &m_todoLabel : &m_scheduleLabel;
        *labelOut = new QLabel(title, panel);
        (*labelOut)->setObjectName("calPanelLabel");
        lay->addWidget(*labelOut);

        auto *list = new ReorderList(panel);
        if (todo)
            m_todoList = list;
        else
            m_scheduleList = list;
        list->setObjectName(todo ? "calTodoList" : "calScheduleList");
        list->setFrameShape(QFrame::NoFrame);
        list->setSelectionMode(QAbstractItemView::SingleSelection);
        list->setDragDropMode(QAbstractItemView::InternalMove);
        list->setDefaultDropAction(Qt::MoveAction);
        list->setContextMenuPolicy(Qt::CustomContextMenu);
        list->setItemDelegate(new ItemDelegate(list));
        lay->addWidget(list, 1);

        auto *input = new QLineEdit(panel);
        if (todo)
            m_todoInput = input;
        else
            m_scheduleInput = input;
        input->setObjectName(todo ? "calTodoInput" : "calScheduleInput");
        input->setPlaceholderText(todo ? QStringLiteral("+ 添加待办，回车确认")
                                       : QStringLiteral("+ 添加日程，回车确认"));
        lay->addWidget(input);

        connect(input, &QLineEdit::returnPressed, this,
                [this, input, todo] { addFromInput(input, todo); });

        // 待办：点击左侧 30px 勾选区切换完成；双击整行打开编辑
        connect(list, &QListWidget::itemClicked, this, [this, list](QListWidgetItem *row) {
            if (!row || !row->data(itemroles::Todo).toBool())
                return;
            const QPoint pos = list->viewport()->mapFromGlobal(QCursor::pos());
            if (pos.x() - list->visualItemRect(row).x() <= 30)
                toggleDone(list, row);
        });
        connect(list, &QListWidget::itemDoubleClicked, this,
                [this, list](QListWidgetItem *row) { editItem(list, row); });
        connect(list, &QListWidget::customContextMenuRequested, this,
                [this, list, todo](const QPoint &pos) {
                    showContextMenu(list, todo, list->viewport()->mapToGlobal(pos));
                });

        auto *delShortcut = new QShortcut(QKeySequence::Delete, list);
        delShortcut->setContext(Qt::WidgetShortcut);
        connect(delShortcut, &QShortcut::activated, this, [this, list] {
            if (QListWidgetItem *row = list->currentItem())
                removeItem(list, row);
        });
        return panel;
    };

    buildPanel(QStringLiteral("日程"), false);
    buildPanel(QStringLiteral("待办"), true);

    // 分栏比例：月历略宽
    m_hSplit->setStretchFactor(0, 5);
    m_hSplit->setStretchFactor(1, 4);
    m_vSplit->setStretchFactor(0, 1);
    m_vSplit->setStretchFactor(1, 1);

    // 拖拽落点回写顺序
    static_cast<ReorderList *>(m_scheduleList)->reordered = [this] {
        syncReorder(m_scheduleList, false);
    };
    static_cast<ReorderList *>(m_todoList)->reordered = [this] {
        syncReorder(m_todoList, true);
    };

    // 分栏状态持久化：base64(水平) | base64(垂直)
    const QByteArray splitterState = Settings::instance().calendarSplitter();
    const QList<QByteArray> parts = splitterState.split('|');
    if (parts.size() == 2) {
        m_hSplit->restoreState(QByteArray::fromBase64(parts.at(0)));
        m_vSplit->restoreState(QByteArray::fromBase64(parts.at(1)));
    }

    rootLayout->addWidget(m_hSplit, 1);

    m_edgeTab = new EdgeTab();
    m_edgeTab->expandRequested = [this] { expandFromEdge(); };
}

// ---------- 增删改与数据刷新（见下方各节） ----------

void CalendarWindow::updateWindowFlags()
{
    Qt::WindowFlags f = Qt::Tool | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint;
    if (Settings::instance().calendarAlwaysOnTop())
        f |= Qt::WindowStaysOnTopHint;
    if (windowFlags() != f) {
        const bool wasVisible = isVisible();
        setWindowFlags(f);
        if (wasVisible)
            show();
    }
    m_pinBtn->setChecked(Settings::instance().calendarAlwaysOnTop());
}

void CalendarWindow::applySettings()
{
    updateWindowFlags();
    setWindowOpacity(Settings::instance().calendarOpacity() / 100.0);
    m_bg = Settings::instance().calendarBgColor();
    m_accent = Settings::instance().calendarAccentColor();
    m_darkText = darkTextFor(m_bg);
    applyTheme();
    refreshDots();

    // 关掉贴边后若正停靠，先展开
    if (!Settings::instance().calendarEdgeSnap() && m_edgeTab && m_edgeTab->isVisible())
        expandFromEdge();
}

void CalendarWindow::applyTheme()
{
    const QColor text = m_darkText ? QColor("#000000") : QColor("#FFFFFF");
    const QColor subText = m_darkText ? QColor(0, 0, 0, 130) : QColor(255, 255, 255, 150);
    const QColor line = m_darkText ? QColor(0, 0, 0, 28) : QColor(255, 255, 255, 36);
    const QColor inputBg = m_darkText ? QColor(255, 255, 255, 120) : QColor(0, 0, 0, 60);
    const QColor btnBorder = m_darkText ? QColor(0, 0, 0, 40) : QColor(255, 255, 255, 55);

    // MonthView 配色
    MonthView::Palette p;
    p.bg = m_bg;
    p.text = text;
    p.subText = subText;
    p.dimText = m_darkText ? QColor(0, 0, 0, 60) : QColor(255, 255, 255, 90);
    p.festival = QColor(0xE0, 0x53, 0x3D);
    p.accent = m_accent;
    p.line = line;
    m_monthView->setViewPalette(p);

    // 列表委托配色
    ItemDelegate::Palette dp;
    dp.text = text;
    dp.subText = subText;
    dp.accent = m_accent;
    static_cast<ItemDelegate *>(m_scheduleList->itemDelegate())->setDelegatePalette(dp);
    static_cast<ItemDelegate *>(m_todoList->itemDelegate())->setDelegatePalette(dp);

    // 控件样式（自定义类一律 ID 选择器，P-022；子控件用类型选择器）
    const QString accent = m_accent.name(QColor::HexRgb);
    setStyleSheet(QStringLiteral(
        "QWidget#calendarTitleBar { background: transparent; }"
        "QLabel { color: %1; background: transparent; }"
        "QLabel#calendarTitleLabel { font-weight: bold; font-size: 13px; }"
        "QLabel#calMonthLabel { font-weight: bold; font-size: 14px; }"
        "QLabel#calPanelLabel { font-weight: bold; font-size: 12px; }"
        "QToolButton { background: transparent; border: 1px solid %2;"
        "  border-radius: 6px; color: %1; padding: 2px; font-size: 12px; }"
        "QToolButton:hover { border-color: %3; color: %3; }"
        "QToolButton#calPinBtn:checked { background: %3; color: #FFFFFF;"
        "  border-color: %3; }"
        "QToolButton#calNavBtn { border: none; font-weight: bold; padding: 3px 8px; }"
        "QListWidget { background: transparent; color: %1;"
        "  outline: none; font-size: 12px; }"
        "QListWidget::item { background: transparent; }"
        "QListWidget::item:selected { background: transparent; }"
        "QLineEdit { background: %4; border: 1px solid %2; border-radius: 6px;"
        "  padding: 4px 8px; color: %1; font-size: 12px; }"
        "QLineEdit:focus { border-color: %3; }"
        "QSplitter::handle { background: %5; }"
        "QSplitter::handle:hover { background: %3; }"
        "QScrollArea { background: transparent; }")
        .arg(text.name(QColor::HexRgb), btnBorder.name(QColor::HexRgb), accent,
             inputBg.name(QColor::HexRgb), line.name(QColor::HexRgb)));
}

void CalendarWindow::refreshTitle()
{
    const QDate d = m_monthView->selectedDate();
    m_monthLabel->setText(QStringLiteral("%1年%2月").arg(d.year()).arg(d.month()));
}

void CalendarWindow::refreshLists()
{
    const QDate date = m_monthView->selectedDate();
    const QLocale zhLocale(QLocale::Chinese, QLocale::China);

    auto fill = [this, &date, &zhLocale](QListWidget *list, bool todo, QLabel *label,
                                         const QString &title) {
        list->clear();
        const QList<CalendarItem> items = m_store->itemsForDate(date, todo);
        for (const CalendarItem &it : items)
            loadItemRow(list, it);
        const QDate today = QDate::currentDate();
        const QString dayText =
            date == today ? QStringLiteral("今天") : date.toString(QStringLiteral("M月d日"));
        const QString week = zhLocale.toString(date, QStringLiteral("dddd"));
        label->setText(QStringLiteral("%1 · %2 %3（%4 项）").arg(title, dayText, week,
                                                                QString::number(items.size())));
    };
    fill(m_scheduleList, false, m_scheduleLabel, QStringLiteral("日程"));
    fill(m_todoList, true, m_todoLabel, QStringLiteral("待办"));
}

void CalendarWindow::loadItemRow(QListWidget *list, const CalendarItem &item)
{
    auto *row = new QListWidgetItem(item.text, list);
    row->setData(itemroles::ItemId, item.id);
    row->setData(itemroles::CategoryColor, m_store->categoryColor(item.categoryId));
    row->setData(itemroles::Important, item.important);
    row->setData(itemroles::Todo, item.todo);
    row->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled);
    if (item.todo) {
        row->setFlags(row->flags() | Qt::ItemIsUserCheckable);
        row->setCheckState(item.done ? Qt::Checked : Qt::Unchecked);
    }
    QStringList tips;
    tips << m_store->categoryName(item.categoryId);
    if (item.important)
        tips << QStringLiteral("重要");
    if (item.todo)
        tips << (item.done ? QStringLiteral("已完成") : QStringLiteral("未完成"));
    row->setToolTip(tips.join(QStringLiteral(" · ")));
}

void CalendarWindow::refreshDots()
{
    QHash<QDate, QVector<QColor>> dots;
    const QDate selected = m_monthView->selectedDate();
    const QDate first(selected.year(), selected.month(), 1);
    const QDate last = first.addMonths(1).addDays(-1);
    for (const CalendarItem &it : m_store->items()) {
        if (it.date < first || it.date > last)
            continue;
        const QColor c = m_store->categoryColor(it.categoryId);
        QVector<QColor> &list = dots[it.date];
        if (!list.contains(c) && list.size() < 3)
            list << c;
    }
    m_monthView->setEventDots(dots);
}

void CalendarWindow::storeChanged()
{
    refreshLists();
    refreshDots();
    refreshTitle();
}

// ---------- 增删改 ----------

void CalendarWindow::addFromInput(QLineEdit *input, bool todo)
{
    const QString text = input->text().trimmed();
    if (text.isEmpty())
        return;
    const QDate date = m_monthView->selectedDate();
    if (todo)
        m_store->addTodo(date, QString(), text, false);
    else
        m_store->addSchedule(date, QString(), text, false);
    input->clear();
    input->setFocus();
}

void CalendarWindow::editItem(QListWidget *list, QListWidgetItem *row)
{
    if (!row)
        return;
    const QString id = row->data(itemroles::ItemId).toString();
    CalendarItem item;
    if (!findItem(m_store, id, &item))
        return;
    ItemEditDialog dlg(m_store, item.date, item.todo, id, this);
    if (dlg.exec() == QDialog::Accepted && !dlg.text().isEmpty())
        m_store->updateItem(id, dlg.text(), dlg.categoryId(), dlg.important());
}

void CalendarWindow::removeItem(QListWidget *list, QListWidgetItem *row)
{
    if (row)
        m_store->removeItem(row->data(itemroles::ItemId).toString());
}

void CalendarWindow::toggleDone(QListWidget *list, QListWidgetItem *row)
{
    Q_UNUSED(list);
    if (!row)
        return;
    const QString id = row->data(itemroles::ItemId).toString();
    CalendarItem item;
    if (findItem(m_store, id, &item))
        m_store->setDone(id, !item.done);
}

void CalendarWindow::toggleImportant(QListWidget *list, QListWidgetItem *row)
{
    Q_UNUSED(list);
    if (!row)
        return;
    const QString id = row->data(itemroles::ItemId).toString();
    CalendarItem item;
    if (findItem(m_store, id, &item))
        m_store->updateItem(id, item.text, item.categoryId, !item.important);
}

void CalendarWindow::moveItemRelative(QListWidget *list, QListWidgetItem *row, int delta)
{
    if (!row)
        return;
    m_store->moveItem(row->data(itemroles::ItemId).toString(), delta);
}

void CalendarWindow::showContextMenu(QListWidget *list, bool todo, const QPoint &globalPos)
{
    QListWidgetItem *row = list->currentItem();
    QMenu menu(list);
    if (row) {
        const QString id = row->data(itemroles::ItemId).toString();
        CalendarItem item;
        if (!findItem(m_store, id, &item))
            return;

        if (todo)
            menu.addAction(item.done ? QStringLiteral("标记为未完成")
                                     : QStringLiteral("标记为完成"),
                           [this, list, row] { toggleDone(list, row); });
        menu.addAction(item.important ? QStringLiteral("取消重要★")
                                      : QStringLiteral("标记重要★"),
                       [this, list, row] { toggleImportant(list, row); });
        menu.addAction(QStringLiteral("编辑…"), [this, list, row] { editItem(list, row); });
        menu.addSeparator();
        menu.addAction(QStringLiteral("上移"), [this, list, row] {
            moveItemRelative(list, row, -1);
        });
        menu.addAction(QStringLiteral("下移"), [this, list, row] {
            moveItemRelative(list, row, 1);
        });
        menu.addSeparator();
        QMenu *assign = menu.addMenu(QStringLiteral("安排到…"));
        const QDate today = QDate::currentDate();
        assign->addAction(QStringLiteral("明天"), [this, id, today] {
            m_store->moveItemToDate(id, today.addDays(1));
        });
        assign->addAction(QStringLiteral("后天"), [this, id, today] {
            m_store->moveItemToDate(id, today.addDays(2));
        });
        assign->addAction(QStringLiteral("下周同日"), [this, id, today] {
            m_store->moveItemToDate(id, today.addDays(7));
        });
        assign->addAction(QStringLiteral("选择日期…"), [this, id, today] {
            const QDate d = pickDate(this, today);
            if (d.isValid())
                m_store->moveItemToDate(id, d);
        });
        menu.addSeparator();
        menu.addAction(QStringLiteral("删除"), [this, list, row] { removeItem(list, row); });
    } else {
        // 空白处右键：快速新增
        menu.addAction(todo ? QStringLiteral("新增待办…") : QStringLiteral("新增日程…"),
                       [this, todo] {
                           ItemEditDialog dlg(m_store, m_monthView->selectedDate(), todo,
                                              QString(), this);
                           if (dlg.exec() == QDialog::Accepted && !dlg.text().isEmpty()) {
                               if (todo)
                                   m_store->addTodo(m_monthView->selectedDate(),
                                                    dlg.categoryId(), dlg.text(),
                                                    dlg.important());
                               else
                                   m_store->addSchedule(m_monthView->selectedDate(),
                                                        dlg.categoryId(), dlg.text(),
                                                        dlg.important());
                           }
                       });
    }
    menu.exec(globalPos);
}

void CalendarWindow::syncReorder(QListWidget *list, bool todo)
{
    QStringList ids;
    for (int i = 0; i < list->count(); ++i)
        ids << list->item(i)->data(itemroles::ItemId).toString();
    m_store->reorderItems(m_monthView->selectedDate(), todo, ids);
}

// ---------- 贴边收起 ----------

CalendarWindow::Edge CalendarWindow::currentEdge() const
{
    const QRect g = frameGeometry();
    const QScreen *screen = QGuiApplication::screenAt(g.center());
    if (!screen)
        return Edge::None;
    const QRect avail = screen->availableGeometry();
    if (g.left() <= avail.left() + kSnapTriggerPx)
        return Edge::Left;
    if (g.right() >= avail.right() - kSnapTriggerPx)
        return Edge::Right;
    if (g.top() <= avail.top() + kSnapTriggerPx)
        return Edge::Top;
    return Edge::None;
}

void CalendarWindow::maybeSnapToEdge()
{
    if (!Settings::instance().calendarEdgeSnap())
        return;
    if (m_edgeTab->isVisible())
        return;
    if (QDateTime::currentMSecsSinceEpoch() < m_snapCooldownUntil)
        return;
    const Edge edge = currentEdge();
    if (edge != Edge::None)
        dockToEdge(edge);
}

void CalendarWindow::dockToEdge(Edge edge)
{
    m_preDockGeometry = saveGeometry();
    m_edgeTab->showFor(edge, frameGeometry(), m_accent);
    hide();
    emit dockChanged(true);
}

void CalendarWindow::expandFromEdge()
{
    m_edgeTab->hide();
    if (!m_preDockGeometry.isEmpty()) {
        restoreGeometry(m_preDockGeometry);
        m_preDockGeometry.clear();
    }
    // 展开位置离边缘 40px，避免立即再次触发吸附
    const QRect g = frameGeometry();
    const QScreen *screen = QGuiApplication::screenAt(g.center());
    const QRect avail = screen ? screen->availableGeometry()
                               : QGuiApplication::primaryScreen()->availableGeometry();
    int x = g.x(), y = g.y();
    if (g.left() <= avail.left() + kSnapTriggerPx)
        x = avail.left() + 40;
    if (g.right() >= avail.right() - kSnapTriggerPx)
        x = avail.right() - g.width() - 40;
    if (g.top() <= avail.top() + kSnapTriggerPx)
        y = avail.top() + 40;
    move(x, y);

    m_snapCooldownUntil = QDateTime::currentMSecsSinceEpoch() + kSnapCooldownMs;
    show();
    raise();
    activateWindow();
    emit dockChanged(false);
}

// ---------- 窗口事件 ----------

void CalendarWindow::moveEvent(QMoveEvent *event)
{
    QWidget::moveEvent(event);
    maybeSnapToEdge();
}

void CalendarWindow::hideEvent(QHideEvent *event)
{
    // 几何与分栏状态持久化（贴边收起时的隐藏不覆盖预停靠几何）
    if (!m_edgeTab->isVisible())
        Settings::instance().setCalendarGeometry(saveGeometry());
    const QByteArray state = m_hSplit->saveState().toBase64() + '|'
                             + m_vSplit->saveState().toBase64();
    Settings::instance().setCalendarSplitter(state);
    QWidget::hideEvent(event);
}

void CalendarWindow::paintEvent(QPaintEvent * /*event*/)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const qreal radius = 12.0;
    QPainterPath path;
    path.addRoundedRect(rect().adjusted(0, 0, -1, -1), radius, radius);
    p.fillPath(path, m_bg);
    const QColor border = m_darkText ? QColor(0, 0, 0, 36) : QColor(255, 255, 255, 46);
    p.setPen(QPen(border, 1));
    p.setBrush(Qt::NoBrush);
    p.drawPath(path);
}

bool CalendarWindow::nativeEvent(const QByteArray &eventType, void *message, qintptr *result)
{
#ifdef Q_OS_WIN
    if (eventType == "windows_generic_MSG") {
        MSG *msg = static_cast<MSG *>(message);
        if (msg->message == WM_NCHITTEST) {
            const qreal dpr = devicePixelRatioF();
            const QPoint globalPhys(GET_X_LPARAM(msg->lParam), GET_Y_LPARAM(msg->lParam));
            const QPointF local = mapFromGlobal(QPointF(globalPhys) / dpr);
            const int w = width(), h = height();
            constexpr int B = 6;

            const bool atLeft = local.x() <= B;
            const bool atRight = local.x() >= w - B;
            const bool atTop = local.y() <= B;
            const bool atBottom = local.y() >= h - B;

            if (atLeft && atTop) *result = HTTOPLEFT;
            else if (atRight && atTop) *result = HTTOPRIGHT;
            else if (atLeft && atBottom) *result = HTBOTTOMLEFT;
            else if (atRight && atBottom) *result = HTBOTTOMRIGHT;
            else if (atLeft) *result = HTLEFT;
            else if (atRight) *result = HTRIGHT;
            else if (atTop) *result = HTTOP;
            else if (atBottom) *result = HTBOTTOM;
            else if (local.y() <= kTitleBarHeight) {
                // 标题栏 = 拖动区；其上的按钮（QToolButton 子控件）不拦截
                QWidget *child = childAt(local.toPoint());
                if (qobject_cast<QToolButton *>(child))
                    *result = HTCLIENT;
                else
                    *result = HTCAPTION;
            } else {
                return QWidget::nativeEvent(eventType, message, result);
            }
            return true;
        }
    }
#endif
    return QWidget::nativeEvent(eventType, message, result);
}

} // namespace poptools
