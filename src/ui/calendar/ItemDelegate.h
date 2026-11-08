#pragma once

#include <QColor>
#include <QStyledItemDelegate>

namespace poptools {

// 列表项自定义数据角色（QListWidgetItem::setData 使用）
namespace itemroles {
inline constexpr int CategoryColor = Qt::UserRole + 11; // QColor（勾选圈颜色）
inline constexpr int Important     = Qt::UserRole + 12; // bool
inline constexpr int ItemId        = Qt::UserRole + 13; // QString
} // namespace itemroles

// ============================================================
// 日程列表项绘制（T-043 起日程=待办单一列表，全部带勾选圈）：
//   * 左侧圆形勾选框 —— 圈色 = 分类色（完成=分类色实底+白勾），文字加删除线
//   * 重要 —— 文字加粗 + 强调色 ★ 前缀
//   * 选中 —— 强调色圆角底 + 反白文字
// ============================================================
class ItemDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    struct Palette
    {
        QColor text;       // 正文
        QColor subText;    // 次级（未勾选圈）
        QColor accent;     // 强调色（选中底/勾选框/★）
        QColor cardBg;     // 行内徽章底（未用，预留）
    };
    explicit ItemDelegate(QObject *parent = nullptr);

    void setDelegatePalette(const Palette &p);

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option,
                   const QModelIndex &index) const override;

private:
    Palette m_pal;
};

} // namespace poptools
