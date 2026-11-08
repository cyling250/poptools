#include "ItemDelegate.h"

#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>

namespace poptools {

ItemDelegate::ItemDelegate(QObject *parent)
    : QStyledItemDelegate(parent)
{
    m_pal.text = QColor("#000000");
    m_pal.subText = QColor(0, 0, 0, 130);
    m_pal.accent = QColor(0x2B, 0x6F, 0xF2);
    m_pal.cardBg = QColor("#FFFFFF");
}

void ItemDelegate::setDelegatePalette(const Palette &p)
{
    m_pal = p;
}

void ItemDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                         const QModelIndex &index) const
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);

    const bool selected = option.state & QStyle::State_Selected;
    const bool important = index.data(itemroles::Important).toBool();
    const bool done = index.data(Qt::CheckStateRole).toInt() == Qt::Checked;
    QColor categoryColor = index.data(itemroles::CategoryColor).value<QColor>();
    if (!categoryColor.isValid())
        categoryColor = m_pal.subText;
    const QString text = index.data(Qt::DisplayRole).toString();

    const QRect r = option.rect.adjusted(4, 1, -4, -1);

    // 行底（悬停淡色 / 选中强调色圆角）
    if (selected) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(m_pal.accent);
        painter->drawRoundedRect(r, 6, 6);
    } else if (option.state & QStyle::State_MouseOver) {
        QColor hover = m_pal.accent;
        hover.setAlpha(22);
        painter->setPen(Qt::NoPen);
        painter->setBrush(hover);
        painter->drawRoundedRect(r, 6, 6);
    }

    // 左侧勾选圈（圈色 = 分类色；完成 = 分类色实底 + 白勾）
    const int cy = r.center().y();
    const int cx = r.left() + 14;
    {
        QRect circle(cx - 7, cy - 7, 14, 14);
        if (done) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(categoryColor);
            painter->drawEllipse(circle);
            // 白色对勾
            QPen checkPen(QColor("#FFFFFF"));
            checkPen.setWidthF(1.8);
            checkPen.setCapStyle(Qt::RoundCap);
            checkPen.setJoinStyle(Qt::RoundJoin);
            painter->setPen(checkPen);
            painter->setBrush(Qt::NoBrush);
            QPainterPath path;
            path.moveTo(circle.left() + 3.5, circle.center().y() + 0.5);
            path.lineTo(circle.center().x() - 0.5, circle.bottom() - 4);
            path.lineTo(circle.right() - 3.5, circle.top() + 4);
            painter->drawPath(path);
        } else {
            QPen pen(selected ? QColor(255, 255, 255, 210) : categoryColor);
            pen.setWidthF(1.5);
            painter->setPen(pen);
            painter->setBrush(Qt::NoBrush);
            painter->drawEllipse(circle);
        }
    }

    // 文字（重要加粗 + ★ 前缀；完成加删除线；选中反白）
    QColor textColor = m_pal.text;
    if (selected)
        textColor = QColor("#FFFFFF");
    else if (done)
        textColor = m_pal.subText;

    QFont f = option.font;
    f.setBold(important);
    f.setStrikeOut(done);
    painter->setFont(f);
    painter->setPen(textColor);

    QFontMetrics fm(f);
    const QString drawText = fm.elidedText(text, Qt::ElideRight, r.width() - 44);
    int tx = r.left() + 28;
    if (important) {
        painter->save();
        QFont starFont = f;
        starFont.setBold(true);
        starFont.setStrikeOut(false);
        painter->setFont(starFont);
        painter->setPen(selected ? QColor("#FFD75E") : m_pal.accent);
        painter->drawText(QPoint(tx, r.center().y() + fm.ascent() / 2 - 1), "★");
        painter->restore();
        tx += 15;
    }
    painter->setPen(textColor);
    painter->drawText(QPoint(tx, r.center().y() + fm.ascent() / 2 - 1), drawText);

    painter->restore();
}

QSize ItemDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    QSize s = QStyledItemDelegate::sizeHint(option, index);
    s.setHeight(qMax(28, s.height()));
    return s;
}

} // namespace poptools
