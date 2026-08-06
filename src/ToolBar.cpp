#include "ToolBar.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QFrame>
#include <QVariant>
#include <QPainter>
#include <QSvgRenderer>

// ============================================================
// 工具栏实现
// ============================================================

// 渲染 SVG 资源为指定颜色的 QIcon（normal=深灰，高亮=白）
static QIcon svgIcon(const QString &name, const QColor &color, int size = 20)
{
    QSvgRenderer renderer(QString(":/icons/%1.svg").arg(name));
    QPixmap src(size, size);
    src.fill(Qt::transparent);
    {
        QPainter p(&src);
        renderer.render(&p);
    }
    // 重新着色（SourceIn 保留透明度）
    QPixmap colored(size, size);
    colored.fill(Qt::transparent);
    {
        QPainter p(&colored);
        p.drawPixmap(0, 0, src);
        p.setCompositionMode(QPainter::CompositionMode_SourceIn);
        p.fillRect(colored.rect(), color);
    }
    return QIcon(colored);
}

// 线宽图标：横向线条粗细代表线宽
static QIcon widthIcon(qreal w, int size = 20, const QColor &color = Qt::black)
{
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);
    {
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(color, w, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(3, size / 2, size - 3, size / 2);
    }
    return QIcon(pm);
}

// 填充/纯线图标
static QIcon fillIcon(bool filled, int size = 20)
{
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);
    {
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        QRect r(4, 4, size - 8, size - 8);
        p.setPen(QPen(Qt::black, 2));
        p.setBrush(filled ? QBrush(Qt::black) : QBrush(Qt::NoBrush));
        p.drawRect(r);
    }
    return QIcon(pm);
}

