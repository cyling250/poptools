#include "TextEditingController.h"

#include "ui/ThemeColors.h"

#include <QTextEdit>
#include <QTextDocument>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QFontMetricsF>
#include <QObject>

#include "ToolBar.h"

namespace poptools
{

    void TextEditingController::init(QWidget *parent)
    {
        m_textEdit = new QTextEdit(parent);
        m_textEdit->hide();
        m_textEdit->setLineWrapMode(QTextEdit::NoWrap); // 不自动换行：宽=最长行，高=总行高
        m_textEdit->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_textEdit->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_textEdit->document()->setDocumentMargin(0); // 去除文档内边距，尺寸即文本自然尺寸
        QObject::connect(m_textEdit, &QTextEdit::textChanged, m_textEdit, [this]
                         {
        adjustSize();       // 输入内容/字号变化 → 文本框实时跟随
        syncToolbar(); });
        QObject::connect(m_textEdit, &QTextEdit::cursorPositionChanged, m_textEdit, [this]
                         {
                             if (m_editing)
                                 syncToolbar(); // 光标移动/选区变化 → 字号栏同步
                         });
        QObject::connect(m_textEdit, &QTextEdit::selectionChanged, m_textEdit, [this]
                         {
        if (m_editing)
            syncToolbar(); });
    }

    void TextEditingController::begin(const QPointF &local, int itemIndex)
    {
        m_editItem = itemIndex;
        m_editPos = local;

        // 新建文本默认字号 = 字号栏当前值（无任何文本选中时显示的"接下来输入文字的字号"）
        QFont f(QStringLiteral("Microsoft YaHei"));
        f.setBold(true);
        f.setPointSizeF(m_defaultFontSize);
        QPointF boxPos = local;
        QString initialText; // 重编辑时的原文本（先设字体再填文本，保证文档字符格式用目标字体）
        QString initialHtml; // 重编辑时的富文本（还原多字号）
        if (itemIndex >= 0)
        {
            const AnnotationItem &it = m_ctx->canvas->items[itemIndex];
            if (it.type != AnnotationType::Text)
                return;
            f = it.font;
            // OCR 等以像素字号创建的文本 → 换算为 pt（96 DPI），保证字号栏与富文本一致
            if (f.pointSizeF() <= 0 && f.pixelSize() > 0)
                f.setPointSizeF(f.pixelSize() * 72.0 / 96.0);
            initialText = it.text;
            initialHtml = it.html;
            boxPos = it.rect.topLeft();       // 待输入框贴在原文字位置
            *m_ctx->selectedItem = itemIndex; // 重编辑的文本保持选中态
            m_startColor = it.color;          // 兜底色 = 原标注色（输入期间改色不影响已输入文字）
        }
        else
        {
            *m_ctx->selectedItem = -1;
            m_startColor = m_ctx->canvas->color; // 兜底色 = 本次输入开始时的标注色
        }
        m_textEdit->setFont(f);
        if (itemIndex >= 0)
        {
            if (initialHtml.isEmpty())
                m_textEdit->setPlainText(initialText);
            else
                m_textEdit->setHtml(initialHtml); // 还原多字号片段
        }
        else
        {
            m_textEdit->clear();
        }

        // 输入中不显示白色底：背景透明，仅虚线标出输入区域；默认文本色 = 本次输入开始色
        // （与提交后渲染兜底色 it.color 一致：无显式色的文字编辑中/提交后显示同色）。
        // 虚线色取 overlay 语义主色（ThemeColors.h overlay::accent，覆盖层恒暗语境）
        m_textEdit->setStyleSheet(QString(
                                      "QTextEdit { background: transparent; border: 1px dashed %1;"
                                      " border-radius: 3px; padding: 1px 2px; color: %2; }")
                                      .arg(overlay::accent().name(), m_startColor.name()));

        // 定位：选区局部坐标 → 全局坐标 → 窗口坐标
        const QPointF global = boxPos + m_ctx->selectionRect().topLeft();
        const QPoint win = (global - QPointF(m_ctx->cap->desktop.topLeft())).toPoint();
        m_textEdit->move(win);
        m_textEdit->show();
        m_textEdit->raise();
        m_textEdit->setFocus();
        m_textEdit->selectAll();
        m_editing = true;
        adjustSize();  // 初始尺寸：宽 240 × 单行高；一旦输入即随内容自适应
        syncToolbar(); // 输入框已全选 → 字号栏反映其字号（多字号则置空）
        m_ctx->requestUpdate();
    }

