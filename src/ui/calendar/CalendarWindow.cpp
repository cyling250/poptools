#include "CalendarWindow.h"

#include "ui/desktopband/DesktopBandHost.h"
#include "ui/desktopband/DesktopBandLogic.h"
#include "ui/shared/DesktopStack.h"
#include "ItemDelegate.h"
#include "ItemEditDialog.h"
#include "MonthView.h"
#include "core/calendar/CalendarStore.h"
#include "core/calendar/LunarCalendar.h"
#include "core/settings/Settings.h"

#include <QApplication>
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
    applyWindowFlags();
    applySettings();

    connect(m_store, &CalendarStore::changed, this, &CalendarWindow::storeChanged);
    connect(m_monthView, &MonthView::selectedDateChanged, this, [this] {
        hideInlineAdd();
        refreshLists();
        refreshDots();
        refreshTitle();
    });
    // 双击格子：原位内联添加日程
    connect(m_monthView, &MonthView::dayActivated, this, &CalendarWindow::showInlineAdd);

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
    titleLayout->setSpacing(6);

    auto *titleLabel = new QLabel(QStringLiteral("桌面日历"), titleBar);
    titleLabel->setObjectName("calendarTitleLabel");

    // 模式提示（编辑模式可见）
    m_modeHint = new QLabel(QStringLiteral("编辑中：拖动移动 · 拖边缘缩放"), titleBar);
    m_modeHint->setObjectName("calModeHint");

    m_modeBtn = new QToolButton(titleBar);
    m_modeBtn->setObjectName("calModeBtn");
    m_modeBtn->setFixedSize(28, 26);
    connect(m_modeBtn, &QToolButton::clicked, this,
            [this] { setEditMode(!m_editMode); });

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
    m_closeBtn->setToolTip(QStringLiteral("隐藏（可在 设置 → 桌面日历 中重新开启）"));
    m_closeBtn->setFixedSize(28, 26);
    connect(m_closeBtn, &QToolButton::clicked, this, [this] {
        // 隐藏 = 关闭功能，与设置开关保持一致
        Settings::instance().setCalendarEnabled(false);
        hide();
    });

    titleLayout->addWidget(titleLabel);
    titleLayout->addWidget(m_modeHint);
    titleLayout->addStretch();
    titleLayout->addWidget(m_modeBtn);
    titleLayout->addWidget(m_miniBtn);
    titleLayout->addWidget(m_closeBtn);
    rootLayout->addWidget(titleBar);

    // ---- 内容区：左月历 | 右日程列表 ----
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

    // 右：日程列表（日程=待办单一列表，可勾选完成）
    auto *panel = new QWidget(m_hSplit);
    m_hSplit->addWidget(panel);
    auto *lay = new QVBoxLayout(panel);
    lay->setContentsMargins(4, 2, 8, 8);
    lay->setSpacing(4);

    m_itemLabel = new QLabel(QStringLiteral("日程"), panel);
    m_itemLabel->setObjectName("calPanelLabel");
    lay->addWidget(m_itemLabel);

    auto *list = new ReorderList(panel);
    m_itemList = list;
    list->setObjectName("calItemList");
    list->setFrameShape(QFrame::NoFrame);
    list->setSelectionMode(QAbstractItemView::SingleSelection);
    list->setDragDropMode(QAbstractItemView::InternalMove);
    list->setDefaultDropAction(Qt::MoveAction);
    list->setContextMenuPolicy(Qt::CustomContextMenu);
    list->setItemDelegate(new ItemDelegate(list));
    lay->addWidget(list, 1);

    m_input = new QLineEdit(panel);
    m_input->setObjectName("calItemInput");
    m_input->setPlaceholderText(QStringLiteral("+ 添加日程，回车确认（可勾选完成）"));
    m_input->installEventFilter(this);
    lay->addWidget(m_input);
    connect(m_input, &QLineEdit::returnPressed, this, [this] {
        addFromInput(m_input->text());
        m_input->clear();
        m_input->setFocus();
    });

    // 日程：点击左侧 30px 勾选区切换完成；双击整行打开编辑
    connect(list, &QListWidget::itemClicked, this, [this, list](QListWidgetItem *row) {
        if (!row)
            return;
        const QPoint pos = list->viewport()->mapFromGlobal(QCursor::pos());
        if (pos.x() - list->visualItemRect(row).x() <= 30)
            toggleDone(row);
    });
    connect(list, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem *row) { editItem(row); });
    connect(list, &QListWidget::customContextMenuRequested, this,
            [this, list](const QPoint &pos) {
                showContextMenu(list->viewport()->mapToGlobal(pos));
            });

    auto *delShortcut = new QShortcut(QKeySequence::Delete, list);
    delShortcut->setContext(Qt::WidgetShortcut);
    connect(delShortcut, &QShortcut::activated, this, [this] {
        if (QListWidgetItem *row = m_itemList->currentItem())
            removeItem(row);
    });

    // 拖拽落点回写顺序
    list->reordered = [this] { syncReorder(); };

    // 分栏比例：月历略宽
    m_hSplit->setStretchFactor(0, 5);
    m_hSplit->setStretchFactor(1, 4);

    // 分栏状态持久化
    const QByteArray splitterState = Settings::instance().calendarSplitter();
    if (!splitterState.isEmpty())
        m_hSplit->restoreState(QByteArray::fromBase64(splitterState));

    rootLayout->addWidget(m_hSplit, 1);

    // 双击格子的内联输入框（懒创建）
    m_inlineAdd = nullptr;

    updateModeUi();
}

