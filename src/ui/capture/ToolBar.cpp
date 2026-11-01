#include "ToolBar.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QFrame>
#include <QVariant>
#include <QPainter>
#include <QSvgRenderer>
#include <QGuiApplication>
#include <QStyleHints>
#include <QComboBox>
#include <QLineEdit>
#include <QLabel>
#include <QDoubleValidator>
#include <QGraphicsDropShadowEffect>
#include <QHash>

namespace poptools
{

    // ============================================================
    // 工具栏实现
    // ============================================================

    ToolBar *ToolBar::s_instance = nullptr;

    ToolBar *ToolBar::create(QWidget *parent)
    {
        if (!s_instance)
            s_instance = new ToolBar(parent);
        return s_instance;
    }

    ToolBar *ToolBar::instance()
    {
        return s_instance;
    }

    ToolBar::~ToolBar()
    {
        if (s_instance == this)
            s_instance = nullptr;
    }

    // 渲染 SVG 资源为指定颜色的 QIcon（normal=深灰，高亮=白）
    // 4x 超采样、不带 DPR 元数据，交给 QIcon 缩放到按钮 iconSize。
    // 不能给位图 setDevicePixelRatio：Qt 6.8 的 QToolButton 绘制路径会把
    // 带 DPR 元数据的位图按物理像素绘制（物理尺寸超过按钮即被裁剪，
    // 高分屏上图标只剩左上角），已由探针实测确认
    static QIcon svgIcon(const QString &name, const QColor &color, int size = 20)
    {
        // 渲染缓存：4x 超采样位图较贵，而 setTool 每次点击都全量刷新按钮图标。
        // 键 = 资源名 + 颜色 + 尺寸（当前仅两种颜色、一种尺寸在用）
        static QHash<QString, QIcon> cache;
        const QString key = name + QLatin1Char('/') + color.name()
                            + QLatin1Char('/') + QString::number(size);
        const auto it = cache.constFind(key);
        if (it != cache.constEnd())
            return it.value();

        QSvgRenderer renderer(QString(":/icons/%1.svg").arg(name));
        if (!renderer.isValid())
            return QIcon(); // 资源缺失时返回空图标（调用方应能处理）
        constexpr int kSs = 4; // 超采样倍数
        QPixmap pm(size * kSs, size * kSs);
        pm.fill(Qt::transparent);
        {
            QPainter p(&pm);
            renderer.render(&p);
            // 重新着色（SourceIn 保留透明度）
            p.setCompositionMode(QPainter::CompositionMode_SourceIn);
            p.fillRect(0, 0, pm.width(), pm.height(), color);
        }
        const QIcon icon(pm);
        cache.insert(key, icon);
        return icon;
    }

    // 线宽图标：圆点大小按档位内相对值缩放（半径 2px..7px），档位之间清晰可辨
    static QIcon widthIcon(qreal w, qreal maxW, int size = 20, const QColor &color = Qt::black)
    {
        constexpr int kSs = 4; // 超采样倍数（同 svgIcon，不带 DPR 元数据）
        QPixmap pm(size * kSs, size * kSs);
        pm.fill(Qt::transparent);
        {
            QPainter p(&pm);
            p.setRenderHint(QPainter::Antialiasing);
            const qreal r = (2.0 + 5.0 * (w / qMax(1e-6, maxW))) * kSs;
            p.setPen(Qt::NoPen);
            p.setBrush(color);
            p.drawEllipse(QPointF(pm.width() / 2.0, pm.height() / 2.0), r, r);
        }
        return QIcon(pm);
    }

    // 填充/纯线图标
    static QIcon fillIcon(bool filled, int size = 20, const QColor &color = Qt::black)
    {
        constexpr int kSs = 4; // 超采样倍数（同 svgIcon，不带 DPR 元数据）
        QPixmap pm(size * kSs, size * kSs);
        pm.fill(Qt::transparent);
        {
            QPainter p(&pm);
            p.setRenderHint(QPainter::Antialiasing);
            const int m = 4 * kSs; // 原逻辑边距 4px 的等比放大
            QRect r(m, m, pm.width() - 2 * m, pm.height() - 2 * m);
            p.setPen(QPen(color, 2 * kSs));
            p.setBrush(filled ? QBrush(color) : QBrush(Qt::NoBrush));
            p.drawRect(r);
        }
        return QIcon(pm);
    }