    // 输入框尺寸自适应：宽 = 最长行的宽度，高 = 全部行的高度；
    // 未输入内容时保持初始长宽（240 × 单行高），一旦输入即实时贴合内容（长宽由内容决定，不可手动调整）
    void TextEditingController::adjustSize()
    {
        if (!m_editing || !m_textEdit->isVisible())
            return;
        const QSizeF doc = m_textEdit->document()->size(); // 无换行模式：即文本自然尺寸
        const int lineH = qMax(1, m_textEdit->fontMetrics().height());
        const bool empty = m_textEdit->toPlainText().isEmpty();
        const int w = empty ? 240 : qMax(8, qCeil(doc.width()) + 6);    // + 边框(1px×2) + 水平内边距(2px×2)
        const int h = empty ? lineH : qMax(8, qCeil(doc.height()) + 4); // + 边框(1px×2) + 垂直内边距(1px×2)
        m_textEdit->resize(w, h);
    }

    void TextEditingController::commit()
    {
        if (!m_editing)
            return;
        const QString text = m_textEdit->toPlainText().trimmed();
        const QString html = m_textEdit->toHtml();                   // 富文本（含多字号/多颜色片段）——须在 clear 前记录
        const QSizeF content = m_textEdit->document()->size();       // 提交前记录：宽=最长行，高=总行高
        const QFont docFont = m_textEdit->document()->defaultFont(); // 文档默认字体（family/bold/默认字号）
        const int itemIdx = m_editItem;
        const QPointF localPos = m_editPos;
        m_textEdit->hide();
        m_textEdit->clear();
        m_editing = false;
        m_editItem = -1;

        // 重编辑期间原标注可能已被撤销/删除 → 直接丢弃本次编辑
        if (itemIdx >= 0 && itemIdx >= m_ctx->canvas->items.size())
        {
            syncToolbar();
            m_ctx->requestUpdate();
            return;
        }

        if (text.isEmpty())
        {
            // 无输入：删除文本框（新建则丢弃；重编辑则删除该标注）
            if (itemIdx >= 0)
            {
                m_ctx->canvas->pushSnapshot();
                m_ctx->canvas->removeItem(itemIdx);
                *m_ctx->selectedItem = -1;
                m_ctx->stateChanged(); // 撤销栈变化 → 工具栏撤销按钮同步
            }
            syncToolbar();
            m_ctx->requestUpdate();
            return;
        }

        // 文本框尺寸 = 内容自然尺寸（宽=最长行的宽度，高=全部行的高度）
        const QSizeF box(qMax(content.width(), 8.0), qMax(content.height(), 8.0));
        if (itemIdx >= 0)
        {
            // 重编辑：更新文字内容 / 富文本（含多字号）与框尺寸（颜色保持原有）
            AnnotationItem it = m_ctx->canvas->items[itemIdx];
            m_ctx->canvas->pushSnapshot();
            it.text = text;
            it.html = html;
            it.font = docFont;
            it.rect = QRectF(it.rect.topLeft(), box);
            m_ctx->canvas->setItem(itemIdx, it);
        }
        else
        {
            // 新建文字标注：兜底色 = 输入开始时的标注色（输入期间改过颜色不影响已输入文字）
            AnnotationItem it;
            it.type = AnnotationType::Text;
            it.color = m_startColor;
            it.font = docFont;
            it.text = text;
            it.html = html;
            it.rect = QRectF(localPos, box);
            m_ctx->canvas->addItem(it);
        }
        m_ctx->stateChanged();
        syncToolbar();
        m_ctx->requestUpdate();
    }

    void TextEditingController::cancelByEsc()
    {
        if (!m_editing)
            return;
        m_textEdit->clear(); // 空文本提交 → 删除待输入框 / 标注
        commit();
    }

    void TextEditingController::detach()
    {
        m_editing = false;
        m_editItem = -1;
        if (m_textEdit)
            m_textEdit->hide();
    }