// ---------- 模式切换 ----------

void CalendarWindow::applyWindowFlags()
{
    Qt::WindowFlags f = Qt::Tool | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint;
    if (m_editMode)
        f |= Qt::WindowStaysOnTopHint; // 编辑时浮到最上，方便抓取拖动
    if (windowFlags() != f) {
        const bool wasVisible = isVisible();
        setWindowFlags(f); // 重建原生窗口（顺带完成解钉）
        if (wasVisible)
            show();
    }
}

void CalendarWindow::setEditMode(bool on)
{
    if (on == m_editMode)
        return;
    hideInlineAdd();
    m_editMode = on;
    m_bandSuspended = false; // 打字挂起态随模式切换一并清除
    if (on)
        desktopstack::detach(this); // 编辑期间浮为置顶，退出贴桌队列
    // T-048：带模式进出——编辑=收起带呈现、真窗口回归（置顶浮动可拖缩）；
    // 完成编辑走 glueIfFixed 重新驻带
    if (on && bandActive()) {
        m_band->hideBand();
        m_bandSwitchGuard = true;
        setVisible(true);
        m_bandSwitchGuard = false;
    }
    applyWindowFlags();
    if (on) {
        raise();
        activateWindow();
    } else if (isVisible()) {
        glueIfFixed(); // 编辑 → 固定：当前位置驻带/贴底
    }
    updateModeUi();
    update();
}

// T-048：固定模式呈现载体二选一——壁纸带直绘（默认）优先，带定位/
// 初始化失败或 calendar/bandMode=false 时降级为 DesktopStack Z 序贴底
//（1s 全局守护归位；降级路径保留守护，带模式无守护——带内不可能浮到
// 应用之上，P-031 横跳根因自然消失）
void CalendarWindow::glueIfFixed()
{
    if (m_editMode || !isVisible())
        return;
    const bool bandEnabled = Settings::instance().calendarBandMode();
    if (bandEnabled && ensureBandEntered()) {
        // 带模式：真窗口隐藏（双表示），内容由 DesktopBandHost 呈现。
        // T-048-FIX（P1-1）：activate 只建链不显示——此刻内容仍可见，
        // 先过 showBand 的内容可见性门开带呈现，再隐藏真窗口
        desktopstack::detach(this);
        m_band->showBand();
        m_bandSwitchGuard = true;
        setVisible(false);
        m_bandSwitchGuard = false;
        presentToBand();
        return;
    }
    if (m_band && m_band->active())
        m_band->deactivate(); // bandMode 被关闭：释放带宿主
    desktopstack::attach(this, desktopstack::Layer::Calendar);
}

// ---------- T-048 壁纸带驻入 ----------

bool CalendarWindow::bandActive() const
{
    return m_band && m_band->active();
}