    // 每个工具的线宽档位：铅笔最粗(4) 仍比涂鸦最细(6) 更细；马赛克档位为笔刷直径
    static QVector<qreal> widthOptionsFor(Tool t)
    {
        switch (t)
        {
        case Tool::Pencil:
            return {1.0, 2.0, 4.0};
        case Tool::Doodle:
            return {6.0, 12.0, 20.0};
        case Tool::Mosaic:
            return {12.0, 20.0, 32.0};
        default:
            return {2.0, 5.0, 12.0}; // 矩形/椭圆/直线/箭头/文本等
        }
    }

    // 第一行互斥工具的元数据表（单一数据源：按钮创建 / tooltip / 图标刷新共用）
    struct ToolInfo
    {
        Tool tool;
        const char *icon;
        const char *tip;
    };
    constexpr ToolInfo kTools[] = {
        {Tool::Rect, "rect", "矩形"},
        {Tool::Ellipse, "ellipse", "椭圆"},
        {Tool::Line, "line", "直线"},
        {Tool::Arrow, "arrow", "箭头"},
        {Tool::Pencil, "pencil", "铅笔"},
        {Tool::Doodle, "doodle", "涂鸦笔"},
        {Tool::Mosaic, "mosaic", "马赛克"},
        {Tool::Text, "text", "文字"},
        {Tool::Eraser, "eraser", "橡皮擦"},
    };

    const ToolInfo *findTool(Tool t)
    {
        for (const ToolInfo &info : kTools)
            if (info.tool == t)
                return &info;
        return nullptr;
    }

    // 行内竖分隔线（两行各自持有，视觉上与行高等高）
    QFrame *ToolBar::makeSeparator()
    {
        auto *sep = new QFrame(this);
        sep->setFrameShape(QFrame::VLine);
        sep->setStyleSheet(QStringLiteral("QFrame { color: rgba(255,255,255,32); margin: 4px 0; }"));
        return sep;
    }

