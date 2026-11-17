#pragma once

#include <QByteArray>
#include <QColor>
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

    // ---- 活动监控 ----
    // 总开关（默认关闭；开启即授权键盘键入记录 + 窗口时间线 + OCR 快照，
    // 钩子常驻，红线2豁免条款见 docs/handover.md）
    bool monitorEnabled() const;
    void setMonitorEnabled(bool on);
    // 密码框自动打码（焦点在密码框时记 ● 占位，不落明文）
    bool monitorMaskPasswords() const;
    void setMonitorMaskPasswords(bool on);
    // 输入空闲阈值（秒）：超过即判定离开，时间线段以最后输入时刻收尾
    int monitorIdleThresholdSec() const;
    void setMonitorIdleThresholdSec(int sec);
    // 日志保留天数（钳制 1-3650，非法手改回退 1；过期日目录随启动清理）
    int monitorRetentionDays() const;
    void setMonitorRetentionDays(int days);
    // 中文上屏 OCR 快照（输入法上屏→截前台窗口→OCR→记增量→删图）
    bool monitorOcrSnapshot() const;
    void setMonitorOcrSnapshot(bool on);

    // ---- Goody 管家（内置大模型助手） ----
    // API 配置（OpenAI 兼容，默认 DeepSeek；Key 只存本机 ini）
    QString assistantApiKey() const;
    void setAssistantApiKey(const QString &key);
    QString assistantApiBaseUrl() const;
    void setAssistantApiBaseUrl(const QString &url);
    QString assistantApiModel() const;
    void setAssistantApiModel(const QString &model);
    // 呼出热键（默认 Ctrl+Alt+G：VK_G=0x47, MOD_CONTROL|MOD_ALT=0x0003）
    quint32 assistantHotKeyVk() const;
    quint32 assistantHotKeyModifiers() const;
    void setAssistantHotKey(quint32 vk, quint32 modifiers);
    // 每日工作记录生成时刻（"HH:mm"，默认 22:00）与自动开关
    QString assistantSummaryTime() const;
    void setAssistantSummaryTime(const QString &hhmm);
    bool assistantAutoSummarize() const;
    void setAssistantAutoSummarize(bool on);
    // 权限模式："ask"逐条确认 / "smart"智能自动 / "auto"全自动
    QString assistantPermMode() const;
    void setAssistantPermMode(const QString &mode);
    // "总是允许此类"工具名清单（用户在确认卡片勾选后累积）
    QStringList assistantAlwaysAllowTools() const;
    void setAssistantAlwaysAllowTools(const QStringList &tools);
    // Smart 模式的 shell 安全命令前缀白名单
    QStringList assistantShellAllowPrefixes() const;
    void setAssistantShellAllowPrefixes(const QStringList &prefixes);
    // 人格附言（用户追加到 Goody 系统提示词末尾）
    QString assistantPersonaAppendix() const;
    void setAssistantPersonaAppendix(const QString &text);
    // 聊天窗口几何（base64 序列化；空 = 未记忆，首开居中弹出）。
    // T-050-P2 新键：窗口升级为居中卡片聊天窗，几何与旧键（assistant/
    // geometry，440×600 竖版）解耦，避免旧尺寸撑爆新布局
    QByteArray assistantWindowGeometry() const;
    void setAssistantWindowGeometry(const QByteArray &state);
    // 桌面悬浮球（T-050 阶段一）：显示开关与记忆位置。位置为 "x,y" 字符串
    //（空 = 未记忆，取默认屏幕右上角；解析/钳制逻辑在 ui/goody/GoodyBallLogic.h）
    bool assistantBallEnabled() const;
    void setAssistantBallEnabled(bool on);
    QString assistantBallPos() const;
    void setAssistantBallPos(const QString &xy);

    // ---- 更新自查 ----
    // 用户选择"跳过此版本"的 Release 版本号（如 "v0.5.0"）；启动自动检查时
    // 命中则静默，不再弹提示。手动检查不受此键影响
    QString updateIgnoredVersion() const;
    void setUpdateIgnoredVersion(const QString &version);

private:
    Settings();
    // 落盘并检查可写性（安装到无写权限目录时 sync 静默失败，首次失败告警一次）
    void syncChecked();
    QSettings m_s;
};

} // namespace poptools
