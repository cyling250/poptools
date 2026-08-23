#pragma once

#include <QDialog>

class QLineEdit;
class QComboBox;
class QPushButton;
class QCheckBox;

namespace snipaste {

// 设置对话框：大模型 API 配置 / 贴图默认保存文件夹 / 开机自启 / 智能框选图像识别
// 构造函数读取当前设置填充表单，点击 OK 时写入 Settings。
class SettingsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget *parent = nullptr);

private:
    void updateAutoStartButton();

    QLineEdit *m_urlEdit;
    QLineEdit *m_keyEdit;
    QLineEdit *m_modelEdit;
    QLineEdit *m_dirEdit;     // 贴图默认保存文件夹
    QPushButton *m_autoStart; // 开机启动开关（加入/取消）
    QCheckBox *m_smartImageCheck; // 智能框选·图像识别（默认关，镜像 eSearch 图像识别）
};

} // namespace snipaste