    ToolBar::ToolBar(QWidget *parent)
        : QWidget(parent)
    {
        // 工具栏自身使用箭头光标（父窗口截图画布为十字/尺寸光标，
        // 若不在本控件显式设置，Qt 会沿祖先链继承父窗口光标）
        setCursor(Qt::ArrowCursor);

        // 跟随系统深浅色主题 → 改为常暗玻璃面板（2026-11 UI 现代化）：
        // 截图覆盖层浮在任何画面上，深色面板在明暗截图内容中都稳定可读
        // （Snipaste/PixPin 同款做法）；浅色系统下也避免大面积亮块刺眼。
        // 内部所有 m_dark 分支因此恒取深色值，分支保留便于未来恢复跟随主题。
        m_dark = true;

        setAttribute(Qt::WA_StyledBackground);
        // 注意：不能用类型选择器 "ToolBar { ... }" —— 类位于 poptools 命名空间，
        // QMetaObject::className() 返回 "poptools::ToolBar"，样式表类型选择器失配 → 底衬丢失。
        // 改用 objectName ID 选择器（#annotationToolBar）与命名空间解耦。
        setObjectName("annotationToolBar");
        // 常暗玻璃面板：深底 + 白 9% 细描边 + 12px 圆角；悬停白 10%；
        // 选中主色蓝 + 顶部内高光。qproperty-iconSize 统一 20px 图标（4x 超采样位图）
        setStyleSheet(QStringLiteral(
                          "#annotationToolBar { background: #1E2024; border: 1px solid rgba(255,255,255,23);"
                          "  border-radius: 12px; }"
                          "QToolButton { qproperty-iconSize: 20px 20px; border: none; border-radius: 7px;"
                          "  background: transparent; padding: 2px; }"
                          "QToolButton:hover { background: #33363B; }"
                          "QToolButton:checked { background: #2B6FF2; }"));

        // 浮起阴影（大模糊低透明度，近似双层观感）
        auto *shadow = new QGraphicsDropShadowEffect(this);
        shadow->setBlurRadius(28);
        shadow->setOffset(0, 6);
        shadow->setColor(QColor(0, 0, 0, 115));
        setGraphicsEffect(shadow);

        // 两行各用一个 QHBoxLayout（增删按钮只影响所在行，无需跨行列号编排）
        auto *rows = new QVBoxLayout(this);
        rows->setContentsMargins(8, 8, 8, 8);
        rows->setSpacing(6);
        auto *row1 = new QHBoxLayout;
        row1->setSpacing(5);
        auto *row2 = new QHBoxLayout;
        row2->setSpacing(5);
        rows->addLayout(row1);
        rows->addLayout(row2);

        // ---- 第一行：绘图工具（互斥，顺序即 kTools 表；【扫描】【译】插在文字与橡皮之间） ----
        // 文字识别按钮（扫描图标）：OCR 结果以文本框覆盖到画布（不打开面板）
        auto *ocrBtn = new QToolButton(this);
        ocrBtn->setFixedSize(32, 32);
        ocrBtn->setIcon(svgIcon("scan", iconColor()));
        ocrBtn->setToolTip("文字识别（OCR）：识别选区文字，以文本框覆盖到画布");
        connect(ocrBtn, &QToolButton::clicked, this, &ToolBar::ocrRequested);

        // 文字识别并翻译按钮：识别后把原始文字与翻译结果一起显示到文字栏面板
        auto *translateBtn = new QToolButton(this);
        translateBtn->setFixedSize(32, 32);
        translateBtn->setText(QStringLiteral("译"));
        translateBtn->setStyleSheet(QStringLiteral(
                                        "QToolButton { border: none; border-radius: 7px; background: transparent;"
                                        "  color: #E0E0E0; font-weight: bold; padding: 0; }"
                                        "QToolButton:hover { background: #33363B; }"));
        translateBtn->setToolTip(QStringLiteral("识别并翻译：识别选区文字，自动打开面板并显示原文与翻译结果"));
        connect(translateBtn, &QToolButton::clicked, this, &ToolBar::translateRequested);

        // 二维码识别按钮：识别选区内二维码，内容弹窗展示并复制到剪贴板
        auto *qrBtn = new QToolButton(this);
        qrBtn->setFixedSize(32, 32);
        qrBtn->setIcon(svgIcon("qrcode", iconColor()));
        qrBtn->setToolTip(QStringLiteral("识别二维码：识别选区内二维码，内容展示并复制到剪贴板"));
        connect(qrBtn, &QToolButton::clicked, this, &ToolBar::qrRequested);

        for (const ToolInfo &info : kTools)
        {
            if (info.tool == Tool::Eraser)
            {
                row1->addWidget(ocrBtn);
                row1->addWidget(translateBtn);
                row1->addWidget(qrBtn);
            }
            auto *btn = new QToolButton(this);
            btn->setFixedSize(32, 32);
            btn->setCheckable(true);
            btn->setIcon(svgIcon(info.icon, iconColor()));
            btn->setProperty("tool", static_cast<int>(info.tool));
            btn->setToolTip(QString::fromUtf8(info.tip));
            connect(btn, &QToolButton::clicked, this, &ToolBar::onToolClicked);
            row1->addWidget(btn);
            m_toolButtons.append(btn);
        }

        row1->addWidget(makeSeparator());

        // 第一行操作按钮：撤销 重做 | 保存 复制
        m_undoBtn = new QToolButton(this);
        m_undoBtn->setFixedSize(32, 32);
        m_undoBtn->setIcon(svgIcon("undo", iconColor()));
        m_undoBtn->setToolTip(QStringLiteral("撤销（Ctrl+Z）"));
        connect(m_undoBtn, &QToolButton::clicked, this, &ToolBar::undoRequested);
        row1->addWidget(m_undoBtn);

        m_redoBtn = new QToolButton(this);
        m_redoBtn->setFixedSize(32, 32);
        m_redoBtn->setIcon(svgIcon("redo", iconColor()));
        m_redoBtn->setToolTip(QStringLiteral("重做（Ctrl+Y）"));
        connect(m_redoBtn, &QToolButton::clicked, this, &ToolBar::redoRequested);
        row1->addWidget(m_redoBtn);

        row1->addWidget(makeSeparator());

        auto *saveBtn = new QToolButton(this);
        saveBtn->setFixedSize(32, 32);
        saveBtn->setIcon(svgIcon("save", iconColor()));
        saveBtn->setToolTip("保存");
        connect(saveBtn, &QToolButton::clicked, this, &ToolBar::saveRequested);
        row1->addWidget(saveBtn);

        auto *copyBtn = new QToolButton(this);
        copyBtn->setFixedSize(32, 32);
        copyBtn->setIcon(svgIcon("copy", iconColor()));
        copyBtn->setToolTip("复制");
        connect(copyBtn, &QToolButton::clicked, this, &ToolBar::copyRequested);
        row1->addWidget(copyBtn);

        // ---- 第二行：线宽 / 填充（档位值/图标由 applyWidthOptions 按工具刷新） ----
        for (int i = 0; i < 3; ++i)
        {
            auto *btn = new QToolButton(this);
            btn->setFixedSize(32, 32);
            btn->setCheckable(true);
            btn->setToolTip(i == 0 ? "小线宽" : i == 1 ? "中线宽"
                                                       : "大线宽");
            connect(btn, &QToolButton::clicked, this, &ToolBar::onWidthClicked);
            row2->addWidget(btn);
            m_widthButtons.append(btn);
        }

        // 填充/纯线（仅矩形/椭圆有效）
        m_fillBtn = new QToolButton(this);
        m_fillBtn->setFixedSize(32, 32);
        m_fillBtn->setCheckable(true);
        m_fillBtn->setIcon(fillIcon(false, 20, iconColor()));
        m_fillBtn->setToolTip("填充/纯线（仅矩形、椭圆）");
        connect(m_fillBtn, &QToolButton::clicked, this, &ToolBar::onFilledClicked);
        row2->addWidget(m_fillBtn);

        row2->addWidget(makeSeparator());

        // ---- 第二行：字号栏（pt，多选下拉 + 手动输入；非法输入回退） ----
        auto *sizeLabel = new QLabel(QStringLiteral("字号"), this);
        sizeLabel->setStyleSheet(QString("QLabel { color: %1; font-size: 12px; }").arg(iconColor().name()));
        row2->addWidget(sizeLabel);

        m_fontSizeBox = new QComboBox(this);
        m_fontSizeBox->setEditable(true);
        m_fontSizeBox->setInsertPolicy(QComboBox::NoInsert); // 手动输入不自动加入下拉列表
        m_fontSizeBox->setMaxVisibleItems(12);
        m_fontSizeBox->setFixedSize(62, 28);
        m_fontSizeBox->setStyleSheet(QStringLiteral(
            "QComboBox { background: rgba(255,255,255,18); border: 1px solid rgba(255,255,255,26); border-radius: 7px;"
            "  color: #E8EAED; padding: 2px 6px; }"
            "QComboBox:hover { border-color: #2B6FF2; }"
            "QComboBox::drop-down { border: none; width: 14px; }"
            "QComboBox QAbstractItemView { background: #26282C; color: #E8EAED;"
            "  border: 1px solid rgba(255,255,255,26); selection-background-color: #2B6FF2; }"));
        const int presets[] = {8, 9, 10, 12, 14, 16, 18, 24, 36, 48};
        for (int v : presets)
            m_fontSizeBox->addItem(QString::number(v));
        m_fontSizeBox->setCurrentText(QString::number(m_fontSize));
        // 合法性验证：允许 1.0 ~ 500.0 pt（可输入一位小数）
        auto *sizeValidator = new QDoubleValidator(1.0, 500.0, 1, this);
        sizeValidator->setNotation(QDoubleValidator::StandardNotation);
        m_fontSizeBox->setValidator(sizeValidator);
        m_fontSizeBox->lineEdit()->setPlaceholderText(QStringLiteral("—"));
        m_fontSizeBox->setToolTip(QStringLiteral("文字大小（pt）：下拉选择预设磅数，或直接输入自定义磅数"));
        // 提交路径仅两处：下拉选择 activated、回车/失焦 editingFinished
        // （不再连 returnPressed——回车时 editingFinished 必发，双连会重复提交）
        connect(m_fontSizeBox, &QComboBox::activated, this, &ToolBar::onFontSizeCommitted);
        connect(m_fontSizeBox->lineEdit(), &QLineEdit::editingFinished, this, &ToolBar::onFontSizeCommitted);
        row2->addWidget(m_fontSizeBox);

        row2->addWidget(makeSeparator());

        // ---- 第二行：颜色板（2×8 迷你色板 + 当前色大色块，高度贴合 32px 行高） ----
        auto *colorPanel = new QWidget(this);
        colorPanel->setStyleSheet("QWidget { background: transparent; }");
        auto *colorGrid = new QGridLayout(colorPanel);
        colorGrid->setContentsMargins(0, 0, 0, 0);
        colorGrid->setSpacing(3);

        // 现代调色板（2026-11 UI 现代化，替代 MS Paint 2007 经典 16 色）：
        // 黑灰白三中性 + 红/橙/黄/绿/青/蓝/紫/粉 两排完整色相，标注常用色优先
        const QColor presetColors[] = {
            QColor("#1C1C1E"),
            QColor("#64748B"),
            QColor("#F97316"),
            QColor("#EF4444"),
            QColor("#EAB308"),
            QColor("#A3E635"),
            QColor("#22C55E"),
            QColor("#14B8A6"),
            QColor("#0EA5E9"),
            QColor("#3B82F6"),
            QColor("#6366F1"),
            QColor("#A855F7"),
            QColor("#EC4899"),
            QColor("#94A3B8"),
            QColor("#F5F5F4"),
            QColor("#FFFFFF"),
        };
        for (int i = 0; i < 16; ++i)
        {
            auto *btn = new QToolButton(colorPanel);
            btn->setFixedSize(14, 14); // 2×14 + 行距 3 = 31，贴合 32px 行高
            btn->setCheckable(true);   // 当前色带主色描边
            btn->setProperty("color", presetColors[i].name());
            btn->setStyleSheet(QString(
                                   "QToolButton { border: 1px solid rgba(255,255,255,45); border-radius: 7px; background: %1; padding: 0; }"
                                   "QToolButton:hover { border: 1px solid rgba(255,255,255,90); }"
                                   "QToolButton:checked { border: 2px solid #2B6FF2; }")
                                   .arg(presetColors[i].name()));
            connect(btn, &QToolButton::clicked, this, &ToolBar::onColorClicked);
            colorGrid->addWidget(btn, i / 8, i % 8);
            m_colorButtons.append(btn);
        }
        row2->addWidget(colorPanel);

        // 当前颜色大色块（调色盘右侧）
        m_colorBig = new QToolButton(this);
        m_colorBig->setFixedSize(26, 26);
        m_colorBig->setEnabled(false);
        m_colorBig->setStyleSheet(QString(
                                      "QToolButton { border: 1px solid rgba(255,255,255,50); border-radius: 8px; background: %1; }")
                                      .arg(m_currentColor.name()));
        m_colorBig->setToolTip("当前颜色");
        row2->addWidget(m_colorBig);

        row2->addStretch(1); // 第二行窄于第一行，余量留在行尾（勿均摊到间距）

        // 默认工具：None（选择模式）—— 无按钮高亮（同时按默认档位初始化线宽按钮）
        setTool(Tool::None);
    }

