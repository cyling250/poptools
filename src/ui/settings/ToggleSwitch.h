#pragma once

#include <QAbstractButton>
#include <QVariantAnimation>

namespace poptools {

// ============================================================
// Win11 Fluent 开关（ToggleSwitch）：胶囊滑块式切换控件。
//
// 设计基准：Win11 ToggleSwitch——40×20 胶囊，选中态 accent 实底、未选中
// 深灰描边底，白色圆形滑块 120ms 位移动画。QAbstractButton 派生，
// isChecked()/toggled 语义与 QCheckBox 一致，可无缝替换设置页开关。
// 颜色经窗口级样式表取 Fluent token（见 FluentTheme.h），本类只管几何
// 与动画；无样式表时回退调色板高亮色（单测场景不出图，无影响）。
// ============================================================
class ToggleSwitch : public QAbstractButton
{
    Q_OBJECT
public:
    explicit ToggleSwitch(QWidget *parent = nullptr);

    QSize sizeHint() const override { return QSize(40, 20); }

protected:
    void paintEvent(QPaintEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    qreal thumbPos() const; // 滑块位置 0..1（动画当前值）

    QVariantAnimation m_anim; // 滑块位移动画（0=左/关，1=右/开）
    bool m_hover = false;
};

} // namespace poptools