// 懒创建/激活带宿主（首次固定显示时才探测壁纸带；失败返回 false 降级）
bool CalendarWindow::ensureBandEntered()
{
    if (!m_band) {
        m_band = new desktopband::DesktopBandHost(this);
        m_band->setContent(this);
        connect(m_band, &desktopband::DesktopBandHost::interactionRouted, this,
                [this](int msg) {
                    // 移动不重绘（无 hover 反馈需求）；其余交互后内容
                    // 状态可能变化（导航/勾选/选中），重新上传
                    if (msg != int(desktopband::RouteMsg::Move))
                        presentToBand();
                });
        connect(m_band, &desktopband::DesktopBandHost::bandLost, this,
                &CalendarWindow::degradeFromBand);
    }
    if (m_band->active()) {
        m_band->showBand();
        return true;
    }
    return m_band->activate(geometry(), QStringLiteral("桌面日历"),
                            desktopband::BandLayer::Calendar);
}

void CalendarWindow::presentToBand()
{
    if (bandActive() && m_band->bandVisible())
        m_band->presentWidget(this);
}

// 带丢失/重定位失败：释放带宿主，真窗口回归并走贴底路径
void CalendarWindow::degradeFromBand()
{
    qWarning("[band] 日历降级：壁纸带不可用，回退 DesktopStack 贴底路径");
    // T-048-FIX（P1-1）：内容可见性门——带呈现正在进行（bandVisible 在
    // deactivate 前取样）才需要真窗口回归；真隐藏态（✕/开关关闭后宿主已
    // deactivate，此门为兜底）只记状态不 show 真窗口，防 Explorer 重建带
    // 触发的降级把用户已关闭的日历复活
    const bool presenting = m_band && m_band->bandVisible();
    if (m_band) {
        m_band->deactivate();
        m_band->deleteLater();
        m_band = nullptr;
    }
    m_bandSuspended = false;
    if (!m_editMode && (presenting || isVisible())) {
        m_bandSwitchGuard = true;
        setVisible(true);
        m_bandSwitchGuard = false;
        glueIfFixed();
    }
}

// 打字挂起：带内窗口无法接收键盘焦点——需要打字的内联输入（双击格子/
// 底部输入行）临时切回真窗口呈现（同几何，视觉连续），输入结束自动回带
void CalendarWindow::suspendBandForTyping()
{
    if (!bandActive() || m_bandSuspended)
        return;
    m_bandSuspended = true;
    m_band->hideBand();
    m_bandSwitchGuard = true;
    setVisible(true);
    m_bandSwitchGuard = false;
    activateWindow();
}

void CalendarWindow::resumeBandIfSuspended()
{
    if (!m_bandSuspended)
        return;
    m_bandSuspended = false;
    if (m_editMode || !bandActive() || !isVisible())
        return; // 编辑切走/带已失效/真隐藏：呈现路径交给 showEvent/glueIfFixed
    // T-048-FIX（P3-2）：挂起期间逃生口被关闭 → 不回带，真窗口走贴底
    if (!Settings::instance().calendarBandMode()) {
        if (m_band)
            m_band->deactivate();
        desktopstack::attach(this, desktopstack::Layer::Calendar);
        return;
    }
    // T-048-FIX（P1-1）：此刻真窗口可见——先过 showBand 内容可见性门开
    // 带呈现，再隐藏真窗口（顺序不可反，门会拦下"先藏后开"）
    m_band->showBand();
    m_bandSwitchGuard = true;
    setVisible(false);
    m_bandSwitchGuard = false;
    presentToBand();
}

void CalendarWindow::maybeResumeBand()
{
    if (!m_bandSuspended)
        return;
    // 输入控件仍持有焦点（继续打字）→ 不恢复
    const QWidget *focus = QApplication::focusWidget();
    if (focus && (focus == m_input || focus == m_inlineAdd))
        return;
    resumeBandIfSuspended();
}

void CalendarWindow::updateModeUi()
{
    if (m_editMode) {
        m_modeBtn->setText(QStringLiteral("✅"));
        m_modeBtn->setToolTip(QStringLiteral("完成编辑（贴回桌面）"));
        m_modeHint->setVisible(true);
    } else {
        m_modeBtn->setText(QStringLiteral("✏️"));
        m_modeBtn->setToolTip(QStringLiteral("编辑模式（拖动移动 · 拖边缘缩放）"));
        m_modeHint->setVisible(false);
    }
}