    void ToolBar::setTool(Tool t)
    {
        m_tool = t;
        for (QToolButton *btn : m_toolButtons)
        {
            const auto btnTool = static_cast<Tool>(btn->property("tool").toInt());
            const bool checked = (btnTool == t);
            btn->setChecked(checked);
            if (const ToolInfo *info = findTool(btnTool))
                btn->setIcon(svgIcon(info->icon, checked ? Qt::white : iconColor()));
        }
        // 切换工具时按该工具的线宽档位刷新按钮（保持相对档位 m_widthIndex）
        applyWidthOptions();
    }

    void ToolBar::setColor(const QColor &c)
    {
        m_currentColor = c;
        m_colorBig->setStyleSheet(QString(
                                      "QToolButton { border: 1px solid rgba(255,255,255,50); border-radius: 8px; background: %1; }")
                                      .arg(c.name()));
        for (QToolButton *btn : m_colorButtons)
            btn->setChecked(btn->property("color").toString() == c.name());
    }

    void ToolBar::setWidth(qreal w)
    {
        m_width = w;
        // 找到 w 对应的档位
        const QVector<qreal> options = widthOptionsFor(m_tool);
        int idx = m_widthIndex;
        for (int i = 0; i < options.size(); ++i)
        {
            if (qFuzzyCompare(options[i], w))
            {
                idx = i;
                break;
            }
        }
        m_widthIndex = idx;
        applyWidthOptions();
    }

