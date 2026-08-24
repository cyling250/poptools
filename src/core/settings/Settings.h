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

    // 划词翻译热键（默认 Ctrl+Alt+Shift+X，VK_X = 0x58, MOD_CONTROL = 0x0002, MOD_ALT = 0x0001, MOD_SHIFT = 0x0004）
    quint32 selectionTranslateHotKeyVk() const;
    quint32 selectionTranslateHotKeyModifiers() const;
    void setSelectionTranslateHotKey(quint32 vk, quint32 modifiers);

    // 智能框选·图像识别（镜像 eSearch「框选.自动框选.图像识别」，默认关；
    // 关时为纯系统窗口识别，避免文字笔画产生的内外圈噪声）
    bool smartImageRecognitionEnabled() const;
    void setSmartImageRecognitionEnabled(bool on);

private:
    Settings();
    QSettings m_s;
};

} // namespace snipaste
