#include "CalendarEventEditor.h"

#include "CalendarPaintUtils.h" // 圆角矩形路径（S-03 模块内收敛，原 editorRoundPath）
#include "core/calendar/CalendarTaskStore.h"
#include "core/calendar/ChineseCalendar.h"
#include "ui/ThemeColors.h" // overlay::warmAccent（T-071 橙收敛）
#include "ui/theming/FluentTheme.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QProcess>
#include <QStandardPaths>

namespace poptools {

void openDigestInNotepad(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return;
    const QString base = QFileInfo(path).completeBaseName(); // 2026-11-18 / 2026-11
    const QString txt = QStandardPaths::writableLocation(QStandardPaths::TempLocation)
                        + QStringLiteral("/PopTools工作摘要_") + base
                        + QStringLiteral(".txt");
    QFile out(txt);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;
    out.write(f.readAll());
    out.close();
    QProcess::startDetached(QStringLiteral("notepad.exe"), {txt});
}

CalendarEventEditor::CalendarEventEditor(const QDate &date, CalendarTaskStore *store,
                                         const QPoint &pos, const QString &digestPath,
                                         QWidget *parent)
    : QWidget(parent, Qt::Dialog | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
    , m_date(date)
    , m_store(store)
    , m_digestPath(digestPath)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowFlag(Qt::Tool, false);
    resize(kEditorSize); // S-07：与面板定位共用同一常量（T-072：340，随工具条/页脚撤除收缩）
    move(pos);

    m_text = new QPlainTextEdit(this);
    m_text->setObjectName("calendarEventEdit");
    m_text->setFrameShape(QPlainTextEdit::NoFrame);
    // 2026-11-19 收编：原硬编码十六进制 → Fluent token 常暗套（本编辑器浮在
    // 贴桌面玻璃日历之上，属恒暗玻璃家族语境，不随系统明暗——对齐 ToolBar 先例）
    {
        const FluentTokens t = fluentTheme(true);
        m_text->setStyleSheet(
            QStringLiteral("QPlainTextEdit#calendarEventEdit { background: %1; "
                           "color: %2; border: none; font: 15px 'SimSun'; "
                           "selection-background-color: %3; }")
                .arg(t.bgSubtle, t.textPrimary, t.accent));
    }
    m_text->setGeometry(14, 48, 352, 280);
    m_text->setPlainText([this, store, date] {
        const QVector<CalendarTaskItem> tasks = store->get(date);
        QStringList lines;
        lines.reserve(tasks.size());
        for (const auto &t : tasks)
            lines << t.text;
        return lines.join(QLatin1Char('\n'));
    }());

    // 文本变化只发生在子控件自身绘制内，父窗绘制（边框/标题）不依赖文本内容
    // ——不每键整窗 update()（T-071 收编）
}

void CalendarEventEditor::paintEvent(QPaintEvent *)
{
    // 常暗玻璃家族语境：底/描边取 Fluent token 深色套（描边保留半透明白
    // 的玻璃语义——alpha 色不入 token 表，见 FluentTheme.h 使用纪律）
    const FluentTokens t = fluentTheme(true);
    const QColor border{QColor(t.textPrimary)};
    QPainter g(this);
    g.setRenderHint(QPainter::Antialiasing);
    g.setCompositionMode(QPainter::CompositionMode_Source);
    g.fillRect(rect(), QColor(t.bgBase));
    g.setCompositionMode(QPainter::CompositionMode_SourceOver);
    g.setPen(QPen(QColor(border.red(), border.green(), border.blue(), 46), 1));
    g.setBrush(Qt::NoBrush);
    g.drawRect(QRectF(0.5, 0.5, width() - 1, height() - 1));

    QFont titleFont;
    titleFont.setFamilies({ QStringLiteral("SimSun"), QStringLiteral("Microsoft YaHei UI") });
    titleFont.setPixelSize(15);
    titleFont.setBold(true);
    g.setFont(titleFont);
    g.setPen(QColor(t.textPrimary)); // T-071 收编：字面量 (245,245,245) → 深套 token
    // 基线经 ascent 锚定（P-064 口径：禁"字号+手算偏移"，DPR 下会漂）
    g.drawText(QPoint(16, 15 + QFontMetrics(titleFont).ascent()),
               QStringLiteral("%1年%2月%3日 %4")
                   .arg(m_date.year())
                   .arg(m_date.month())
                   .arg(m_date.day())
                   .arg(ChineseCalendar::weekdayName(m_date)));

    // 摘要链接（2026-11-22 用户拍板）：标题右侧、✓ 左侧，当日有日报才显示。
    // 下划线传达可点击（编辑器无 hover 高亮路径，保持最小改动）；命中区比
    // 文字略宽便于点击。字色用日历家族暖强调（T-071 橙收敛）。
    // 注意：m_checkRect 必须先于摘要定位赋值（首帧几何就绪，否则链接 x 依赖
    // 未初始化 rect 算出负坐标，画进窗外=看不见但热区能命中，2026-11-22 实测坑）
    m_checkRect = QRect(width() - 42, 10, 28, 28);
    m_digestRect = QRect();
    if (!m_digestPath.isEmpty()) {
        QFont linkFont;
        linkFont.setFamilies({ QStringLiteral("SimSun"), QStringLiteral("Microsoft YaHei UI") });
        linkFont.setPixelSize(14);
        linkFont.setUnderline(true);
        const QString label = QStringLiteral("摘要»");
        const QFontMetrics lm(linkFont);
        const int linkW = lm.horizontalAdvance(label);
        m_digestRect = QRect(m_checkRect.left() - 8 - linkW, 10, linkW + 8, 28);
        g.setFont(linkFont);
        g.setPen(overlay::warmAccent());
        g.drawText(QPoint(m_digestRect.left(),
                          m_digestRect.top() + 14 + lm.ascent() - lm.descent() / 2),
                   label);
    }

    g.fillPath(roundedPath(QRectF(m_checkRect), 5), QColor(255, 255, 255, 60)); // 玻璃按压底
    g.fillPath(roundedPath(QRectF(m_checkRect), 5), QColor(255, 255, 255, 60)); // 玻璃按压底
    QPen checkPen(overlay::warmAccent(), 2); // T-071 收编：散写橙 → 具名语义色
    checkPen.setCapStyle(Qt::RoundCap);
    checkPen.setJoinStyle(Qt::RoundJoin);
    g.setPen(checkPen);
    g.drawPolyline(QVector<QPoint>{ QPoint(m_checkRect.left() + 7, m_checkRect.top() + 14),
                                    QPoint(m_checkRect.left() + 12, m_checkRect.top() + 19),
                                    QPoint(m_checkRect.left() + 21, m_checkRect.top() + 8) });
}

void CalendarEventEditor::mousePressEvent(QMouseEvent *event)
{
    if (m_checkRect.contains(event->pos())) {
        saveAndClose();
        return;
    }
    // 摘要链接：直接 notepad 弹日报（不关编辑器；notepad 抢焦点后编辑器走
    // 失焦保存关闭，摘要独立展示——用户拍板形态）
    if (m_digestRect.isValid() && m_digestRect.contains(event->pos())) {
        openDigestInNotepad(m_digestPath);
        return;
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
