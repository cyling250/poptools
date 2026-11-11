#include "Settings.h"

#include <QCoreApplication>
#include <QDir>
#include <QTime>

namespace poptools {

// 配置文件存放在可执行文件旁（构建输出/安装目录），随程序打包分发、便于查看
Settings::Settings()
    : m_s(QCoreApplication::applicationDirPath() + "/PopTools.ini",
          QSettings::IniFormat)
{
}

// 开机启动注册表项：HKCU\Software\Microsoft\Windows\CurrentVersion\Run
static QSettings autoStartRegistry()
{
    return QSettings("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                     QSettings::NativeFormat);
}

// 热键修饰键合法性（R3 #13，对照 vk 的 0x01-0xFE 校验）：热键无设置 UI，
// 只能手改 INI——0（无修饰键）会全局吞键，含未知位同理非法。
// 0x1 MOD_ALT | 0x2 MOD_CONTROL | 0x4 MOD_SHIFT | 0x8 MOD_WIN
static bool validHotkeyModifiers(quint32 mod)
{
    return mod != 0 && (mod & ~0xFu) == 0;
}

Settings &Settings::instance()
{
    static Settings inst;
    return inst;
}

// 落盘并检查可写性：QSettings::sync 在目录无写权限（如 Program Files）时
// 静默失败，用户改过的设置全部不保存且毫无感知——首次失败时告警一次
void Settings::syncChecked()
{
    m_s.sync();
    if (m_s.status() == QSettings::AccessError) {
        static bool warned = false;
        if (!warned) {
            warned = true;
            qWarning("PopTools.ini 不可写（目录权限不足？），设置将无法保存");
        }
    }
}

quint32 Settings::captureHotKeyVk() const
{
    // 手改 INI 写入非法值（0/越界）时回退默认：vk=0 传进 RegisterHotKey
    // 必然失败，且失败提示会误导为"快捷键被占用"
    const quint32 vk = m_s.value("hotkey/captureVk", 0x70 /* F1 */).toUInt(); // VK_F1 = 0x70
    return (vk >= 0x01 && vk <= 0xFE) ? vk : 0x70;
}

void Settings::setCaptureHotKeyVk(quint32 vk)
{
    m_s.setValue("hotkey/captureVk", vk);
    syncChecked(); // 立即落盘，异常退出也不丢配置
}

QString Settings::apiBaseUrl() const
{
    return m_s.value("translate/apiBaseUrl", "https://api.deepseek.com/v1").toString();
}

void Settings::setApiBaseUrl(const QString &url)
{
    m_s.setValue("translate/apiBaseUrl", url);
    syncChecked();
}

QString Settings::apiKey() const
{
    // 默认无 API Key，由用户在设置中填写
    return m_s.value("translate/apiKey", "").toString();
}

void Settings::setApiKey(const QString &key)
{
    m_s.setValue("translate/apiKey", key);
    syncChecked();
}

QString Settings::apiModel() const
{
    return m_s.value("translate/apiModel", "deepseek-v4-flash").toString();
}

void Settings::setApiModel(const QString &model)
{
    m_s.setValue("translate/apiModel", model);
    syncChecked();
}

// API 接口格式，默认 OpenAI 兼容（存量配置无此键时行为不变）
QString Settings::apiFormat() const
{
    return m_s.value("translate/apiFormat", "openai").toString();
}

void Settings::setApiFormat(const QString &format)
{
    m_s.setValue("translate/apiFormat", format);
    syncChecked();
}

// 记忆的目标语言，默认中文（zh）
QString Settings::translateTargetLang() const
{
    return m_s.value("translate/targetLang", "zh").toString();
}

void Settings::setTranslateTargetLang(const QString &code)
{
    m_s.setValue("translate/targetLang", code);
    syncChecked();
}

// 记忆的 LLM 动作，默认翻译（translate）
QString Settings::translateAction() const
{
    return m_s.value("translate/action", "translate").toString();
}

void Settings::setTranslateAction(const QString &action)
{
    m_s.setValue("translate/action", action);
    syncChecked();
}

QStringList Settings::translateDomains() const
{
    return m_s.value("translate/domains", QStringList()).toStringList();
}

void Settings::setTranslateDomains(const QStringList &domains)
{
    m_s.setValue("translate/domains", domains);
    syncChecked();
}

// 术语对用两个平行的字符串列表存储（termSources/termTargets），
// 避免自选分隔符与术语内容冲突
QList<Settings::TermPair> Settings::translateTerms() const
{
    const QStringList sources = m_s.value("translate/termSources", QStringList()).toStringList();
    const QStringList targets = m_s.value("translate/termTargets", QStringList()).toStringList();
    QList<TermPair> terms;
    const int n = qMin(sources.size(), targets.size());
    for (int i = 0; i < n; ++i)
        terms.append({sources.at(i), targets.at(i)});
    return terms;
}

void Settings::setTranslateTerms(const QList<TermPair> &terms)
{
    QStringList sources, targets;
    for (const TermPair &term : terms) {
        if (term.first.isEmpty() || term.second.isEmpty())
            continue;
        sources << term.first;
        targets << term.second;
    }
    m_s.setValue("translate/termSources", sources);
    m_s.setValue("translate/termTargets", targets);
    syncChecked();
}

QString Settings::pinSaveDir() const
{
    // 默认贴图文件夹：软件目录（exe 旁）下的 tempPopTools
    const QString def = QCoreApplication::applicationDirPath() + "/tempPopTools";
    return m_s.value("pin/saveDir", def).toString();
}

void Settings::setPinSaveDir(const QString &dir)
{
    m_s.setValue("pin/saveDir", dir);
    syncChecked();
}

bool Settings::autoStartEnabled() const
{
    return autoStartRegistry().contains("PopTools");
}

void Settings::setAutoStartEnabled(bool on)
{
    QSettings reg = autoStartRegistry();
    if (on) {
        // Run 值要求带引号的可执行文件路径
        const QString exe =
            "\"" + QDir::toNativeSeparators(QCoreApplication::applicationFilePath()) + "\"";
        reg.setValue("PopTools", exe);
    } else {
        reg.remove("PopTools");
    }
    reg.sync();
}

// 划词翻译热键：默认 Ctrl+Alt+Shift+X（VK_X = 0x58, MOD_CONTROL = 0x0002, MOD_ALT = 0x0001, MOD_SHIFT = 0x0004）
quint32 Settings::selectionTranslateHotKeyVk() const
{
    const quint32 vk = m_s.value("hotkey/selectionTranslateVk", 0x58 /* X */).toUInt();
    return (vk >= 0x01 && vk <= 0xFE) ? vk : 0x58; // 非法值回退默认（同 captureVk）
}

quint32 Settings::selectionTranslateHotKeyModifiers() const
{
    const quint32 mod =
        m_s.value("hotkey/selectionTranslateMod", 0x0007 /* MOD_CONTROL|MOD_ALT|MOD_SHIFT */).toUInt();
    if (!validHotkeyModifiers(mod))
    {
        qWarning("Settings: 非法热键修饰键 hotkey/selectionTranslateMod=0x%x，回退默认",
                 mod);
        return 0x0007;
    }
    return mod;
}

void Settings::setSelectionTranslateHotKey(quint32 vk, quint32 modifiers)
{
    m_s.setValue("hotkey/selectionTranslateVk", vk);
    m_s.setValue("hotkey/selectionTranslateMod", modifiers);
    syncChecked();
}

// ---- 桌面日历 ----

bool Settings::calendarEnabled() const
{
    return m_s.value("calendar/enabled", false).toBool();
}

void Settings::setCalendarEnabled(bool on)
{
    m_s.setValue("calendar/enabled", on);
    syncChecked();
}

int Settings::calendarOpacity() const
{
    // 钳制在 20-100：过小则窗口不可见，用户会以为程序坏了
    const int v = m_s.value("calendar/opacity", 95).toInt();
    return qBound(20, v, 100);
}

void Settings::setCalendarOpacity(int percent)
{
    m_s.setValue("calendar/opacity", qBound(20, percent, 100));
    syncChecked();
}

int Settings::calendarTextOpacity() const
{
    // 文字/强调色 alpha 系数（背景透明度分离后新增）；过低文字难辨认
    const int v = m_s.value("calendar/textOpacity", 100).toInt();
    return qBound(30, v, 100);
}

void Settings::setCalendarTextOpacity(int percent)
{
    m_s.setValue("calendar/textOpacity", qBound(30, percent, 100));
    syncChecked();
}

QColor Settings::calendarBgColor() const
{
    const QColor c(m_s.value("calendar/bgColor", "#FFFFFF").toString());
    return c.isValid() ? c : QColor("#FFFFFF");
}

void Settings::setCalendarBgColor(const QColor &color)
{
    m_s.setValue("calendar/bgColor", color.isValid() ? color.name(QColor::HexRgb)
                                                     : QColor("#FFFFFF").name(QColor::HexRgb));
    syncChecked();
}

QColor Settings::calendarAccentColor() const
{
    const QColor c(m_s.value("calendar/accentColor", "#2B6FF2").toString());
    return c.isValid() ? c : QColor("#2B6FF2");
}

void Settings::setCalendarAccentColor(const QColor &color)
{
    m_s.setValue("calendar/accentColor",
                 color.isValid() ? color.name(QColor::HexRgb)
                                 : QColor("#2B6FF2").name(QColor::HexRgb));
    syncChecked();
}

QByteArray Settings::calendarGeometry() const
{
    return m_s.value("calendar/geometry").toByteArray();
}

void Settings::setCalendarGeometry(const QByteArray &state)
{
    m_s.setValue("calendar/geometry", state);
    syncChecked();
}

QByteArray Settings::calendarSplitter() const
{
    return m_s.value("calendar/splitter").toByteArray();
}

void Settings::setCalendarSplitter(const QByteArray &state)
{
    m_s.setValue("calendar/splitter", state);
    syncChecked();
}

// T-048 逃生口：calendar/bandMode（默认 true；false=固定模式回退
// DesktopStack 贴底路径）
bool Settings::calendarBandMode() const
{
    return m_s.value("calendar/bandMode", true).toBool();
}

void Settings::setCalendarBandMode(bool on)
{
    m_s.setValue("calendar/bandMode", on);
    syncChecked();
}

// ---- 桌面收纳盒 ----

bool Settings::organizerEnabled() const
{
    return m_s.value("organizer/enabled", false).toBool();
}

void Settings::setOrganizerEnabled(bool on)
{
    m_s.setValue("organizer/enabled", on);
    syncChecked();
}

// T-048 逃生口：box/bandMode（默认 true；false=回退 DesktopStack 贴底路径）
bool Settings::boxBandMode() const
{
    return m_s.value("box/bandMode", true).toBool();
}

void Settings::setBoxBandMode(bool on)
{
    m_s.setValue("box/bandMode", on);
    syncChecked();
}

// ---- 活动监控 ----

bool Settings::monitorEnabled() const
{
    // 默认关闭：键入记录涉及高隐私，必须用户显式授权
    return m_s.value("monitor/enabled", false).toBool();
}

void Settings::setMonitorEnabled(bool on)
{
    m_s.setValue("monitor/enabled", on);
    syncChecked();
}

bool Settings::monitorMaskPasswords() const
{
    return m_s.value("monitor/maskPasswords", true).toBool();
}

void Settings::setMonitorMaskPasswords(bool on)
{
    m_s.setValue("monitor/maskPasswords", on);
    syncChecked();
}

int Settings::monitorIdleThresholdSec() const
{
    // 钳制 10-3600：过小把思考间隙算成离开，过大失去"离开"语义
    const int v = m_s.value("monitor/idleThresholdSec", 120).toInt();
    return qBound(10, v, 3600);
}

void Settings::setMonitorIdleThresholdSec(int sec)
{
    m_s.setValue("monitor/idleThresholdSec", qBound(10, sec, 3600));
    syncChecked();
}

int Settings::monitorRetentionDays() const
{
    // 钳制 1-3650；0 需手改 INI 才能永久保留（设置页不给永久档，防误设）
    const int v = m_s.value("monitor/retentionDays", 14).toInt();
    return qBound(1, v, 3650);
}

void Settings::setMonitorRetentionDays(int days)
{
    m_s.setValue("monitor/retentionDays", qBound(1, days, 3650));
    syncChecked();
}

bool Settings::monitorOcrSnapshot() const
{
    return m_s.value("monitor/ocrSnapshot", true).toBool();
}

void Settings::setMonitorOcrSnapshot(bool on)
{
    m_s.setValue("monitor/ocrSnapshot", on);
    syncChecked();
}

// ---- Goody 管家 ----

QString Settings::assistantApiKey() const
{
    return m_s.value("assistant/apiKey", "").toString();
}

void Settings::setAssistantApiKey(const QString &key)
{
    m_s.setValue("assistant/apiKey", key);
    syncChecked();
}

QString Settings::assistantApiBaseUrl() const
{
    return m_s.value("assistant/apiBaseUrl", "https://api.deepseek.com/v1").toString();
}

void Settings::setAssistantApiBaseUrl(const QString &url)
{
    m_s.setValue("assistant/apiBaseUrl", url);
    syncChecked();
}

QString Settings::assistantApiModel() const
{
    // 默认 deepseek-v4-flash（与翻译模块同款）：调试期便宜快，用户可改 deepseek-chat
    return m_s.value("assistant/apiModel", "deepseek-v4-flash").toString();
}

void Settings::setAssistantApiModel(const QString &model)
{
    m_s.setValue("assistant/apiModel", model);
    syncChecked();
}

quint32 Settings::assistantHotKeyVk() const
{
    const quint32 vk = m_s.value("assistant/hotkeyVk", 0x47 /* G */).toUInt();
    return (vk >= 0x01 && vk <= 0xFE) ? vk : 0x47;
}

quint32 Settings::assistantHotKeyModifiers() const
{
    const quint32 mod = m_s.value("assistant/hotkeyMod", 0x0003 /* MOD_CONTROL|MOD_ALT */).toUInt();
    if (!validHotkeyModifiers(mod))
    {
        qWarning("Settings: 非法热键修饰键 assistant/hotkeyMod=0x%x，回退默认", mod);
        return 0x0003;
    }
    return mod;
}

void Settings::setAssistantHotKey(quint32 vk, quint32 modifiers)
{
    m_s.setValue("assistant/hotkeyVk", vk);
    m_s.setValue("assistant/hotkeyMod", modifiers);
    syncChecked();
}

QString Settings::assistantSummaryTime() const
{
    const QString v = m_s.value("assistant/summaryTime", "22:00").toString();
    // 非法手改回退默认（QTime 严格解析 HH:mm）
    return QTime::fromString(v, "HH:mm").isValid() ? v : QStringLiteral("22:00");
}

void Settings::setAssistantSummaryTime(const QString &hhmm)
{
    m_s.setValue("assistant/summaryTime", hhmm);
    syncChecked();
}

bool Settings::assistantAutoSummarize() const
{
    return m_s.value("assistant/autoSummarize", true).toBool();
}

void Settings::setAssistantAutoSummarize(bool on)
{
    m_s.setValue("assistant/autoSummarize", on);
    syncChecked();
}

QString Settings::assistantPermMode() const
{
    const QString v = m_s.value("assistant/permMode", "ask").toString();
    return (v == "smart" || v == "auto") ? v : QStringLiteral("ask");
}

void Settings::setAssistantPermMode(const QString &mode)
{
    m_s.setValue("assistant/permMode", mode);
    syncChecked();
}

QStringList Settings::assistantAlwaysAllowTools() const
{
    return m_s.value("assistant/alwaysAllowTools", QStringList()).toStringList();
}

void Settings::setAssistantAlwaysAllowTools(const QStringList &tools)
{
    m_s.setValue("assistant/alwaysAllowTools", tools);
    syncChecked();
}

QStringList Settings::assistantShellAllowPrefixes() const
{
    // Smart 模式默认白名单：只读性质的常用查询命令（降低确认打扰，
    // 不是安全边界——白名单外命令仍逐条确认）
    return m_s.value("assistant/shellAllowPrefixes",
                     QStringList{"git status", "git log", "git diff", "dir", "ls",
                                 "ipconfig", "ping", "systeminfo"})
        .toStringList();
}

void Settings::setAssistantShellAllowPrefixes(const QStringList &prefixes)
{
    m_s.setValue("assistant/shellAllowPrefixes", prefixes);
    syncChecked();
}

QString Settings::assistantPersonaAppendix() const
{
    return m_s.value("assistant/personaAppendix", "").toString();
}

void Settings::setAssistantPersonaAppendix(const QString &text)
{
    m_s.setValue("assistant/personaAppendix", text);
    syncChecked();
}

QByteArray Settings::assistantGeometry() const
{
    return m_s.value("assistant/geometry").toByteArray();
}

void Settings::setAssistantGeometry(const QByteArray &state)
{
    m_s.setValue("assistant/geometry", state);
    syncChecked();
}

// T-050：桌面悬浮球开关（默认开）与记忆位置（"x,y"；空 = 未记忆，
// 取屏幕右上角默认位；解析/钳制逻辑在 ui/goody/GoodyBallLogic.h）
bool Settings::assistantBallEnabled() const
{
    return m_s.value("assistant/ballEnabled", true).toBool();
}

void Settings::setAssistantBallEnabled(bool on)
{
    m_s.setValue("assistant/ballEnabled", on);
    syncChecked();
}

QString Settings::assistantBallPos() const
{
    // 兼容两种磁盘形态：应用写入的 QString("x,y")（IniFormat 落盘带引号）
    // 与手改 ini 未加引号的 "x,y"——后者会被 IniFormat 按逗号裂成
    // QStringList，直接 toString() 得空串（实测踩坑），此处合回 "x,y"
    const QVariant v = m_s.value("assistant/ballPos", QString());
    if (v.metaType().id() == QMetaType::QStringList) {
        const QStringList parts = v.toStringList();
        return parts.size() == 2 ? parts.join(QLatin1Char(',')) : QString();
    }
    return v.toString();
}

void Settings::setAssistantBallPos(const QString &xy)
{
    m_s.setValue("assistant/ballPos", xy);
    syncChecked();
}

} // namespace poptools