// ---------- 设置应用 ----------

void CalendarWindow::applySettings()
{
    applyWindowFlags();
    // 整窗透明度拆分：背景走卡片底色 alpha，文字走文字色 alpha
    m_bg = Settings::instance().calendarBgColor();
    m_bg.setAlpha(255 * Settings::instance().calendarOpacity() / 100);
    m_textOpacity = Settings::instance().calendarTextOpacity();
    m_accent = Settings::instance().calendarAccentColor();
    m_darkText = darkTextFor(m_bg);
    applyTheme();
    refreshDots();
    // T-048-FIX（P3-2）：设置变更后重估呈现载体（calendar/bandMode 逃生
    // 口即时生效）——带呈现中真窗口隐藏，glueIfFixed 会因不可见早退，
    // 关带模式时先恢复可见再二选一；编辑/打字挂起态不重估（回带路径
    // setEditMode→glueIfFixed 与 resumeBandIfSuspended 各自复查 bandMode）
    if (!m_editMode && !m_bandSuspended) {
        if (!Settings::instance().calendarBandMode() && bandActive()) {
            m_bandSwitchGuard = true;
            setVisible(true);
            m_bandSwitchGuard = false;
        }
        glueIfFixed();
    }
    presentToBand(); // T-048：外观变化（透明度/颜色）重新上传带呈现
}

void CalendarWindow::applyTheme()
{
    // 文字系颜色统一乘文字透明度系数（背景透明度只作用于底色/底纹）
    const auto textTone = [this](const QColor &c) {
        QColor r = c;
        r.setAlpha(r.alpha() * m_textOpacity / 100);
        return r;
    };
    const QColor text = textTone(m_darkText ? QColor("#000000") : QColor("#FFFFFF"));
    const QColor subText = textTone(m_darkText ? QColor(0, 0, 0, 130) : QColor(255, 255, 255, 150));
    const QColor line = m_darkText ? QColor(0, 0, 0, 28) : QColor(255, 255, 255, 36);
    const QColor inputBg = m_darkText ? QColor(255, 255, 255, 120) : QColor(0, 0, 0, 60);
    const QColor btnBorder = m_darkText ? QColor(0, 0, 0, 40) : QColor(255, 255, 255, 55);
    const QColor accent = textTone(m_accent);

    // MonthView 配色
    MonthView::Palette p;
    p.bg = m_bg;
    p.text = text;
    p.subText = subText;
    p.dimText = textTone(m_darkText ? QColor(0, 0, 0, 60) : QColor(255, 255, 255, 90));
    p.festival = textTone(QColor(0xE0, 0x53, 0x3D));
    p.accent = accent;
    p.line = line;
    m_monthView->setViewPalette(p);

    // 列表委托配色
    ItemDelegate::Palette dp;
    dp.text = text;
    dp.subText = subText;
    dp.accent = accent;
    static_cast<ItemDelegate *>(m_itemList->itemDelegate())->setDelegatePalette(dp);

    // 控件样式（自定义类一律 ID 选择器，P-022；子控件用类型选择器）
    setStyleSheet(QStringLiteral(
        "QWidget#calendarTitleBar { background: transparent; }"
        "QLabel { color: %1; background: transparent; }"
        "QLabel#calendarTitleLabel { font-weight: bold; font-size: 13px; }"
        "QLabel#calModeHint { font-size: 11px; }"
        "QLabel#calMonthLabel { font-weight: bold; font-size: 14px; }"
        "QLabel#calPanelLabel { font-weight: bold; font-size: 12px; }"
        "QToolButton { background: transparent; border: 1px solid %2;"
        "  border-radius: 6px; color: %1; padding: 2px; font-size: 12px; }"
        "QToolButton:hover { border-color: %3; color: %3; }"
        "QToolButton#calNavBtn { border: none; font-weight: bold; padding: 3px 8px; }"
        "QListWidget { background: transparent; color: %1;"
        "  outline: none; font-size: 12px; }"
        "QListWidget::item { background: transparent; }"
        "QListWidget::item:selected { background: transparent; }"
        "QLineEdit { background: %4; border: 1px solid %2; border-radius: 6px;"
        "  padding: 4px 8px; color: %1; font-size: 12px; }"
        "QLineEdit:focus { border-color: %3; }"
        "QLineEdit#calInlineAdd { background: #FFFFFF; color: #000000;"
        "  border: 2px solid %3; border-radius: 6px; padding: 2px 6px;"
        "  font-size: 12px; }"
        "QSplitter::handle { background: %5; }"
        "QSplitter::handle:hover { background: %3; }")
        .arg(text.name(QColor::HexArgb), btnBorder.name(QColor::HexArgb), accent.name(QColor::HexArgb),
             inputBg.name(QColor::HexArgb), line.name(QColor::HexArgb)));
}

