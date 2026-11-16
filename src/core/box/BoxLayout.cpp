#include "BoxLayout.h"

#include <algorithm>

namespace poptools {

// ceil(entryCount/capacity)；capacity<=0 或 0 条目 → 1（空盒也要能布局出一页）
int BoxLayout::pageCountFor(int entryCount, int capacity)
{
    if (capacity <= 0)
        return 1;
    return std::max(1, (entryCount + capacity - 1) / capacity);
}

// 本页实际可见条目数：min(capacity, 剩余条目) 且不为负（末页可能不满）
int BoxLayout::visibleCount(int totalEntries) const
{
    const int remaining = totalEntries - firstVisibleIndex();
    return std::min(capacity, remaining > 0 ? remaining : 0);
}

BoxLayout::Sizing BoxLayout::compute(const QPoint &topLeft, int cols, int entryCount,
                                     const QRect &screen)
{
    const int c = std::clamp(cols, 2, 8);
    const int idealRows = entryCount <= 0 ? 1 : (entryCount + c - 1) / c;
    // 屏内可用行数：顶部头部 + 底部页圆点条之后的高度能放几行
    const int availHeight = screen.bottom() + 1 - topLeft.y() - kHeaderHeight - kDotsHeight;
    const int maxRows = std::max(1, availHeight / kCellH);
    Sizing s;
    s.rows = std::clamp(idealRows, 1, maxRows);
    const int capacity = c * s.rows;
    s.pageCount = pageCountFor(entryCount, capacity);
    // x 右缘钳制屏内
    const int x = std::clamp(topLeft.x(), screen.left(),
                             std::max(screen.left(), screen.right() + 1 - c * kCellW));
    const int height = kHeaderHeight + s.rows * kCellH
                       + (s.pageCount > 1 ? kDotsHeight : 0);
    s.panel = QRect(x, topLeft.y(), c * kCellW, height);
    return s;
}

int BoxLayout::snapCols(int widthPx)
{
    return std::clamp((widthPx + kCellW / 2) / kCellW, 2, 8);
}

int BoxLayout::snapRows(int heightPx, bool hasPages)
{
    const int content = heightPx - kHeaderHeight - (hasPages ? kDotsHeight : 0);
    return std::max(1, (content + kCellH / 2) / kCellH);
}

QRect BoxLayout::snappedRect(const QPoint &topLeft, int cols, int rows, bool hasPages)
{
    const int w = std::clamp(cols, 2, 8) * kCellW;
    const int h = kHeaderHeight + std::max(1, rows) * kCellH + (hasPages ? kDotsHeight : 0);
    return QRect(topLeft, QSize(w, h));
}

// 由面板矩形展开全部命中几何：头部按钮右起往左（Next 最右）、格子网格、页圆点；
// 入参 page 钳到 [0,pageCount-1]。无副作用，可随意重算
BoxLayout::BoxLayout(const QRect &panelRect, int cols_, int entryCount_, int page_)
    : panel(panelRect)
    , cols(std::clamp(cols_, 2, 8))
    , entryCount(std::max(0, entryCount_))
{
    headerBar = QRect(panel.left(), panel.top(), panel.width(), kHeaderHeight);
    // 14/4 = 标题左/上内边距（物理像素，与头部条 44 配套）
    titleRect = QRect(panel.left() + 14, panel.top() + 4, panel.width() / 2, kHeaderHeight - 8);

    // 头部按钮右起往左填充：index 对齐 BoxHeaderKind——Next(2) 最右、Adjust(1)
    // 居中、Prev(0) 最左（视觉从左到右为 ‹ ✛ ›；与日历头部同款右起循环）
    headerButtons.resize(3);
    // 12 = 最右按钮（Next）距面板右缘的边距
    int bx = panel.right() - 12 - kHeaderBtnSize;
    for (int i = 2; i >= 0; --i) {
        headerButtons[i] = QRect(bx, panel.top() + (kHeaderHeight - kHeaderBtnSize) / 2,
                                 kHeaderBtnSize, kHeaderBtnSize);
        bx -= kHeaderBtnSize + 8;
    }

    // 网格：头部以下、页圆点条以上；固定格子尺寸（面板宽必为 cols×kCellW）
    const int gridTop = panel.top() + kHeaderHeight;
    const int gridHeight = std::max(0, panel.height() - kHeaderHeight - kDotsHeight);
    rows = std::max(1, gridHeight / kCellH);
    capacity = cols * rows;

    pageCount = pageCountFor(entryCount, capacity);
    page = std::clamp(page_, 0, pageCount - 1);

    cells.resize(capacity);
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            cells[r * cols + c] = QRect(panel.left() + c * kCellW, gridTop + r * kCellH, kCellW,
                                        kCellH);
        }
    }

    // 页圆点（≤9 页才生成；更多页时渲染器改画「n/m」文本）
    if (pageCount <= 9) {
        // 圆点直径 8、间距 6，在 kDotsHeight=18 内垂直居中（dy 公式见下）
        const int dotD = 8, gap = 6;
        const int totalW = pageCount * dotD + (pageCount - 1) * gap;
        int dx = panel.left() + (panel.width() - totalW) / 2;
        const int dy = panel.bottom() + 1 - kDotsHeight + (kDotsHeight - dotD) / 2;
        dots.resize(pageCount);
        for (int i = 0; i < pageCount; ++i) {
            dots[i] = QRect(dx, dy, dotD, dotD);
            dx += dotD + gap;
        }
    }
}

int BoxLayout::entryIndexAt(const QPoint &p) const
{
    const int cell = hitCell(p);
    if (cell < 0) // 注意：hitCell 返回 0 是合法命中（第 0 格），不能用布尔判断
        return -1;
    const int idx = page * capacity + cell;
    return idx < entryCount ? idx : -1;
}

int BoxLayout::hitCell(const QPoint &p, int *cellIndex) const
{
    if (cellIndex)
        *cellIndex = -1;
    for (int i = 0; i < cells.size(); ++i) {
        if (cells.at(i).contains(p)) {
            if (cellIndex)
                *cellIndex = i;
            return i;
        }
    }
    return -1;
}

int BoxLayout::hitHeader(const QPoint &p) const
{
    for (int i = 0; i < headerButtons.size(); ++i) {
        if (headerButtons.at(i).contains(p))
            return i;
    }
    return -1;
}

int BoxLayout::hitDot(const QPoint &p) const
{
    for (int i = 0; i < dots.size(); ++i) {
        if (dots.at(i).contains(p))
            return i;
    }
    return -1;
}

} // namespace poptools
