#pragma once

#include <QDialog>

class QLineEdit;
class QComboBox;
class QPushButton;
class QCheckBox;
class QPlainTextEdit;
class QLabel;
class QToolButton;

namespace snipaste {

class ApiTester;

// 设置对话框：大模型 API 配置（含「测试连接」）/ 翻译领域与术语表 / 贴图默认保存
// 文件夹 / 开机自启 / 智能框选图像识别。构造函数读取当前设置填充表单，点击 OK 时
// 写入 Settings；「测试连接」用表单当前值（未保存前也能测）发一次真实请求。
class SettingsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget *parent = nullptr);

private:
    void updateAutoStartButton();
    QStringList parseDomains() const;          // 逗号/顿号分隔的领域输入 → 列表
    QList<QPair<QString, QString>> parseTerms() const; // "原文=译文" 逐行 → 翻译对

    QLineEdit *m_urlEdit;
    QComboBox *m_formatCombo;      // API 接口格式（OpenAI 兼容 / Anthropic）
    QLineEdit *m_keyEdit;
    QToolButton *m_keyToggle;      // 显示/隐藏 API Key（密码框看不见内容，防粘贴错）
    QLineEdit *m_modelEdit;
    QPushButton *m_testBtn;        // 测试连接
    QLabel *m_testStatus;          // 测试结果（绿/红文字）
    ApiTester *m_tester;
    QLineEdit *m_domainEdit;       // 翻译领域（逗号分隔）
    QPlainTextEdit *m_termsEdit;   // 专有名词翻译对（每行 "原文=译文"）
    QLineEdit *m_dirEdit;     // 贴图默认保存文件夹
    QPushButton *m_autoStart; // 开机启动开关（加入/取消）
    QCheckBox *m_smartImageCheck; // 智能框选·图像识别（默认关，镜像 eSearch 图像识别）
};

} // namespace snipaste
