#include "CalendarSettingsForm.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace poptools {

CalendarSettingsForm::CalendarSettingsForm(CalendarSettings &settings, QWidget *parent)
    : QDialog(parent)
    , m_settings(settings)
{
    setWindowTitle(QStringLiteral("日历设置"));
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    setWindowFlag(Qt::WindowStaysOnTopHint, true);
    setFixedSize(460, 430);
    setStyleSheet(QStringLiteral(
        "QDialog { background: #1E2026; color: #EBEBEB; font-size: 13px; }"
        "QCheckBox { color: #E6E6E6; spacing: 8px; }"
        "QLabel { color: #DCDCDC; }"
        "QSpinBox { background: #2D3038; color: #FFFFFF; border: 1px solid #3C4048; "
        "padding: 4px 6px; }"
        "QPushButton { background: #383C46; color: #FFFFFF; border: 1px solid #5A606E; "
        "padding: 8px 20px; border-radius: 4px; }"));

    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(20, 16, 20, 16);
    lay->setSpacing(10);

    auto *title = new QLabel(QStringLiteral("日历设置"), this);
    QFont titleFont;
    titleFont.setPixelSize(20);
    titleFont.setBold(true);
    title->setFont(titleFont);
    title->setStyleSheet(QStringLiteral("color: #FFFFFF;"));
    lay->addWidget(title);
    lay->addSpacing(8);

    auto *lunar = new QCheckBox(QStringLiteral("显示农历 / 节气 / 节日"), this);
    lunar->setChecked(settings.showLunar);
    auto *weekNumbers = new QCheckBox(QStringLiteral("显示周数"), this);
    weekNumbers->setChecked(settings.showWeekNumbers);
    auto *events = new QCheckBox(QStringLiteral("显示日程内容"), this);
    events->setChecked(settings.showEvents);
    auto *glass = new QCheckBox(QStringLiteral("毛玻璃特效（模糊壁纸背景）"), this);
    glass->setChecked(settings.glassEffect);
    auto *guide = new QCheckBox(QStringLiteral("显示首次使用提示"), this);
    guide->setChecked(settings.showGuide);
    lay->addWidget(lunar);
    lay->addWidget(weekNumbers);
    lay->addWidget(events);
    lay->addWidget(glass);
    lay->addWidget(guide);
    lay->addSpacing(10);

    auto *fontRow = new QHBoxLayout();
    auto *fontLabel = new QLabel(QStringLiteral("字体大小："), this);
    auto *fontScale = new QSpinBox(this);
    fontScale->setRange(70, 160);
    fontScale->setSingleStep(5);
    fontScale->setValue(std::clamp(settings.fontScale, 70, 160));
    fontScale->setFixedWidth(80);
    auto *percent = new QLabel(QStringLiteral("%"), this);
    fontRow->addWidget(fontLabel);
    fontRow->addWidget(fontScale);
    fontRow->addWidget(percent);
    fontRow->addStretch();
    lay->addLayout(fontRow);
    lay->addSpacing(6);

    auto *themeLabel =
        new QLabel(QStringLiteral("主题皮肤：默认（深色毛玻璃）    数据目录：") +
                       CalendarSettings::dataDir(),
                   this);
    themeLabel->setStyleSheet(QStringLiteral("color: #AAAAAA;"));
    themeLabel->setWordWrap(true);
    lay->addWidget(themeLabel);
    lay->addStretch();

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("保存"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    lay->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    connect(this, &QDialog::accepted, this, [this, lunar, weekNumbers, events, glass, guide,
                                              fontScale] {
        m_settings.showLunar = lunar->isChecked();
        m_settings.showWeekNumbers = weekNumbers->isChecked();
        m_settings.showEvents = events->isChecked();
        m_settings.glassEffect = glass->isChecked();
        m_settings.showGuide = guide->isChecked();
        m_settings.fontScale = fontScale->value();
    });
}

} // namespace poptools