    // 字号提交：三态分发（字号栏只对"当前选中或编辑的文本"负责）
    void TextEditingController::onFontSizeSelected(qreal pt)
    {
        if (m_editing)
        {
            applyFontSizeToInput(pt);
            return;
        }
        const int selIdx = m_ctx->selectedIndex();
        if (selIdx >= 0 && selIdx < m_ctx->canvas->items.size() && m_ctx->canvas->items[selIdx].type == AnnotationType::Text)
        {
            // 选中文字标注（非输入态）：整条文本改为统一字号
            AnnotationItem it = m_ctx->canvas->items[selIdx];
            m_ctx->canvas->pushSnapshot();
            applyFontSizeToItem(it, pt);
            m_ctx->canvas->setItem(selIdx, it);
            m_ctx->stateChanged();
            syncToolbar();
            m_ctx->requestUpdate();
            return;
        }
        // 无任何文本选中：作为接下来输入文字的字号
        m_defaultFontSize = pt;
    }

    void TextEditingController::applyFontSizeToInput(qreal pt)
    {
        if (!m_editing)
            return;
        // 正在输入：有选中文字 → 立即改选中文字大小；无选中 → 设为后续输入字号
        m_textEdit->setFontPointSize(pt);
        // 无选中时同步"接下来输入文字的字号"（新建文本框 begin 用此值，
        // 否则本框后续输入用了新字号，但提交后新建文本框仍沿用旧字号）
        if (!m_textEdit->textCursor().hasSelection())
            m_defaultFontSize = pt;
        m_textEdit->setFocus(); // 焦点还给编辑框：改完字号可直接继续输入
        adjustSize();           // 字号变化 → 文本框尺寸跟随
        syncToolbar();
        m_ctx->requestUpdate();
    }

    void TextEditingController::applyColor(const QColor &c)
    {
        if (!m_editing)
            return;
        // 颜色只对"当前选中或接下来输入的内容"负责（与字号一致，不提交编辑框）。
        // 有选区 → 应用到选中文字；无选区 → 设为后续输入文字的颜色。
        // 同时更新画板默认色（新建标注/下一文本框用新色）；
        // 当前框内已输入文字（无显式色片段）渲染时仍用 m_startColor 兜底，不受影响。
        m_textEdit->setTextColor(c);
        m_ctx->canvas->color = c;
        m_textEdit->setFocus(); // 焦点还给编辑框：用户可直接继续输入，不会误入工具栏控件
        m_ctx->requestUpdate();
    }

    // ==================== 字号栏（文字大小，pt） ====================
    // 逻辑：字号栏永远体现"当前所选中，或所编辑的文本的大小"——
    //   - 正在输入：有选区 → 扫描选区片段字号（多字号置空 / 统一显示该值）
    //   - 正在输入：无选区 → 显示光标处当前格式（即接下来输入文字的字号）
    //   - 选中文字标注（非输入态）：整条扫描（多字号置空 / 统一显示该值）
    //   - 其余情况：显示默认字号（接下来输入文字的字号）

    qreal TextEditingController::fragmentPt(const QTextCharFormat &fmt, const QFont &def)
    {
        qreal pt = fmt.fontPointSize();
        if (pt > 0)
            return pt;
        // 片段未显式设置字号（继承文档默认）→ 用默认字体兜底
        pt = def.pointSizeF();
        if (pt > 0)
            return pt;
        // 像素字号 → 换算 pt（96 DPI）
        if (def.pixelSize() > 0)
            return def.pixelSize() * 72.0 / 96.0;
        return 14.0;
    }