    void ToolBar::applyWidthOptions()
    {
        const QVector<qreal> options = widthOptionsFor(m_tool);
        const qreal maxW = options.isEmpty() ? 1.0 : options.last();
        for (int i = 0; i < m_widthButtons.size(); ++i)
        {
            QToolButton *btn = m_widthButtons[i];
            const qreal w = (i < options.size()) ? options[i] : options.last();
            btn->setProperty("width", w);
            const bool checked = (i == m_widthIndex);
            btn->setChecked(checked);
            btn->setIcon(widthIcon(w, maxW, 20, checked ? Qt::white : iconColor()));
        }
        // 仅同步 m_width（经 currentWidth() 供宿主读取）；
        // 程序性刷新（setTool/setWidth）不发射 widthSelected，避免宿主误 commit/回环
        if (!options.isEmpty() && m_widthIndex < options.size())
            m_width = options[m_widthIndex];
    }

    void ToolBar::setFilled(bool filled)
    {
        m_filled = filled;
        if (m_fillBtn)
        {
            m_fillBtn->setChecked(filled);
            m_fillBtn->setIcon(fillIcon(filled, 20, iconColor()));
        }
    }

    QColor ToolBar::iconColor() const
    {
        // 深色面板上用浅色图标保证可见性
        return m_dark ? QColor("#E0E0E0") : QColor("#1A1A1A");
    }

