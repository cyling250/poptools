#pragma once

#include <QString>
#include <QSettings>

namespace snipaste {

// 应用设置持久化（QSettings 存储于注册表/配置文件）
class Settings
{
public:
    static Settings &instance();

    // 截图热键（默认 F1，注册为 VK_F1）
    quint32 captureHotKeyVk() const;
    void setCaptureHotKeyVk(quint32 vk);

    // 大模型 API 配置（OpenAI 兼容接口，默认 DeepSeek）
    QString apiBaseUrl() const;
    void setApiBaseUrl(const QString &url);

    QString apiKey() const;
    void setApiKey(const QString &key);

    QString apiModel() const;
    void setApiModel(const QString &model);

    // 贴图默认保存文件夹（F3 张贴时自动落盘）
    QString pinSaveDir() const;
    void setPinSaveDir(const QString &dir);

    // 开机自启（HKCU\...\Run 注册表项）
    bool autoStartEnabled() const;
    void setAutoStartEnabled(bool on);

private:
    Settings();
    QSettings m_s;
};

} // namespace snipaste
