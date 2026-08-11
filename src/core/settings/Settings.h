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

    // 翻译模式: "api" 或 "local"
    QString translateMode() const;
    void setTranslateMode(const QString &mode);

    // 大模型 API 配置（OpenAI 兼容接口）
    QString apiBaseUrl() const;
    void setApiBaseUrl(const QString &url);

    QString apiKey() const;
    void setApiKey(const QString &key);

    QString apiModel() const;
    void setApiModel(const QString &model);

    // 目标语言（如 "en", "zh"）
    QString targetLang() const;
    void setTargetLang(const QString &lang);

private:
    Settings();
    QSettings m_s;
};

} // namespace snipaste
