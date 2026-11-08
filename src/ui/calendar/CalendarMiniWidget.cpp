#include "CalendarMiniWidget.h"

#include "DesktopPin.h"
#include "ItemDelegate.h"
#include "core/calendar/CalendarStore.h"
#include "core/settings/Settings.h"

#include <QGuiApplication>
#include <QIcon>
#include <QLabel>
#include <QListWidget>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#ifdef Q_OS_WIN
#include <windows.h>
#include <windowsx.h>
#endif

namespace poptools {

namespace {
constexpr int kMiniWidth = 250;
constexpr int kMiniMaxRows = 8;

// 由背景色亮度决定文字用深色还是浅色（与 CalendarWindow 同规则）
bool darkTextFor(const QColor &bg)
{
    const qreal lum = 0.299 * bg.redF() + 0.587 * bg.greenF() + 0.114 * bg.blueF();
    return lum > 0.6;
}
} // namespace

CalendarMiniWidget::CalendarMiniWidget(CalendarStore *store, QWidget *parent)
    : QWidget(parent)
    , m_store(store)
{
    setObjectName("calendarMini");
    setWindowIcon(QIcon(":/app_icon.svg"));
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setFixedWidth(kMiniWidth);

    buildUi();
    applySettings();
    refresh();

    connect(m_store, &CalendarStore::changed, this, &CalendarMiniWidget::refresh);
}

void CalendarMiniWidget::buildUi()
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(1, 1, 1, 1);
    lay->setSpacing(2);

    auto *header = new QWidget(this);
    header->setObjectName("miniHeader");
    auto *hlay = new QHBoxLayout(header);
    hlay->setContentsMargins(12, 4, 6, 4);
    hlay->setSpacing(4);

    m_dateLabel = new QLabel(header);
    m_dateLabel->setObjectName("miniDateLabel");

    auto *expandBtn = new QToolButton(header);
    expandBtn->setObjectName("miniBtn");
    expandBtn->setText(QStringLiteral("⤢"));
    expandBtn->setToolTip(QStringLiteral("展开完整日历"));
    expandBtn->setFixedSize(24, 22);
    auto *closeBtn = new QToolButton(header);
    closeBtn->setObjectName("miniBtn");
    closeBtn->setText(QStringLiteral("✕"));
    closeBtn->setToolTip(QStringLiteral("关闭"));
    closeBtn->setFixedSize(24, 22);
    connect(expandBtn, &QToolButton::clicked, this, &CalendarMiniWidget::expandRequested);
    connect(closeBtn, &QToolButton::clicked, this, &QWidget::hide);

    hlay->addWidget(m_dateLabel, 1);
    hlay->addWidget(expandBtn);
    hlay->addWidget(closeBtn);
    lay->addWidget(header);

    m_list = new QListWidget(this);
    m_list->setObjectName("miniList");
    m_list->setFrameShape(QFrame::NoFrame);
    m_list->setSelectionMode(QAbstractItemView::NoSelection);
    m_list->setItemDelegate(new ItemDelegate(m_list));
    m_list->setContextMenuPolicy(Qt::NoContextMenu);
    connect(m_list, &QListWidget::itemClicked, this, [this](QListWidgetItem *row) {
        // 点击左侧勾选区（或整行）切换完成状态
        const QString id = row->data(itemroles::ItemId).toString();
        for (const CalendarItem &it : m_store->items()) {
            if (it.id == id) {
                m_store->setDone(id, !it.done);
                break;
            }
        }
    });
    lay->addWidget(m_list, 1);

    applyTheme();
}

void CalendarMiniWidget::refresh()
{
    const QDate today = QDate::currentDate();

    // 跨天自动刷新内容（常驻显示，内容由日期驱动）
    if (m_loadedForDate != today) {
        m_loadedForDate = today;
    } else if (!isVisible()) {
        return; // 未显示时无需频繁重排
    }

    const QLocale zhLocale(QLocale::Chinese, QLocale::China);
    m_dateLabel->setText(QStringLiteral("%1 %2 · 待办")
                             .arg(today.toString(QStringLiteral("M月d日")),
                                  zhLocale.toString(today, QStringLiteral("dddd"))));

    m_list->clear();
    const QList<CalendarItem> items = m_store->itemsForDate(today);
    // 未完成在前，完成垫底；各按存储顺序
    for (const CalendarItem &it : items) {
        if (it.done)
            continue;
        auto *row = new QListWidgetItem(it.text, m_list);
        row->setData(itemroles::ItemId, it.id);
        row->setData(itemroles::CategoryColor, m_store->categoryColor(it.categoryId));
        row->setData(itemroles::Important, it.important);
        row->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
        row->setCheckState(Qt::Unchecked);
    }
    for (const CalendarItem &it : items) {
        if (!it.done)
            continue;
        auto *row = new QListWidgetItem(it.text, m_list);
        row->setData(itemroles::ItemId, it.id);
        row->setData(itemroles::CategoryColor, m_store->categoryColor(it.categoryId));
        row->setData(itemroles::Important, it.important);
        row->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
        row->setCheckState(Qt::Checked);
    }

    if (items.isEmpty()) {
        auto *row = new QListWidgetItem(QStringLiteral("今日无日程 🌿"), m_list);
        row->setFlags(Qt::ItemIsEnabled);
        row->setSizeHint(QSize(0, 40));
    }

    // 高度随内容自适应（上限 8 行）
    const int rows = qMin(m_list->count(), kMiniMaxRows);
    const int listH = rows * 30 + 6;
    m_list->setFixedHeight(qMax(44, listH));
    adjustSize();
}