// ---------- 数据刷新 ----------

void CalendarWindow::refreshTitle()
{
    const QDate d = m_monthView->selectedDate();
    m_monthLabel->setText(QStringLiteral("%1年%2月").arg(d.year()).arg(d.month()));
}

void CalendarWindow::refreshLists()
{
    const QDate date = m_monthView->selectedDate();
    const QLocale zhLocale(QLocale::Chinese, QLocale::China);

    m_itemList->clear();
    const QList<CalendarItem> items = m_store->itemsForDate(date);
    for (const CalendarItem &it : items)
        loadItemRow(m_itemList, it);

    const QDate today = QDate::currentDate();
    const QString dayText =
        date == today ? QStringLiteral("今天") : date.toString(QStringLiteral("M月d日"));
    const QString week = zhLocale.toString(date, QStringLiteral("dddd"));
    m_itemLabel->setText(QStringLiteral("日程 · %1 %2（%3 项）")
                             .arg(dayText, week, QString::number(items.size())));
}

void CalendarWindow::loadItemRow(QListWidget *list, const CalendarItem &item)
{
    auto *row = new QListWidgetItem(item.text, list);
    row->setData(itemroles::ItemId, item.id);
    row->setData(itemroles::CategoryColor, m_store->categoryColor(item.categoryId));
    row->setData(itemroles::Important, item.important);
    row->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled
                  | Qt::ItemIsUserCheckable);
    row->setCheckState(item.done ? Qt::Checked : Qt::Unchecked);
    QStringList tips;
    tips << m_store->categoryName(item.categoryId);
    if (item.important)
        tips << QStringLiteral("重要");
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
    presentToBand(); // T-048：带模式下内容变化即重新上传（合并节流）
}

// ---------- 增删改 ----------

void CalendarWindow::addFromInput(const QString &text)
{
    const QString t = text.trimmed();
    if (t.isEmpty())
        return;
    m_store->addItem(m_monthView->selectedDate(), QString(), t, false);
}

// 双击格子：原位弹出内联输入框添加日程
void CalendarWindow::showInlineAdd(const QDate &date)
{
    const QRect cell = m_monthView->cellRectFor(date);
    if (cell.isNull())
        return;
    if (!m_inlineAdd) {
        m_inlineAdd = new QLineEdit(this);
        m_inlineAdd->setObjectName("calInlineAdd");
        m_inlineAdd->installEventFilter(this);
        connect(m_inlineAdd, &QLineEdit::returnPressed, this, [this] {
            const QString text = m_inlineAdd->text().trimmed();
            if (!text.isEmpty())
                m_store->addItem(m_inlineDate, QString(), text, false);
            hideInlineAdd();
        });
    }
    m_inlineDate = date;
    // 盖在格子上（略内缩），垂直居中
    const QPoint tl = m_monthView->mapTo(this, cell.topLeft());
    const int h = 26;
    m_inlineAdd->setGeometry(QRect(tl + QPoint(5, (cell.height() - h) / 2),
                                   QSize(cell.width() - 10, h)));
    m_inlineAdd->clear();
    m_inlineAdd->show();
    m_inlineAdd->raise();
    // 固定模式下点击不激活（防抬升），打字需要键盘焦点——用户双击
    // 是明确意图，此处临时唤起窗口；输入结束守护定时器自动归位。
    // T-048：带模式下带窗无法接键盘——打字挂起临时切回真窗口呈现
    if (!m_editMode) {
        if (bandActive())
            suspendBandForTyping();
        else
            activateWindow();
    }
    m_inlineAdd->setFocus();
}