    void ToolBar::setUndoRedoEnabled(bool undo, bool redo)
    {
        if (m_undoBtn)
            m_undoBtn->setEnabled(undo);
        if (m_redoBtn)
            m_redoBtn->setEnabled(redo);
    }

    void ToolBar::onToolClicked()
    {
        auto *btn = qobject_cast<QToolButton *>(sender());
        if (!btn || !btn->property("tool").isValid())
            return;
        const auto t = static_cast<Tool>(btn->property("tool").toInt());
        if (t == m_tool)
        {
            // 点灭当前高亮工具 → 进入选择模式（可拖动画布）
            setTool(Tool::None);
            emit toolSelected(Tool::None);
        }
        else
        {
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
        if (c.isValid())
        {
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
        const int idx = m_widthButtons.indexOf(btn);
        if (idx < 0)
            return;
        m_widthIndex = idx;
        // 用户点击：更新互斥选中 + 图标 + m_width，并向宿主发射选中的档位值
        applyWidthOptions();
        emit widthSelected(m_width);
    }

    void ToolBar::onFilledClicked()
    {
        m_filled = m_fillBtn->isChecked();
        m_fillBtn->setIcon(fillIcon(m_filled));
        emit filledToggled(m_filled);
    }

    // ---- 字号栏 ----

    void ToolBar::setFontSize(qreal pt)
    {
        m_fontSize = pt;
        m_fontSizeMixed = false;
        syncFontSizeDisplay();
    }

    void ToolBar::clearFontSize()
    {
        m_fontSizeMixed = true;
        syncFontSizeDisplay();
    }

    void ToolBar::syncFontSizeDisplay()
    {
        if (!m_fontSizeBox)
            return;
        m_updatingFontSize = true; // 程序性刷新：避免 setCurrentText 引发用户信号
        if (m_fontSizeMixed)
            m_fontSizeBox->clearEditText();
        else
            m_fontSizeBox->setCurrentText(QString::number(m_fontSize));
        m_updatingFontSize = false;
    }

void ToolBar::onFontSizeCommitted()
    {
        if (m_updatingFontSize || !m_fontSizeBox)
            return;
        const QString txt = m_fontSizeBox->currentText().trimmed();
        bool ok = false;
        const qreal v = txt.toDouble(&ok);
        if (ok && v >= 1.0 && v <= 500.0)
        {
            m_fontSize = v;
            emit fontSizeSelected(v);
        }
        // 非法输入不发射信号，并把显示回退为当前有效值
        syncFontSizeDisplay();
    }

} // namespace poptools
