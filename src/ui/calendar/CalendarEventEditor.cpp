#include "CalendarEventEditor.h"

#include "CalendarPaintUtils.h" // 圆角矩形路径（S-03 模块内收敛，原 editorRoundPath）
#include "core/calendar/CalendarTaskStore.h"
#include "core/calendar/ChineseCalendar.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QToolTip>

namespace poptools {

CalendarEventEditor::CalendarEventEditor(const QDate &date, CalendarTaskStore *store,
                                         const QPoint &pos, QWidget *parent)
    : QWidget(parent, Qt::Dialog | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
    , m_date(date)
    , m_store(store)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowFlag(Qt::Tool, false);
    setMouseTracking(true);
    resize(kEditorSize); // S-07：与面板定位共用同一常量
    move(pos);

    m_text = new QPlainTextEdit(this);
    m_text->setObjectName("calendarEventEdit");
    m_text->setFrameShape(QPlainTextEdit::NoFrame);
    m_text->setStyleSheet(
        QStringLiteral("QPlainTextEdit#calendarEventEdit { background: #16181E; "
                       "color: #F0F0F0; border: none; font: 15px 'SimSun'; "
                       "selection-background-color: #2B6FF2; }"));
    m_text->setGeometry(14, 48, 352, 280);
    m_text->setPlainText([this, store, date] {
        const QVector<CalendarTaskItem> tasks = store->get(date);
        QStringList lines;
        lines.reserve(tasks.size());
        for (const auto &t : tasks)
            lines << t.text;
        return lines.join(QLatin1Char('\n'));
    }());

    connect(m_text, &QPlainTextEdit::textChanged, this, [this] { update(); });
}

void CalendarEventEditor::paintEvent(QPaintEvent *)
{
    QPainter g(this);
    g.setRenderHint(QPainter::Antialiasing);
    g.setCompositionMode(QPainter::CompositionMode_Source);
    g.fillRect(rect(), QColor(26, 28, 34));
    g.setCompositionMode(QPainter::CompositionMode_SourceOver);
    g.setPen(QPen(QColor(255, 255, 255, 46), 1));
    g.setBrush(Qt::NoBrush);
    g.drawRect(QRectF(0.5, 0.5, width() - 1, height() - 1));

    QFont titleFont;
    titleFont.setFamilies({ QStringLiteral("SimSun"), QStringLiteral("Microsoft YaHei UI") });
    titleFont.setPixelSize(15);
    titleFont.setBold(true);
    g.setFont(titleFont);
    g.setPen(QColor(245, 245, 245));
    g.drawText(QPointF(16, 15 + 14),
               QStringLiteral("%1年%2月%3日 %4")
                   .arg(m_date.year())
                   .arg(m_date.month())
                   .arg(m_date.day())
                   .arg(ChineseCalendar::weekdayName(m_date)));

    m_checkRect = QRect(width() - 42, 10, 28, 28);
    g.fillPath(roundedPath(QRectF(m_checkRect), 5), QColor(255, 255, 255, 60));
    QPen checkPen(QColor(240, 166, 72), 2);
    checkPen.setCapStyle(Qt::RoundCap);
    checkPen.setJoinStyle(Qt::RoundJoin);
    g.setPen(checkPen);
    g.drawPolyline(QVector<QPoint>{ QPoint(m_checkRect.left() + 7, m_checkRect.top() + 14),
                                    QPoint(m_checkRect.left() + 12, m_checkRect.top() + 19),
                                    QPoint(m_checkRect.left() + 21, m_checkRect.top() + 8) });

    // 工具条
    const int toolbarY = height() - 110;
    g.setPen(QPen(QColor(255, 255, 255, 34), 1));
    g.drawLine(14, toolbarY - 8, width() - 14, toolbarY - 8);
    for (int i = 0; i < 5; ++i) {
        const QRect r(14 + i * 36, toolbarY, 28, 28);
        m_toolRects[i] = r;
        if (i == m_hoverTool)
            g.fillPath(roundedPath(QRectF(r), 5), QColor(255, 255, 255, 42));
        drawToolIcon(&g, i, r);
    }

    QFont smallFont;
    smallFont.setFamilies({ QStringLiteral("Microsoft YaHei UI") });
    smallFont.setPixelSize(11);
    g.setFont(smallFont);
    g.setPen(QColor(150, 150, 150));
    g.drawText(QPoint(16, height() - 58 + 11), QStringLiteral("数据保存在本机"));
}

int CalendarEventEditor::hitTool(const QPoint &p) const
{
    for (int i = 0; i < 5; ++i)
        if (m_toolRects[i].contains(p))
            return i;
    return -1;
}

void CalendarEventEditor::handleTool(int index)
{
    switch (index) {
        case 0:
            insertTime();
            break;
        case 1:
            QToolTip::showText(mapToGlobal(m_toolRects[index].topLeft()),
                               QStringLiteral("重复：不重复（复刻版仅支持单日）"), this);
            break;
        case 2:
            QToolTip::showText(mapToGlobal(m_toolRects[index].topLeft()),
                               QStringLiteral("提醒：无"), this);
            break;
        case 3:
            QToolTip::showText(mapToGlobal(m_toolRects[index].topLeft()),
                               QStringLiteral("地点：未设置"), this);
            break;
        case 4:
            QToolTip::showText(mapToGlobal(m_toolRects[index].topLeft()),
                               QStringLiteral("颜色：默认"), this);
            break;
        default:
            break;
    }
}

void CalendarEventEditor::insertTime()
{
    // 与 rlqd 一致：在光标所在行行首插入 "09:00 " 前缀
    QTextCursor cur = m_text->textCursor();
    const int pos = cur.position();
    const QString text = m_text->toPlainText();
    int lineStart = text.lastIndexOf(QLatin1Char('\n'), qMax(0, pos - 1)) + 1;
    if (pos == 0)
        lineStart = 0;
    const QString prefix = QStringLiteral("09:00 ");
    cur.setPosition(lineStart);
    cur.insertText(prefix);
    cur.setPosition(lineStart + prefix.length());
    m_text->setTextCursor(cur);
    m_text->setFocus();
}

void CalendarEventEditor::drawToolIcon(QPainter *g, int index, const QRect &r)
{
    QPen pen(QColor(220, 220, 220), 1.4);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    g->setPen(pen);
    g->setBrush(Qt::NoBrush);
    const qreal cx = r.left() + r.width() / 2.0, cy = r.top() + r.height() / 2.0;
    switch (index) {
        case 0: // 时间
            g->drawEllipse(QPointF(cx, cy), 7, 7);
            g->drawLine(QPointF(cx, cy - 4), QPointF(cx, cy));
            g->drawLine(QPointF(cx, cy), QPointF(cx + 4, cy + 2));
            break;
        case 1: // 重复
            g->drawArc(QRectF(cx - 7, cy - 7, 14, 14), 40 * 16, 250 * 16);
            g->drawLine(QPointF(cx + 5, cy - 8), QPointF(cx + 8, cy - 5));
            g->drawLine(QPointF(cx + 8, cy - 5), QPointF(cx + 4, cy - 3));
            break;
        case 2: // 提醒
            g->drawArc(QRectF(cx - 6, cy - 7, 12, 12), 180 * 16, 180 * 16);
            g->drawLine(QPointF(cx - 6, cy), QPointF(cx - 6, cy + 5));
            g->drawLine(QPointF(cx + 6, cy), QPointF(cx + 6, cy + 5));
            g->drawLine(QPointF(cx - 8, cy + 5), QPointF(cx + 8, cy + 5));
            g->drawLine(QPointF(cx, cy + 8), QPointF(cx, cy + 8.1));
            break;
        case 3: // 地点
            g->drawEllipse(QPointF(cx, cy - 3), 5, 5);
            g->drawPolyline(QVector<QPointF>{ QPointF(cx + 4, cy - 1), QPointF(cx, cy + 9),
                                              QPointF(cx - 4, cy - 1) });
            break;
        case 4: { // 颜色
            g->drawEllipse(QPointF(cx, cy), 7, 7);
            g->setBrush(QColor(220, 220, 220));
            g->drawEllipse(QPointF(cx - 2.8, cy - 2.8), 1.2, 1.2);
            g->drawEllipse(QPointF(cx + 2.6, cy - 2.8), 1.2, 1.2);
            g->drawEllipse(QPointF(cx - 2.8, cy + 2.6), 1.2, 1.2);
            g->setBrush(Qt::NoBrush);
            break;
        }
        default:
            break;
    }
}

void CalendarEventEditor::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        saveAndClose();
        return;
    }
    if (event->modifiers() & Qt::ControlModifier && event->key() == Qt::Key_S) {
        saveAndClose();
        return;
    }
    QWidget::keyPressEvent(event);
}

void CalendarEventEditor::mouseMoveEvent(QMouseEvent *event)
{
    const int h = hitTool(event->pos());
    if (h != m_hoverTool) {
        m_hoverTool = h;
        update();
    }
}

void CalendarEventEditor::mousePressEvent(QMouseEvent *event)
{
    if (m_checkRect.contains(event->pos())) {
        saveAndClose();
        return;
    }
    const int t = hitTool(event->pos());
    if (t >= 0)
        handleTool(t);
}

bool CalendarEventEditor::event(QEvent *event)
{
    if (event->type() == QEvent::WindowDeactivate && !m_closing) {
        saveAndClose();
        return true;
    }
    return QWidget::event(event);
}

void CalendarEventEditor::saveAndClose()
{
    if (m_closing)
        return;
    m_closing = true;
    QVector<CalendarTaskItem> tasks;
    const QStringList lines = m_text->toPlainText().replace(QLatin1Char('\r'), QString())
                                  .split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        const QString s = line.trimmed();
        if (s.isEmpty())
            continue;
        CalendarTaskItem item;
        item.text = s;
        tasks.append(item);
    }
    m_store->set(m_date, tasks);
    close();
}

} // namespace poptools