void CalendarWindow::hideInlineAdd()
{
    if (m_inlineAdd && m_inlineAdd->isVisible())
        m_inlineAdd->hide();
    maybeResumeBand(); // T-048：内联输入结束（回车/Esc/失焦/换月）即回带
}

bool CalendarWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_inlineAdd) {
        if (event->type() == QEvent::KeyPress) {
            auto *ke = static_cast<QKeyEvent *>(event);
            if (ke->key() == Qt::Key_Escape) {
                hideInlineAdd();
                return true;
            }
        } else if (event->type() == QEvent::FocusOut) {
            hideInlineAdd();
        }
    } else if (watched == m_input && event->type() == QEvent::MouseButtonPress) {
        // 固定模式下点击输入行：显式唤起窗口以获得键盘焦点
        //（MA_NOACTIVATE 抑制了点击激活）。T-048：带模式走打字挂起
        if (!m_editMode) {
            if (bandActive())
                suspendBandForTyping();
            else
                activateWindow();
        }
    } else if (watched == m_input && event->type() == QEvent::FocusOut) {
        maybeResumeBand(); // T-048：底部输入行失焦即回带
    }
    return QWidget::eventFilter(watched, event);
}

void CalendarWindow::editItem(QListWidgetItem *row)
{
    if (!row)
        return;
    const QString id = row->data(itemroles::ItemId).toString();
    CalendarItem item;
    if (!findItem(m_store, id, &item))
        return;
    ItemEditDialog dlg(m_store, id, this);
    if (dlg.exec() == QDialog::Accepted && !dlg.text().isEmpty())
        m_store->updateItem(id, dlg.text(), dlg.categoryId(), dlg.important());
}

void CalendarWindow::removeItem(QListWidgetItem *row)
{
    if (row)
        m_store->removeItem(row->data(itemroles::ItemId).toString());
}

void CalendarWindow::toggleDone(QListWidgetItem *row)
{
    if (!row)
        return;
    const QString id = row->data(itemroles::ItemId).toString();
    CalendarItem item;
    if (findItem(m_store, id, &item))
        m_store->setDone(id, !item.done);
}

void CalendarWindow::toggleImportant(QListWidgetItem *row)
{
    if (!row)
        return;
    const QString id = row->data(itemroles::ItemId).toString();
    CalendarItem item;
    if (findItem(m_store, id, &item))
        m_store->updateItem(id, item.text, item.categoryId, !item.important);
}

void CalendarWindow::moveItemRelative(QListWidgetItem *row, int delta)
{
    if (!row)
        return;
    m_store->moveItem(row->data(itemroles::ItemId).toString(), delta);
}

void CalendarWindow::showContextMenu(const QPoint &globalPos)
{
    QListWidgetItem *row = m_itemList->currentItem();
    QMenu menu(m_itemList);
    if (row) {
        const QString id = row->data(itemroles::ItemId).toString();
        CalendarItem item;
        if (!findItem(m_store, id, &item))
            return;

        menu.addAction(item.done ? QStringLiteral("标记为未完成")
                                 : QStringLiteral("标记为完成"),
                       [this, row] { toggleDone(row); });
        menu.addAction(item.important ? QStringLiteral("取消重要★")
                                      : QStringLiteral("标记重要★"),
                       [this, row] { toggleImportant(row); });
        menu.addAction(QStringLiteral("编辑…"), [this, row] { editItem(row); });
        menu.addSeparator();
        menu.addAction(QStringLiteral("上移"),
                       [this, row] { moveItemRelative(row, -1); });
        menu.addAction(QStringLiteral("下移"),
                       [this, row] { moveItemRelative(row, 1); });
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
        menu.addAction(QStringLiteral("删除"), [this, row] { removeItem(row); });
    } else {
        // 空白处右键：快速新增
        menu.addAction(QStringLiteral("新增日程…"), [this] {
            ItemEditDialog dlg(m_store, QString(), this);
            if (dlg.exec() == QDialog::Accepted && !dlg.text().isEmpty())
                m_store->addItem(m_monthView->selectedDate(), dlg.categoryId(),
                                 dlg.text(), dlg.important());
        });
    }
    menu.exec(globalPos);
}

