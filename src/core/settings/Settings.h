#pragma once

#include <QString>
#include <QStringList>
#include <QSettings>
#include <QPair>
#include <QList>

namespace poptools {

// 应用设置持久化（QSettings 存储于注册表/配置文件）
class Settings
{
public:
    // 单例入口：exe 旁 PopTools.ini（随程序分发、便于查看与备份）
    static Settings &instance();

    // 测试/工具注入用：以指定 ini 路径构造独立实例（不影响单例）
    explicit Settings(const QString &iniPath)
        : m_s(iniPath, QSettings::IniFormat)
    {
    }

    // 截图热键（默认 F1，注册为 VK_F1）
    quint32 captureHotKeyVk() const;
    void setCaptureHotKeyVk(quint32 vk);

    // 大模型 API 配置（支持 OpenAI 兼容与 Anthropic 两种接口格式，默认 DeepSeek）
    QString apiBaseUrl() const;
    void setApiBaseUrl(const QString &url);

    QString apiKey() const;
    void setApiKey(const QString &key);

    QString apiModel() const;
    void setApiModel(const QString &model);

    // API 接口格式："openai"（OpenAI 兼容 /chat/completions，默认）或
    // "anthropic"（Anthropic /v1/messages）
    QString apiFormat() const;
    void setApiFormat(const QString &format);

    // 记忆的目标语言（划词翻译卡片与 OCR 文字栏共用）：用户切换目标语言时立即保存，
    // 下次弹出/自动翻译直接使用记忆值，而不是每次都回到默认中文
    QString translateTargetLang() const;
    void setTranslateTargetLang(const QString &code);

    // 记忆的 LLM 动作（划词卡片动作下拉："translate"/"explain"/"summarize"/"rewrite"，
    // 见 Translator::llmActionKey）
    QString translateAction() const;
    void setTranslateAction(const QString &action);

    // 翻译领域（如"医学 / 法律"）与专有名词翻译对，注入 Translator 的系统提示词
    QStringList translateDomains() const;
    void setTranslateDomains(const QStringList &domains);

    typedef QPair<QString, QString> TermPair; // first=原文, second=译文
    QList<TermPair> translateTerms() const;
    void setTranslateTerms(const QList<TermPair> &terms);

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

private:
    Settings();
    // 落盘并检查可写性（安装到无写权限目录时 sync 静默失败，首次失败告警一次）
    void syncChecked();
    QSettings m_s;
};

} // namespace poptools
