#pragma once

#include <QDialog>

#include "core/calendar/CalendarSettings.h"

namespace poptools {

// ============================================================
// 日历设置（照搬 rlqd CalendarClone/SettingsForm.cs：深色固定对话框，
// 五个开关 + 字体大小 70-160%）。
// ============================================================
class CalendarSettingsForm : public QDialog
{
    Q_OBJECT
public:
    explicit CalendarSettingsForm(CalendarSettings &settings, QWidget *parent = nullptr);

private:
    CalendarSettings &m_settings;
};

} // namespace poptools
