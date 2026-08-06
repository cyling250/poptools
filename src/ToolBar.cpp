#include "ToolBar.h"

#include <QHBoxLayout>
#include <QFrame>
#include <QVariant>
#include <QPainter>

ToolBar::ToolBar(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_StyledBackground);
    setStyleSheet(
        "QToolButton { border: 1px solid #c8c8c8; border-radius: 4px;"
        "  background: #ffffff; padding: 4px 8px; color: #333; }"
        "QToolButton:hover { background: #e6f2ff; }"
        "QToolButton:checked { background: #cfe6ff; border-color: #2f6feb; color: #2f6feb; }");

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(6, 4, 6, 4);
    layout->setSpacing(4);

    // ---- 一、绘图与形状工具 ----
    struct { const char *label; AnnotationCanvas::Tool tool; } drawTools[] = {
        {"矩形", AnnotationCanvas::Tool::Rect},
        {"椭圆", AnnotationCanvas::Tool::Ellipse},
        {"箭头", AnnotationCanvas::Tool::Arrow},
        {"折线", AnnotationCanvas::Tool::Polyline},
    };
    for (auto &t : drawTools) {
        auto *btn = makeToolButton(t.label);
        btn->setCheckable(true);
        btn->setProperty("tool", static_cast<int>(t.tool));
        connect(btn, &QToolButton::clicked, this, &ToolBar::onToolClicked);
        layout->addWidget(btn);
        m_toolButtons.append(btn);
    }

    layout->addWidget([&] { auto *s = new QFrame(this); s->setFrameShape(QFrame::VLine); return s; }());

    // ---- 二、标注编辑工具 ----
    struct { const char *label; AnnotationCanvas::Tool tool; } editTools[] = {
        {"画笔", AnnotationCanvas::Tool::Freehand},
        {"橡皮", AnnotationCanvas::Tool::Eraser},
        {"文本", AnnotationCanvas::Tool::Text},
        {"取色", AnnotationCanvas::Tool::ColorPicker},
    };
    for (auto &t : editTools) {
        auto *btn = makeToolButton(t.label);
        btn->setCheckable(true);
        btn->setProperty("tool", static_cast<int>(t.tool));
        connect(btn, &QToolButton::clicked, this, &ToolBar::onToolClicked);
        layout->addWidget(btn);
        m_toolButtons.append(btn);
    }

    layout->addWidget([&] { auto *s = new QFrame(this); s->setFrameShape(QFrame::VLine); return s; }());

    // ---- 三、预设颜色面板 ----
    const QColor presetColors[] = {
        Qt::red, QColor("#ff7f00"), Qt::yellow, Qt::green,
        Qt::cyan, Qt::blue, Qt::magenta, Qt::black, Qt::white, Qt::gray,
    };
    for (const QColor &c : presetColors) {
        auto *btn = new QToolButton(this);
        btn->setFixedSize(20, 20);
        btn->setProperty("color", c.name());
        btn->setStyleSheet(QString(
            "QToolButton { border: 1px solid #999; border-radius: 3px; background: %1; }"
            "QToolButton:hover { border: 2px solid #2f6feb; }").arg(c.name()));
        connect(btn, &QToolButton::clicked, this, &ToolBar::onColorClicked);
        layout->addWidget(btn);
        m_colorButtons.append(btn);
    }

    layout->addWidget([&] { auto *s = new QFrame(this); s->setFrameShape(QFrame::VLine); return s; }());

    // ---- 四、状态管理与完成操作 ----
    auto *undoBtn = new QToolButton(this);
    undoBtn->setText("↶");
    undoBtn->setToolTip("撤销");
    undoBtn->setProperty("action", "undo");
    connect(undoBtn, &QToolButton::clicked, this, &ToolBar::undoRequested);
    auto *redoBtn = new QToolButton(this);
    redoBtn->setText("↷");
    redoBtn->setToolTip("重做");
    redoBtn->setProperty("action", "redo");
    connect(redoBtn, &QToolButton::clicked, this, &ToolBar::redoRequested);
    m_toolButtons.append(undoBtn);
    m_toolButtons.append(redoBtn);
    layout->addWidget(undoBtn);
    layout->addWidget(redoBtn);

    layout->addWidget([&] { auto *s = new QFrame(this); s->setFrameShape(QFrame::VLine); return s; }());

    auto *saveBtn = new QToolButton(this);
    saveBtn->setText("保存");
    connect(saveBtn, &QToolButton::clicked, this, &ToolBar::saveRequested);
    auto *copyBtn = new QToolButton(this);
    copyBtn->setText("复制");
    connect(copyBtn, &QToolButton::clicked, this, &ToolBar::copyRequested);
    layout->addWidget(saveBtn);
    layout->addWidget(copyBtn);

    auto *confirmBtn = new QToolButton(this);
    confirmBtn->setText("完成 ✓");
    confirmBtn->setStyleSheet(
        "QToolButton { border: none; border-radius: 4px; background: #2f6feb;"
        "  padding: 4px 14px; color: #fff; font-weight: bold; }"
        "QToolButton:hover { background: #1f5fd0; }");
    connect(confirmBtn, &QToolButton::clicked, this, &ToolBar::confirmRequested);
    layout->addWidget(confirmBtn);

    layout->addStretch();

    // 默认工具：矩形
    if (!m_toolButtons.isEmpty())
        m_toolButtons[0]->setChecked(true);
}

QToolButton *ToolBar::makeToolButton(const QString &label)
{
    auto *btn = new QToolButton(this);
    btn->setText(label);
    return btn;
}

void ToolBar::setTool(AnnotationCanvas::Tool t)
{
    for (QToolButton *btn : m_toolButtons) {
        if (btn->property("tool").isValid()) {
            const bool checked =
                static_cast<AnnotationCanvas::Tool>(btn->property("tool").toInt()) == t;
            btn->setChecked(checked);
            btn->setDown(checked);
        }
    }
}

void ToolBar::setColor(const QColor &c)
{
    m_currentColor = c;
    for (QToolButton *btn : m_colorButtons)
        btn->setChecked(btn->property("color").toString() == c.name());
}

void ToolBar::setUndoRedoEnabled(bool undo, bool redo)
{
    for (QToolButton *btn : m_toolButtons) {
        const QString action = btn->property("action").toString();
        if (action == "undo")
            btn->setEnabled(undo);
        else if (action == "redo")
            btn->setEnabled(redo);
    }
}

void ToolBar::onToolClicked()
{
    auto *btn = qobject_cast<QToolButton *>(sender());
    if (!btn)
        return;
    const auto tool = static_cast<AnnotationCanvas::Tool>(btn->property("tool").toInt());
    emit toolSelected(tool);
}

void ToolBar::onColorClicked()
{
    auto *btn = qobject_cast<QToolButton *>(sender());
    if (!btn)
        return;
    m_currentColor = QColor(btn->property("color").toString());
    emit colorSelected(m_currentColor);
}