void CalendarMiniWidget::applySettings()
{
    setWindowOpacity(Settings::instance().calendarOpacity() / 100.0);
    m_bg = Settings::instance().calendarBgColor();
    m_accent = Settings::instance().calendarAccentColor();
    m_darkText = darkTextFor(m_bg);
    applyTheme();
}

void CalendarMiniWidget::applyTheme()
{
    const QColor text = m_darkText ? QColor("#000000") : QColor("#FFFFFF");
    const QColor btnBorder = m_darkText ? QColor(0, 0, 0, 40) : QColor(255, 255, 255, 55);

    ItemDelegate::Palette dp;
    dp.text = text;
    dp.subText = m_darkText ? QColor(0, 0, 0, 130) : QColor(255, 255, 255, 150);
    dp.accent = m_accent;
    static_cast<ItemDelegate *>(m_list->itemDelegate())->setDelegatePalette(dp);

    setStyleSheet(QStringLiteral(
        "QLabel { color: %1; background: transparent; }"
        "QLabel#miniDateLabel { font-weight: bold; font-size: 12px; }"
        "QToolButton { background: transparent; border: 1px solid %2;"
        "  border-radius: 5px; color: %1; padding: 1px; font-size: 11px; }"
        "QToolButton:hover { border-color: %3; color: %3; }"
        "QListWidget { background: transparent; color: %1;"
        "  outline: none; font-size: 12px; }"
        "QListWidget::item { background: transparent; }")
        .arg(text.name(QColor::HexRgb), btnBorder.name(QColor::HexRgb),
             m_accent.name(QColor::HexRgb)));
}

void CalendarMiniWidget::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    // 贴底（顶层 + Z 序紧贴桌面，显示完成后延迟执行）+ 1s 守护
    if (!m_zGuard) {
        m_zGuard = new QTimer(this);
        m_zGuard->setInterval(1000);
        connect(m_zGuard, &QTimer::timeout, this,
                [this] { desktoppin::glueToDesktop(this); });
    }
    QTimer::singleShot(0, this, [this] {
        desktoppin::glueToDesktop(this);
        m_zGuard->start();
    });
}

void CalendarMiniWidget::paintEvent(QPaintEvent * /*event*/)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QPainterPath path;
    path.addRoundedRect(rect().adjusted(0, 0, -1, -1), 10, 10);
    p.fillPath(path, m_bg);
    const QColor border = m_darkText ? QColor(0, 0, 0, 36) : QColor(255, 255, 255, 46);
    p.setPen(QPen(border, 1));
    p.setBrush(Qt::NoBrush);
    p.drawPath(path);
}

// 标题区拖动（无边框窗口的移动）；列表区保持客户端事件
bool CalendarMiniWidget::nativeEvent(const QByteArray &eventType, void *message,
                                     qintptr *result)
{
#ifdef Q_OS_WIN
    if (eventType == "windows_generic_MSG") {
        MSG *msg = static_cast<MSG *>(message);
        if (msg->message == WM_MOUSEACTIVATE) {
            // 贴底小组件：点击不激活（避免抬升到正常窗口之上）
            *result = MA_NOACTIVATE;
            return true;
        }
        if (msg->message == WM_NCHITTEST) {
            const qreal dpr = devicePixelRatioF();
            const QPoint globalPhys(GET_X_LPARAM(msg->lParam), GET_Y_LPARAM(msg->lParam));
            const QPointF local = mapFromGlobal(QPointF(globalPhys) / dpr);
            if (local.y() <= 30 && !qobject_cast<QToolButton *>(childAt(local.toPoint()))) {
                *result = HTCAPTION;
                return true;
            }
        }
    }
#endif
    return QWidget::nativeEvent(eventType, message, result);
}

void CalendarMiniWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
    emit expandRequested();
    QWidget::mouseDoubleClickEvent(event);
}

} // namespace poptools
