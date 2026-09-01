#pragma once

#include <QWidget>
#include <QString>

class QLabel;
class QPushButton;
class QComboBox;

namespace snipaste {

// ============================================================
// 划词翻译结果卡片
//
// 暗色主题，显示原文 + 语言选择 + 翻译按钮 + 翻译结果。
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

private slots:
    void onSwapLanguages();
    void onTranslateClicked();

private:
    QLabel *m_sourceText = nullptr;
    QLabel *m_translateText = nullptr;
    QComboBox *m_fromCombo = nullptr;
    QComboBox *m_toCombo = nullptr;
    QPushButton *m_swapBtn = nullptr;
    QPushButton *m_translateBtn = nullptr;
    QPushButton *m_closeBtn = nullptr;
    QPoint m_dragOffset;
    bool m_dragging = false;
    QString m_originalText;
};

} // namespace snipaste