ToolBar::ToolBar(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_StyledBackground);
    // 选中：蓝底白图标；悬停：浅蓝
    setStyleSheet(
        "QToolButton { border: none; border-radius: 3px;"
        "  background: transparent; padding: 2px; }"
        "QToolButton:hover { background: #e6f2ff; }"
        "QToolButton:checked { background: #2f6feb; }");

    auto *grid = new QGridLayout(this);
    grid->setContentsMargins(4, 4, 4, 4);
    grid->setSpacing(2);

    // ---- 第一行：绘图工具（互斥） ----
    struct { const char *icon; AnnotationCanvas::Tool tool; } tools[] = {
        {"rect", AnnotationCanvas::Tool::Rect},
        {"ellipse", AnnotationCanvas::Tool::Ellipse},
        {"line", AnnotationCanvas::Tool::Line},
        {"arrow", AnnotationCanvas::Tool::Arrow},
        {"pencil", AnnotationCanvas::Tool::Pencil},
        {"doodle", AnnotationCanvas::Tool::Doodle},
        {"mosaic", AnnotationCanvas::Tool::Mosaic},
        {"text", AnnotationCanvas::Tool::Text},
        {"eraser", AnnotationCanvas::Tool::Eraser},
    };
    for (int i = 0; i < 9; ++i) {
        auto *btn = new QToolButton(this);
        btn->setFixedSize(26, 26);
        btn->setCheckable(true);
        btn->setIcon(svgIcon(tools[i].icon, QColor("#333")));
        btn->setProperty("tool", static_cast<int>(tools[i].tool));
        btn->setToolTip([&] {
            switch (tools[i].tool) {
            case AnnotationCanvas::Tool::Rect: return QStringLiteral("矩形");
            case AnnotationCanvas::Tool::Ellipse: return QStringLiteral("椭圆");
            case AnnotationCanvas::Tool::Line: return QStringLiteral("直线");
            case AnnotationCanvas::Tool::Arrow: return QStringLiteral("箭头");
            case AnnotationCanvas::Tool::Pencil: return QStringLiteral("铅笔");
            case AnnotationCanvas::Tool::Doodle: return QStringLiteral("涂鸦笔");
            case AnnotationCanvas::Tool::Mosaic: return QStringLiteral("马赛克");
            case AnnotationCanvas::Tool::Text: return QStringLiteral("文字");
            case AnnotationCanvas::Tool::Eraser: return QStringLiteral("橡皮擦");
            default: return QString();
            }
        }());
        connect(btn, &QToolButton::clicked, this, &ToolBar::onToolClicked);
        grid->addWidget(btn, 0, i);
        m_toolButtons.append(btn);
    }

    // 第一行操作按钮：撤销 重做 | 保存 复制
    m_undoBtn = new QToolButton(this);
    m_undoBtn->setFixedSize(26, 26);
    m_undoBtn->setIcon(svgIcon("undo", QColor("#333")));
    m_undoBtn->setToolTip("撤销");
    connect(m_undoBtn, &QToolButton::clicked, this, &ToolBar::undoRequested);
    grid->addWidget(m_undoBtn, 0, 9);

    m_redoBtn = new QToolButton(this);
    m_redoBtn->setFixedSize(26, 26);
    m_redoBtn->setIcon(svgIcon("redo", QColor("#333")));
    m_redoBtn->setToolTip("重做");
    connect(m_redoBtn, &QToolButton::clicked, this, &ToolBar::redoRequested);
    grid->addWidget(m_redoBtn, 0, 10);

    auto *saveBtn = new QToolButton(this);
    saveBtn->setFixedSize(26, 26);
    saveBtn->setIcon(svgIcon("save", QColor("#333")));
    saveBtn->setToolTip("保存");
    connect(saveBtn, &QToolButton::clicked, this, &ToolBar::saveRequested);
    grid->addWidget(saveBtn, 0, 12);

    auto *copyBtn = new QToolButton(this);
    copyBtn->setFixedSize(26, 26);
    copyBtn->setIcon(svgIcon("copy", QColor("#333")));
    copyBtn->setToolTip("复制");
    connect(copyBtn, &QToolButton::clicked, this, &ToolBar::copyRequested);
    grid->addWidget(copyBtn, 0, 13);

    // 完成按钮（绿色，贴右上角）
    auto *confirmBtn = new QToolButton(this);
    confirmBtn->setFixedSize(26, 26);
    confirmBtn->setText("✓");
    confirmBtn->setStyleSheet(
        "QToolButton { border: none; border-radius: 3px; background: #34c759;"
        "  color: #fff; font-weight: bold; padding: 0; }"
        "QToolButton:hover { background: #28a745; }");
    confirmBtn->setToolTip("完成");
    connect(confirmBtn, &QToolButton::clicked, this, &ToolBar::confirmRequested);
    grid->addWidget(confirmBtn, 0, 14);

    // ---- 第二行：线宽 / 填充 ----
    struct { qreal w; } widths[] = {{1.5}, {3.0}, {6.0}};
    for (int i = 0; i < 3; ++i) {
        auto *btn = new QToolButton(this);
        btn->setFixedSize(26, 26);
        btn->setCheckable(true);
        btn->setIcon(widthIcon(widths[i].w));
        btn->setProperty("width", widths[i].w);
        btn->setToolTip(i == 0 ? "小线宽" : i == 1 ? "中线宽" : "大线宽");
        connect(btn, &QToolButton::clicked, this, &ToolBar::onWidthClicked);
        grid->addWidget(btn, 1, i);
        m_widthButtons.append(btn);
    }

    // 填充/纯线（仅矩形/椭圆有效）
    m_fillBtn = new QToolButton(this);
    m_fillBtn->setFixedSize(26, 26);
    m_fillBtn->setCheckable(true);
    m_fillBtn->setIcon(fillIcon(false));
    m_fillBtn->setToolTip("填充/纯线（仅矩形、椭圆）");
    connect(m_fillBtn, &QToolButton::clicked, this, &ToolBar::onFilledClicked);
    grid->addWidget(m_fillBtn, 1, 3);

    // ---- 第二行：颜色板（两行八列 + 大色块，跨两行） ----
    auto *colorPanel = new QWidget(this);
    colorPanel->setStyleSheet("QWidget { background: transparent; }");
    auto *colorGrid = new QGridLayout(colorPanel);
    colorGrid->setContentsMargins(0, 0, 0, 0);
    colorGrid->setSpacing(2);

    const QColor presetColors[] = {
        QColor("#000000"), QColor("#7f7f7f"), QColor("#880015"), QColor("#ed1c24"),
        QColor("#ff7f27"), QColor("#fff200"), QColor("#22b14c"), QColor("#00a2e8"),
        QColor("#3f48cc"), QColor("#a349a4"), QColor("#ffffff"), QColor("#ffc90e"),
        QColor("#c8bfe7"), QColor("#b5e61d"), QColor("#99d9ea"), QColor("#ffaec9"),
    };
    for (int i = 0; i < 16; ++i) {
        auto *btn = new QToolButton(colorPanel);
        btn->setFixedSize(18, 18);
        btn->setProperty("color", presetColors[i].name());
        btn->setStyleSheet(QString(
            "QToolButton { border: 1px solid #bbb; background: %1; padding: 0; }"
            "QToolButton:hover { border: 1px solid #2f6feb; }").arg(presetColors[i].name()));
        connect(btn, &QToolButton::clicked, this, &ToolBar::onColorClicked);
        colorGrid->addWidget(btn, i / 8, i % 8);
        m_colorButtons.append(btn);
    }
    grid->addWidget(colorPanel, 0, 15, 2, 1);

    // 当前颜色大色块（占两行，右侧）
    m_colorBig = new QToolButton(this);
    m_colorBig->setFixedSize(22, 22);
    m_colorBig->setEnabled(false);
    m_colorBig->setStyleSheet(QString(
        "QToolButton { border: 1px solid #555; background: %1; }").arg(m_currentColor.name()));
    m_colorBig->setToolTip("当前颜色");
    grid->addWidget(m_colorBig, 0, 16, 2, 1);

    // 默认工具：None（选择模式）—— 无按钮高亮
    setTool(AnnotationCanvas::Tool::None);
    // 默认线宽：中线宽（3.0）
    setWidth(3.0);
}