void CalendarWindow::syncReorder()
{
    QStringList ids;
    for (int i = 0; i < m_itemList->count(); ++i)
        ids << m_itemList->item(i)->data(itemroles::ItemId).toString();
    m_store->reorderItems(m_monthView->selectedDate(), ids);
}

// ---------- 窗口事件 ----------

void CalendarWindow::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    // 固定模式：显示完成后驻带/贴底（延迟到事件循环，避开 show 过程中的
    // 原生窗口状态变化窗口期）。T-048：打字挂起期真窗口临时可见，跳过
    //（恢复由 resumeBandIfSuspended 负责）
    if (!m_editMode && !m_bandSuspended)
        QTimer::singleShot(0, this, [this] { glueIfFixed(); });
}

void CalendarWindow::hideEvent(QHideEvent *event)
{
    // 几何与分栏状态持久化（窗口始终为顶层，几何永远有效）
    Settings::instance().setCalendarGeometry(saveGeometry());
    Settings::instance().setCalendarSplitter(m_hSplit->saveState().toBase64());
    desktopstack::detach(this);
    // T-048：真隐藏（设置开关/✕）时连带呈现一并收起；双表示切换
    //（m_bandSwitchGuard）不收——带窗口继续承载显示；真隐藏同时清
    // 打字挂起态，下次显示走正常驻带流程
    if (m_bandSwitchGuard)
        return QWidget::hideEvent(event);
    m_bandSuspended = false;
    // T-048-FIX（P1-1）：真隐藏（✕/开关关闭/shutdown，无 guard 的
    // hideEvent）→ 宿主 deactivate()（释放 DComp、注销路由、停 heal），
    // 比 hideBand 彻底——否则 m_active 挂着、heal 照跑，Explorer 重建带
    // 时整链重建 + 恢复呈现，把用户已 ✕ 关闭的日历幽灵复活
    if (m_band && m_band->active())
        m_band->deactivate();
    QWidget::hideEvent(event);
}

// T-048：打字挂起中失焦（点外部应用）即回带——挂起态的真窗口是普通
// 浮动窗，不应久留
void CalendarWindow::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::ActivationChange && !m_editMode
        && !isActiveWindow())
        resumeBandIfSuspended();
    QWidget::changeEvent(event);
}

// 固定模式：空白处右键转发给桌面（弹出系统桌面右键菜单），让日历
// 像真的贴在桌面上一样；条目列表的右键由列表自身处理（条目操作），
// 输入框/按钮等子控件各自的菜单不受影响。编辑模式维持默认行为。
void CalendarWindow::contextMenuEvent(QContextMenuEvent *event)
{
    if (m_editMode) {
        QWidget::contextMenuEvent(event);
        return;
    }
#ifdef Q_OS_WIN
    // 桌面图标层（DefView）响应 WM_CONTEXTMENU 弹出系统桌面菜单；
    // lParam 为屏幕物理坐标
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

    // 编辑模式：强调色虚线框 + 半透明遮罩提示
    if (m_editMode) {
        QPen dashPen(m_accent);
        dashPen.setWidth(2);
        dashPen.setStyle(Qt::DashLine);
        p.setPen(dashPen);
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(rect().adjusted(1, 1, -2, -2), radius, radius);
    }
}

bool CalendarWindow::nativeEvent(const QByteArray &eventType, void *message, qintptr *result)
{
#ifdef Q_OS_WIN
    if (eventType == "windows_generic_MSG") {
        MSG *msg = static_cast<MSG *>(message);
        if (msg->message == WM_MOUSEACTIVATE) {
            // 固定模式：点击不激活窗口（Windows 默认把被点的窗口提到
            // 前面——贴底日历必须抑制，否则会盖住正常应用窗口，随守护
            // 定时器回落形成横跳）。需要键盘输入的场景由代码显式
            // activateWindow() 临时唤起
            if (!m_editMode) {
                *result = MA_NOACTIVATE;
                return true;
            }
        }
        if (msg->message == WM_NCHITTEST) {
            // 固定模式：窗口完全锁定，全部按客户区处理（无缩放/拖动）
            if (!m_editMode) {
                *result = HTCLIENT;
                return true;
            }
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
