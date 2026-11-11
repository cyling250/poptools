#include "CalendarMiniWidget.h"

#include "ui/desktopband/DesktopBandHost.h"
#include "ui/shared/DesktopStack.h"
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

    // 跨天自动刷新内容（常驻显示，内容由日期驱动）。
    // T-048：驻带呈现中 isVisible()==false（真窗口隐藏），视为显示中
    if (m_loadedForDate != today) {
        m_loadedForDate = today;
    } else if (!isVisible() && !bandActive()) {
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
    presentToBand(); // T-048：内容/高度变化重新上传（合并节流）
}

void CalendarMiniWidget::applySettings()
{
    m_bg = Settings::instance().calendarBgColor();
    m_bg.setAlpha(255 * Settings::instance().calendarOpacity() / 100);
    m_textOpacity = Settings::instance().calendarTextOpacity();
    m_accent = Settings::instance().calendarAccentColor();
    m_darkText = darkTextFor(m_bg);
    applyTheme();
    // T-048-FIX（P3-2）：设置变更后重估呈现载体（calendar/bandMode 逃生
    // 口即时生效）——带呈现中真窗口隐藏，关带模式先恢复可见再走
    // glueIfFixed 二选一；未显示时 glueIfFixed 自行早退
    if (!Settings::instance().calendarBandMode() && bandActive()) {
        m_bandSwitchGuard = true;
        setVisible(true);
        m_bandSwitchGuard = false;
    }
    glueIfFixed();
    presentToBand(); // T-048：外观变化重新上传
}

void CalendarMiniWidget::applyTheme()
{
    const QColor text = [this] {
        QColor r = m_darkText ? QColor("#000000") : QColor("#FFFFFF");
        r.setAlpha(r.alpha() * m_textOpacity / 100);
        return r;
    }();
    const QColor btnBorder = m_darkText ? QColor(0, 0, 0, 40) : QColor(255, 255, 255, 55);

    ItemDelegate::Palette dp;
    dp.text = text;
    dp.subText = [this] {
        QColor r = m_darkText ? QColor(0, 0, 0, 130) : QColor(255, 255, 255, 150);
        r.setAlpha(r.alpha() * m_textOpacity / 100);
        return r;
    }();
    dp.accent = [this] {
        QColor r = m_accent;
        r.setAlpha(r.alpha() * m_textOpacity / 100);
        return r;
    }();
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
        .arg(text.name(QColor::HexArgb), btnBorder.name(QColor::HexArgb),
             m_accent.name(QColor::HexArgb)));
}

void CalendarMiniWidget::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    // 驻带/贴底（T-048：驻带优先，显示完成后延迟执行）
    QTimer::singleShot(0, this, [this] { glueIfFixed(); });
}

void CalendarMiniWidget::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
    // 退出守护表（R3 #19）：隐藏后不再需要 Z 序守护——attach 与 detach
    // 配对，否则守护定时器为不可见窗口永不停表（每秒一次 FindWindowW 空转）
    desktopstack::detach(this);
    // T-048：真隐藏（✕/展开切换）连带呈现一并收起；双表示切换不收
    if (m_bandSwitchGuard)
        return;
    // T-048-FIX（P1-1）：真隐藏 → 宿主 deactivate()（释放 DComp、注销
    // 路由、停 heal）——只 hideBand 会留下 m_active 挂着 + heal 照跑，
    // Explorer 重建带时把用户已 ✕ 关闭的迷你条幽灵复活
    if (m_band && m_band->active())
        m_band->deactivate();
}

// ---------- T-048 壁纸带驻入 ----------

bool CalendarMiniWidget::bandActive() const
{
    return m_band && m_band->active();
}

// 驻带优先：bandMode 开启且带就绪 → 真窗口隐藏（双表示），内容由
// DesktopBandHost 呈现；失败降级为 DesktopStack 贴底
void CalendarMiniWidget::glueIfFixed()
{
    if (!isVisible())
        return;
    if (Settings::instance().calendarBandMode() && ensureBandEntered()) {
        // T-048-FIX（P1-1）：内容仍可见时先过 showBand 的内容可见性门开
        // 带呈现（activate 只建链不显示），再隐藏真窗口
        m_band->showBand();
        m_bandSwitchGuard = true;
        setVisible(false);
        m_bandSwitchGuard = false;
        presentToBand();
        return;
    }
    if (m_band && m_band->active())
        m_band->deactivate();
    desktopstack::attach(this, desktopstack::Layer::Calendar);
}

bool CalendarMiniWidget::ensureBandEntered()
{
    if (!m_band) {
        m_band = new desktopband::DesktopBandHost(this);
        m_band->setContent(this);
        connect(m_band, &desktopband::DesktopBandHost::bandLost, this,
                &CalendarMiniWidget::degradeFromBand);
    }
    if (m_band->active()) {
        m_band->showBand();
        return true;
    }
    return m_band->activate(geometry(), QStringLiteral("今日待办"),
                            desktopband::BandLayer::Calendar);
}

void CalendarMiniWidget::presentToBand()
{
    if (bandActive() && m_band->bandVisible())
        m_band->presentWidget(this);
}

void CalendarMiniWidget::degradeFromBand()
{
    qWarning("[band] 迷你条降级：壁纸带不可用，回退 DesktopStack 贴底路径");
    // T-048-FIX（P1-1）：内容可见性门——带呈现进行中才回归真窗口；
    // 真隐藏态（✕ 后宿主已 deactivate，此门为兜底）只记状态不 show，
    // 防 Explorer 重建带触发的降级把已关闭的迷你条复活
    const bool presenting = m_band && m_band->bandVisible();
    if (m_band) {
        m_band->deactivate();
        m_band->deleteLater();
        m_band = nullptr;
    }
    if (presenting || isVisible()) {
        m_bandSwitchGuard = true;
        setVisible(true);
        m_bandSwitchGuard = false;
        glueIfFixed();
    }
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