void ToolBar::setTool(AnnotationCanvas::Tool t)
{
    m_tool = t;
    for (QToolButton *btn : m_toolButtons) {
        const bool checked =
            btn->property("tool").isValid()
            && static_cast<AnnotationCanvas::Tool>(btn->property("tool").toInt()) == t;
        btn->setChecked(checked);
        // 高亮时切换白图标
        btn->setIcon(svgIcon([&] {
            switch (t) {
            case AnnotationCanvas::Tool::Rect: return "rect";
            case AnnotationCanvas::Tool::Ellipse: return "ellipse";
            case AnnotationCanvas::Tool::Line: return "line";
            case AnnotationCanvas::Tool::Arrow: return "arrow";
            case AnnotationCanvas::Tool::Pencil: return "pencil";
            case AnnotationCanvas::Tool::Doodle: return "doodle";
            case AnnotationCanvas::Tool::Mosaic: return "mosaic";
            case AnnotationCanvas::Tool::Text: return "text";
            case AnnotationCanvas::Tool::Eraser: return "eraser";
            default: return "";
            }
        }(), checked ? Qt::white : QColor("#333")));
    }
}

void ToolBar::setColor(const QColor &c)
{
    m_currentColor = c;
    m_colorBig->setStyleSheet(QString(
        "QToolButton { border: 1px solid #555; background: %1; }").arg(c.name()));
    for (QToolButton *btn : m_colorButtons)
        btn->setChecked(btn->property("color").toString() == c.name());
}

void ToolBar::setWidth(qreal w)
{
    m_width = w;
    for (QToolButton *btn : m_widthButtons) {
        const bool checked = qFuzzyCompare(btn->property("width").toReal(), w);
        btn->setChecked(checked);
        btn->setIcon(widthIcon(btn->property("width").toReal(), 20,
                               checked ? Qt::white : Qt::black));
    }
}

void ToolBar::setFilled(bool filled)
{
    m_filled = filled;
    if (m_fillBtn) {
        m_fillBtn->setChecked(filled);
        m_fillBtn->setIcon(fillIcon(filled));
    }
}

void ToolBar::setUndoRedoEnabled(bool undo, bool redo)
{
    if (m_undoBtn) m_undoBtn->setEnabled(undo);
    if (m_redoBtn) m_redoBtn->setEnabled(redo);
}

void ToolBar::onToolClicked()
{
    auto *btn = qobject_cast<QToolButton *>(sender());
    if (!btn || !btn->property("tool").isValid())
        return;
    const auto t = static_cast<AnnotationCanvas::Tool>(btn->property("tool").toInt());
    if (t == m_tool) {
        // 点灭当前高亮工具 → 进入选择模式（可拖动画布）
        setTool(AnnotationCanvas::Tool::None);
        emit toolSelected(AnnotationCanvas::Tool::None);
    } else {
        setTool(t);
        emit toolSelected(t);
    }
}

void ToolBar::onColorClicked()
{
    auto *btn = qobject_cast<QToolButton *>(sender());
    if (!btn)
        return;
    const QColor c(btn->property("color").toString());
    if (c.isValid()) {
        m_currentColor = c;
        emit colorSelected(c);
        setColor(c);
    }
}

void ToolBar::onWidthClicked()
{
    auto *btn = qobject_cast<QToolButton *>(sender());
    if (!btn)
        return;
    // 线宽互斥：取消其他宽度按钮选中
    for (QToolButton *b : findChildren<QToolButton *>()) {
        if (b->property("width").isValid())
            b->setChecked(b == btn);
    }
    m_width = btn->property("width").toReal();
    emit widthSelected(m_width);
}

void ToolBar::onFilledClicked()
{
    m_filled = m_fillBtn->isChecked();
    m_fillBtn->setIcon(fillIcon(m_filled));
    emit filledToggled(m_filled);
}
