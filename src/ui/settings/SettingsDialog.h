#pragma once

#include <QDialog>

class QLineEdit;
class QComboBox;

namespace snipaste {

// 设置对话框：翻译模式 / 大模型 API 配置 / 贴图默认保存文件夹
// 构造函数读取当前设置填充表单，点击 OK 时写入 Settings。
class SettingsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget *parent = nullptr);

private:
    QComboBox *m_modeCombo;
    QLineEdit *m_urlEdit;
    QLineEdit *m_keyEdit;
    QLineEdit *m_modelEdit;
    QLineEdit *m_langEdit;
    QLineEdit *m_dirEdit; // 贴图默认保存文件夹
};

} // namespace snipaste
