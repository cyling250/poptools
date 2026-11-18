#pragma once

#include <QColor>
#include <QDialog>

class QCheckBox;
class QComboBox;
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

// ============================================================
// 设置对话框（左侧导航 + 分页，通用 / 截图与贴图 / 翻译 / 监控 / Goody /
// 桌面日历）。
//
// 构造函数读取当前设置填充表单，点击 OK 时写入 Settings；
// 「测试连接」用表单当前值（未保存前也能测）发一次真实请求。
// 桌面日历页读写 CalendarSettings（JSON），保存后经 AppServices 槽位
// 通知日历宿主重载；功能按钮（搜索/选日期/导入/导出）即时生效。
// ============================================================
class SettingsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SettingsDialog(AppServices *services = nullptr, QWidget *parent = nullptr);
    void openCalendarPage(); // 导航定位到「桌面日历」页

private slots:
    void applyTheme(); // 主题应用（构造末尾 + 系统明暗切换时重取 ThemeColors）

private:
    QWidget *buildGeneralPage();
    QWidget *buildCapturePage();
    QWidget *buildTranslatePage();
    QWidget *buildMonitorPage();
    QWidget *buildGoodyPage();
    QWidget *buildCalendarPage();
    void updateAutoStartButton();
    void applySettingsFromForm(); // OK 保存：表单当前值 → Settings/CalendarSettings（accepted 时调用）
    QStringList parseDomains() const;                  // 逗号/顿号分隔的领域输入 → 列表
    QList<QPair<QString, QString>> parseTerms() const; // "原文=译文" 逐行 → 翻译对

    AppServices *m_services = nullptr; // 日历功能按钮/保存后重载（可为空，单测场景）

    // 导航
    QListWidget *m_nav = nullptr;
    QStackedWidget *m_pages = nullptr;

    // 通用
    QPushButton *m_autoStart = nullptr; // 开机启动开关（加入/取消）

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
    QCheckBox *m_monEnabledCheck = nullptr;
    QCheckBox *m_monOcrCheck = nullptr;
    QSpinBox *m_monIdleSpin = nullptr;
    QSpinBox *m_monRetentionSpin = nullptr;

    // Goody 管家
    QLineEdit *m_gdyKeyEdit = nullptr;
    QLineEdit *m_gdyUrlEdit = nullptr;
    QLineEdit *m_gdyModelEdit = nullptr;
    QComboBox *m_gdyPermCombo = nullptr;
    QLineEdit *m_gdyTimeEdit = nullptr;
    QCheckBox *m_gdyAutoCheck = nullptr;
    QCheckBox *m_gdyBallCheck = nullptr; // T-050：桌面悬浮球开关
    QPlainTextEdit *m_gdyAppendix = nullptr;

    // 桌面日历（CalendarSettings JSON 存储；T-056 起日历面版只留翻页+调整）
    QCheckBox *m_calLunarCheck = nullptr;     // 显示农历 / 节气 / 节日
    QCheckBox *m_calWeekNumCheck = nullptr;   // 显示周数
    QCheckBox *m_calEventsCheck = nullptr;    // 显示日程内容
    QCheckBox *m_calGlassCheck = nullptr;     // 毛玻璃特效
    QCheckBox *m_calGuideCheck = nullptr;     // 显示首次使用提示
    QCheckBox *m_calListCheck = nullptr;      // 显示日历清单（=hidden 取反）
    QSpinBox *m_calFontSpin = nullptr;        // 字号 70-160%
};

} // namespace poptools
