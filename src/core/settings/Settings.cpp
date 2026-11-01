#include "Settings.h"

#include <QCoreApplication>
#include <QDir>

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
    return m_s.value("hotkey/selectionTranslateMod", 0x0007 /* MOD_CONTROL|MOD_ALT|MOD_SHIFT */).toUInt();
}

void Settings::setSelectionTranslateHotKey(quint32 vk, quint32 modifiers)
{
    m_s.setValue("hotkey/selectionTranslateVk", vk);
    m_s.setValue("hotkey/selectionTranslateMod", modifiers);
    syncChecked();
}

} // namespace poptools