    // 统一字号扫描：输入框选区片段（syncToolbar 输入态）与整份 HTML 文档
    // （syncToolbar 选中标注态）原本各写一遍相同的"遍历片段 + qFuzzyCompare
    // 判混排"逻辑，T-062（R1 结构 8）合并为单一数据源，遍历与判定规则不变
    qreal TextEditingController::uniformFontSize(const QTextDocument &doc, const QFont &def,
                                                 bool *mixed, int selStart, int selEnd)
    {
        qreal size = -1;
        *mixed = false;
        QTextBlock b = doc.begin();
        while (b.isValid() && !(*mixed))
        {
            for (QTextBlock::iterator it = b.begin(); !it.atEnd(); ++it)
            {
                const QTextFragment frag = it.fragment();
                const int fEnd = frag.position() + frag.length();
                if (selStart >= 0 && qMax(frag.position(), selStart) >= qMin(fEnd, selEnd))
                    continue; // 片段与选区无交集
                const qreal pt = fragmentPt(frag.charFormat(), def);
                if (size < 0)
                    size = pt;
                else if (!qFuzzyCompare(size, pt))
                {
                    *mixed = true;
                    break;
                }
            }
            b = b.next();
        }
        return size;
    }

    void TextEditingController::syncToolbar()
    {
        if (!m_ctx->toolbar)
            return;

        if (m_editing)
        {
            const QTextCursor cur = m_textEdit->textCursor();
            const QFont def = m_textEdit->document()->defaultFont();
            if (cur.hasSelection())
            {
                // 有选区：扫描选中范围内片段的字号（混排 → 置空，判定见 uniformFontSize）
                bool mixed = false;
                const qreal size = uniformFontSize(*m_textEdit->document(), def, &mixed,
                                                   cur.selectionStart(), cur.selectionEnd());
                if (mixed || size < 0)
                    m_ctx->toolbar->clearFontSize(); // 多字号 → 不显示
                else
                    m_ctx->toolbar->setFontSize(size);
            }
            else
            {
                // 无选区：显示接下来输入文字的字号（光标处当前格式）
                m_ctx->toolbar->setFontSize(fragmentPt(m_textEdit->currentCharFormat(), def));
            }
            return;
        }

        if (m_ctx->selectedIndex() >= 0 && m_ctx->selectedIndex() < m_ctx->canvas->items.size())
        {
            const AnnotationItem &it = m_ctx->canvas->items[m_ctx->selectedIndex()];
            if (it.type == AnnotationType::Text)
            {
                // 选中文字标注（非输入态）：扫描整条的字号
                qreal size = -1;
                bool mixed = false;
                if (it.html.isEmpty())
                {
                    size = it.font.pointSizeF();
                    if (size <= 0 && it.font.pixelSize() > 0)
                        size = it.font.pixelSize() * 72.0 / 96.0; // OCR 像素字号 → pt
                }
                else
                {
                    // 富文本整条扫描（与输入态选区扫描同一套规则，见 uniformFontSize）
                    QTextDocument doc;
                    doc.setDefaultFont(it.font);
                    doc.setHtml(it.html);
                    size = uniformFontSize(doc, doc.defaultFont(), &mixed);
                }
                if (mixed || size < 0)
                    m_ctx->toolbar->clearFontSize();
                else
                    m_ctx->toolbar->setFontSize(size);
                return;
            }
        }

        // 无任何文本选中：默认字号 = 接下来输入文字的字号
        m_ctx->toolbar->setFontSize(m_defaultFontSize);
    }

    void TextEditingController::applyFontSizeToItem(AnnotationItem &it, qreal pt)
    {
        QTextDocument doc;
        doc.setDocumentMargin(0);
        if (it.html.isEmpty())
        {
            doc.setDefaultFont(it.font);
            doc.setPlainText(it.text);
        }
        else
        {
            doc.setDefaultFont(it.font);
            doc.setHtml(it.html);
        }
        {
            QTextCursor cur(&doc);
            cur.select(QTextCursor::Document);
            QTextCharFormat fmt;
            fmt.setFontPointSize(pt);
            cur.mergeCharFormat(fmt);
        }
        // 文档默认字号同步为新值：body 的 font-size 保持一致，重新编辑时后续输入用新字号
        QFont def = doc.defaultFont();
        def.setPointSizeF(pt);
        doc.setDefaultFont(def);
        it.font = def;
        it.html = doc.toHtml();
        it.text = doc.toPlainText();
        // 重算框尺寸 = 内容自然尺寸（不换行：宽=最长行，高=总行高）
        const QSizeF box = doc.size();
        it.rect = QRectF(it.rect.topLeft(),
                         QSizeF(qMax(box.width(), 8.0), qMax(box.height(), 8.0)));
    }

} // namespace poptools
