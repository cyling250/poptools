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
class QSlider;
class QSpinBox;
class QStackedWidget;
class QToolButton;

namespace poptools {

class ApiTester;
class CalendarStore;

// ============================================================
// 设置对话框（左侧导航 + 分页，通用 / 截图与贴图 / 翻译 / 桌面日历）。
//
// 构造函数读取当前设置填充表单，点击 OK 时写入 Settings；
// 「测试连接」用表单当前值（未保存前也能测）发一次真实请求。
// 分类管理（CategoryManagerDialog）改动即时生效，与 OK/Cancel 无关。
// ============================================================
class SettingsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SettingsDialog(CalendarStore *calendarStore, QWidget *parent = nullptr);

private slots:
    void applyTheme(); // 主题应用（构造末尾 + 系统明暗切换时重取 ThemeColors）

private:
    QWidget *buildGeneralPage();
    QWidget *buildCapturePage();
    QWidget *buildTranslatePage();
    QWidget *buildCalendarPage();
    QWidget *buildOrganizerPage();
    QWidget *buildMonitorPage();
    QWidget *buildGoodyPage();
    void updateAutoStartButton();
    void updateCalendarColorButtons();
    QStringList parseDomains() const;                  // 逗号/顿号分隔的领域输入 → 列表
    QList<QPair<QString, QString>> parseTerms() const; // "原文=译文" 逐行 → 翻译对

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

    // 桌面日历
    QCheckBox *m_calShowCheck = nullptr;
    QSlider *m_calOpacity = nullptr;
    QLabel *m_calOpacityValue = nullptr;
    QSlider *m_calTextOpacity = nullptr;
    QLabel *m_calTextOpacityValue = nullptr;
    QPushButton *m_calBgBtn = nullptr;
    QPushButton *m_calAccentBtn = nullptr;
    QLabel *m_calBgSwatch = nullptr;
    QLabel *m_calAccentSwatch = nullptr;
    QColor m_calBg;
    QColor m_calAccent;
    CalendarStore *m_calendarStore = nullptr;

    // 桌面收纳盒
    QCheckBox *m_orgShowCheck = nullptr;

    // 活动监控
    QCheckBox *m_monEnabledCheck = nullptr;
    QCheckBox *m_monMaskCheck = nullptr;
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
};

} // namespace poptools
