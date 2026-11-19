#pragma once

#include <QDialog>
#include <QTimer>

class QComboBox;
class QFrame;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QStackedWidget;
class QToolButton;


namespace poptools {

class ApiTester;
class AppServices;
class ToggleSwitch;

// ============================================================
// 设置对话框（Win11 Fluent 范式，2026-11-19 UI 改版 M1）：
// 左侧导航 + 右侧滚动页，页内为 SettingCard 分组卡片（图标留白版：
// 标题 + 一行描述 + 右侧即时控件），开关统一用 ToggleSwitch。
//
// 交互语义（改版后）：所有设置【即时生效】——控件变更即写 Settings/
// CalendarSettings 并经 AppServices::notifySettingsChanged 广播（原
// OK/Cancel 确认制移除；文本框走 400ms 防抖，关闭前冲刷防丢尾）。
// 「测试连接」用当前值（=已保存值）发一次真实请求，结果就地显示。
// 主题跟随系统明暗（QStyleHints::colorSchemeChanged → applyTheme 全量
// 重设，色值一律 Fluent token，见 ui/theming/FluentTheme.h）。
// ============================================================
class SettingsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SettingsDialog(AppServices *services = nullptr, QWidget *parent = nullptr);
    void openCalendarPage(); // 导航定位到「桌面日历」页

protected:
    void done(int r) override; // 关闭前冲刷文本防抖提交，保证落盘完整

private slots:
    void applyTheme(); // 主题应用（构造末尾 + 系统明暗切换时重取 token）

private:
    QWidget *buildGeneralPage();
    QWidget *buildCapturePage();
    QWidget *buildTranslatePage();
    QWidget *buildMonitorPage();
    QWidget *buildGoodyPage();
    QWidget *buildCalendarPage();
    void updateAutoStartButton();
    void commitSettings();       // 表单当前值 → Settings/CalendarSettings + 广播
    void scheduleTextCommit();   // QPlainTextEdit 防抖提交入口
    void flushPendingCommit();   // 关闭前冲刷未落盘的文本变更
    void connectInstant(QWidget *w); // 按控件类型连接变更信号 → commitSettings
    QStringList parseDomains() const;                  // 逗号/顿号分隔的领域输入 → 列表
    QList<QPair<QString, QString>> parseTerms() const; // "原文=译文" 逐行 → 翻译对

    AppServices *m_services = nullptr; // 即时广播/日历功能按钮（可为空，单测场景）
    QTimer m_textCommitTimer;          // 文本编辑提交防抖（400ms 单发）

    // 导航
    QListWidget *m_nav = nullptr;
    QStackedWidget *m_pages = nullptr;

    // 通用
    QPushButton *m_autoStart = nullptr; // 开机启动开关（加入/取消）
    ToggleSwitch *m_boxVisibleSwitch = nullptr; // 收纳盒显示开关（T-069b，即时槽不走 commit）

    // 翻译
    QComboBox *m_formatCombo = nullptr; // API 接口格式（OpenAI 兼容 / Anthropic）
    QLineEdit *m_urlEdit = nullptr;
    QLineEdit *m_keyEdit = nullptr;
    QToolButton *m_keyToggle = nullptr; // 显示/隐藏 API Key（防密码框粘贴错）
    QLineEdit *m_modelEdit = nullptr;
    QPushButton *m_testBtn = nullptr; // 测试连接
    QLabel *m_testStatus = nullptr;   // 测试结果（绿/红文字）
    ApiTester *m_tester = nullptr;
    QLineEdit *m_domainEdit = nullptr; // 翻译领域（逗号分隔）
    QPlainTextEdit *m_termsEdit = nullptr; // 专有名词翻译对（每行 "原文=译文"）

    // 截图与贴图
    QLineEdit *m_dirEdit = nullptr; // 贴图默认保存文件夹

    // 活动监控
    ToggleSwitch *m_monEnabledSwitch = nullptr;
    ToggleSwitch *m_monOcrSwitch = nullptr;
    QSpinBox *m_monIdleSpin = nullptr;
    QSpinBox *m_monRetentionSpin = nullptr;

    // Goody 管家
    QLineEdit *m_gdyKeyEdit = nullptr;
    QLineEdit *m_gdyUrlEdit = nullptr;
    QLineEdit *m_gdyModelEdit = nullptr;
    QComboBox *m_gdyPermCombo = nullptr;
    QLineEdit *m_gdyTimeEdit = nullptr;
    ToggleSwitch *m_gdyAutoSwitch = nullptr;
    ToggleSwitch *m_gdyBallSwitch = nullptr; // T-050：桌面悬浮球开关
    QPlainTextEdit *m_gdyAppendix = nullptr;

    // 桌面日历（CalendarSettings JSON 存储；T-056 起日历面版只留翻页+调整）
    ToggleSwitch *m_calDutySwitch = nullptr;    // 桌面日历总开关（T-069b，即时槽不走 commit）
    ToggleSwitch *m_calLunarSwitch = nullptr;   // 显示农历 / 节气 / 节日
    ToggleSwitch *m_calWeekNumSwitch = nullptr; // 显示周数
    ToggleSwitch *m_calEventsSwitch = nullptr;  // 显示日程内容
    ToggleSwitch *m_calGlassSwitch = nullptr;   // 毛玻璃特效
    ToggleSwitch *m_calGuideSwitch = nullptr;   // 显示首次使用提示
    ToggleSwitch *m_calListSwitch = nullptr;    // 显示日历清单（=hidden 取反）
    QSpinBox *m_calFontSpin = nullptr;          // 字号 70-160%
};

} // namespace poptools
