#pragma once

#include <QRect>
#include <QVector>

namespace poptools {

enum class BoxHeaderKind
{
    Prev,   // ‹ 上一页
    Adjust, // 调整位置和列数（十字箭头，同日历头部；管理器在托盘）
    Next    // › 下一页
};

// ============================================================
// 收纳盒布局与命中测试（文件夹门户式 + 安卓网格分页）。
// 坐标全部为主屏共享画布物理像素。格子尺寸固定：面板宽度 = cols×kCellW，
// 高度 = 头部 + rows×kCellH +（多页时）页圆点条——大小必为图标整数倍；
// 行数随条目数自动增高（不超屏），超容量翻页（T-060）。
// ============================================================
class BoxLayout
{
public:
    QRect panel;
    int cols = 4;
    int rows = 1;        // 由条目数与屏幕高度推导
    int page = 0;        // 当前页（构造时钳制到 [0, pageCount-1]）
    int pageCount = 1;
    int capacity = 0;    // cols×rows
    int entryCount = 0;  // 构造入参快照；entryIndexAt 用它钳制全局条目下标
    QRect titleRect;     // 标题文字区（左上）
    QRect headerBar;     // 头部条（标题 + 按钮）
    QVector<QRect> headerButtons; // 3 个，索引 = BoxHeaderKind；左起：Prev/Adjust/Next
    QVector<QRect> cells;         // 当前页格子（capacity 个；前 min(剩余,capacity) 个有内容）
    QVector<QRect> dots;          // 每页一个圆点（≤9 页才生成；页圆点条高度恒 kDotsHeight）

    // ---- 尺寸常量（物理像素；大小 = 图标整数倍纪律的基准） ----
    // kCellH 预算（150% DPI）：图标顶偏移 10 + 图标 57 + 标签间距 ~11
    // + 雅黑标签 ~23 + 底边距 ~11——96 塞不下（T-061：标签顶曾叠进图标底缘）
    static constexpr int kCellW = 92;
    static constexpr int kCellH = 112;
    static constexpr int kHeaderHeight = 44;
    static constexpr int kDotsHeight = 18;
    static constexpr int kHeaderBtnSize = 26;

    BoxLayout() = default;
    BoxLayout(const QRect &panelRect, int cols, int entryCount, int page);

    // 自动定尺寸：给定锚点/列数/条目数/屏幕，推导行数（自动增高，不超屏）、
    // 页数（超容量翻页）与面板矩形。多页时预留页圆点条高度。
    struct Sizing
    {
        QRect panel;
        int rows = 1;
        int pageCount = 1;
    };
    static Sizing compute(const QPoint &topLeft, int cols, int entryCount, const QRect &screen);

    // 拖拽吸附：任意矩形 → 列/行整数（≤8 列；行数 = 内容区高度 ÷ kCellH 四舍五入）。
    // ⚠ snapRows/snappedRect 当前仅 domain_tests 覆盖，预留给管理器"拖拽调行数"（未实现）
    static int snapCols(int widthPx);
    static int snapRows(int heightPx, bool hasPages);
    // 吸附后的完整面板矩形：宽=cols×kCellW，高=头部+rows×kCellH（+多页圆点条）；
    // 供拖拽预览/测试
    static QRect snappedRect(const QPoint &topLeft, int cols, int rows, bool hasPages);

    // ---- 当前页可见区间（面板补图标与渲染器画网格共用口径，勿双写公式） ----
    // 首个全局条目下标 = page×capacity；本页可见条目数 = min(capacity, 剩余≥0)
    int firstVisibleIndex() const { return page * capacity; }
    int visibleCount(int totalEntries) const;

    // 命中：返回全局条目下标（page*capacity + cell）；空格/越界/未命中 → -1
    int entryIndexAt(const QPoint &p) const;
    int hitCell(const QPoint &p, int *cellIndex = nullptr) const;
    int hitHeader(const QPoint &p) const; // BoxHeaderKind 或 -1
    int hitDot(const QPoint &p) const;    // 页下标或 -1（≤9 页才有）

    // ceil(entryCount/capacity)；capacity<=0 或 0 条目 → 1（空盒也要能布局出一页）
    static int pageCountFor(int entryCount, int capacity);
};

} // namespace poptools
