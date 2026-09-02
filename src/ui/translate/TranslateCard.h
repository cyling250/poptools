#pragma once

#include <QWidget>
#include <QString>

class QTextEdit;
class QPushButton;
class QComboBox;

namespace poptools {

// ============================================================
// 划词翻译结果卡片
//
// 跟随系统明暗主题（浅色=白底黑字浅灰线，深色=黑底白字），
// 显示原文 + 语言选择 + 翻译按钮 + 翻译结果。
// 无边框窗口：按住卡片空白处拖动移动；四边边缘拖拽调整大小
// （调整后不再随内容自适应）；文本区为只读 QTextEdit，
// 内容超出可视区时内部滚动，翻译结果下方可一键复制。
// 全局单例复用（不随关闭销毁）：隐藏时统一清空原文与译文，
// 下次划词直接复用并显示在鼠标位置。
// ============================================================
class TranslateCard : public QWidget
{
    Q_OBJECT
public:
    explicit TranslateCard(QWidget *parent = nullptr);

    // 设置原文（卡片弹出时调用）
    void setSourceText(const QString &text);
    // 更新翻译结果
    void setTranslateResult(const QString &text);
    // 显示错误信息
    void showError(const QString &errorMsg);
    // 立即执行一次翻译（弹出卡片后自动调用；用户亦可点击翻译按钮再次翻译）
    void translateNow();
    // 定位到鼠标位置附近（屏幕边界内）
    void moveNearCursor();

signals:
    // 用户关闭卡片
    void closed();
    // 用户点击翻译按钮
    void translateRequested(const QString &text, const QString &fromLang, const QString &toLang);

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private slots:
    void onSwapLanguages();
    void onTranslateClicked();

private:
    // 应用当前系统明暗主题的整套样式（构造与 colorSchemeChanged 时调用）
    void applyTheme();
    // 内容更新后按内容自适应整体高度（用户手动调整过大小后不再干预）
    void refit();
    // 单个文本区按文档实际高度自适应，限制在 [kTextEditMinHeight, maxH]，超出时走滚动条
    void fitTextEdit(QTextEdit *edit, int maxH);
    // 解除文本区固定高度，交给布局按拉伸系数分配（用户开始拖拽调大小时调用）
    void beginUserResize();

    QTextEdit *m_sourceText = nullptr;
    QTextEdit *m_translateText = nullptr;
    QComboBox *m_fromCombo = nullptr;
    QComboBox *m_toCombo = nullptr;
    QPushButton *m_swapBtn = nullptr;
    QPushButton *m_translateBtn = nullptr;
    QPushButton *m_copyBtn = nullptr;
    QPushButton *m_closeBtn = nullptr;
    QPoint m_dragOffset;
    bool m_dragging = false;
    // 边缘拖拽调整大小状态
    Qt::Edges m_resizeEdges;
    QPoint m_pressGlobalPos;
    QRect m_pressGeometry;
    bool m_userResized = false; // 用户手动调整过大小后，内容更新不再自动改尺寸
    QString m_originalText;
    QString m_editSheet;        // 当前主题的文本框内联样式（applyTheme 生成）
};

} // namespace poptools
