#include "Settings.h"

#include <QCoreApplication>
#include <QDir>

namespace snipaste {

// 配置文件存放在可执行文件旁（构建输出/安装目录），随程序打包分发、便于查看
Settings::Settings()
    : m_s(QCoreApplication::applicationDirPath() + "/Snipaste.ini",
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

quint32 Settings::captureHotKeyVk() const
{
    return m_s.value("hotkey/captureVk", 0x70 /* F1 */).toUInt(); // VK_F1 = 0x70
}

void Settings::setCaptureHotKeyVk(quint32 vk)
{
    m_s.setValue("hotkey/captureVk", vk);
    m_s.sync(); // 立即落盘，异常退出也不丢配置
}

QString Settings::apiBaseUrl() const
{
    return m_s.value("translate/apiBaseUrl", "https://api.deepseek.com/v1").toString();
}

void Settings::setApiBaseUrl(const QString &url)
{
    m_s.setValue("translate/apiBaseUrl", url);
    m_s.sync();
}

QString Settings::apiKey() const
{
    // 默认无 API Key，由用户在设置中填写
    return m_s.value("translate/apiKey", "").toString();
}

void Settings::setApiKey(const QString &key)
{
    m_s.setValue("translate/apiKey", key);
    m_s.sync();
}

QString Settings::apiModel() const
{
    return m_s.value("translate/apiModel", "deepseek-v4-flash").toString();
}

void Settings::setApiModel(const QString &model)
{
    m_s.setValue("translate/apiModel", model);
    m_s.sync();
}

// 记忆的目标语言，默认中文（zh）
QString Settings::translateTargetLang() const
{
    return m_s.value("translate/targetLang", "zh").toString();
}

void Settings::setTranslateTargetLang(const QString &code)
{
    m_s.setValue("translate/targetLang", code);
    m_s.sync();
}

QStringList Settings::translateDomains() const
{
    return m_s.value("translate/domains", QStringList()).toStringList();
}

void Settings::setTranslateDomains(const QStringList &domains)
{
    m_s.setValue("translate/domains", domains);
    m_s.sync();
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
    m_s.sync();
}

QString Settings::pinSaveDir() const
{
    // 默认贴图文件夹：软件目录（exe 旁）下的 tempSnipaste
    const QString def = QCoreApplication::applicationDirPath() + "/tempSnipaste";
    return m_s.value("pin/saveDir", def).toString();
}

void Settings::setPinSaveDir(const QString &dir)
{
    m_s.setValue("pin/saveDir", dir);
    m_s.sync();
}

bool Settings::autoStartEnabled() const
{
    return autoStartRegistry().contains("Snipaste");
}

void Settings::setAutoStartEnabled(bool on)
{
    QSettings reg = autoStartRegistry();
    if (on) {
        // Run 值要求带引号的可执行文件路径
        const QString exe =
            "\"" + QDir::toNativeSeparators(QCoreApplication::applicationFilePath()) + "\"";
        reg.setValue("Snipaste", exe);
    } else {
        reg.remove("Snipaste");
    }
    reg.sync();
}

// 划词翻译热键：默认 Ctrl+Alt+Shift+X（VK_X = 0x58, MOD_CONTROL = 0x0002, MOD_ALT = 0x0001, MOD_SHIFT = 0x0004）
quint32 Settings::selectionTranslateHotKeyVk() const
{
    return m_s.value("hotkey/selectionTranslateVk", 0x58 /* X */).toUInt();
}

quint32 Settings::selectionTranslateHotKeyModifiers() const
{
    return m_s.value("hotkey/selectionTranslateMod", 0x0007 /* MOD_CONTROL|MOD_ALT|MOD_SHIFT */).toUInt();
}

void Settings::setSelectionTranslateHotKey(quint32 vk, quint32 modifiers)
{
    m_s.setValue("hotkey/selectionTranslateVk", vk);
    m_s.setValue("hotkey/selectionTranslateMod", modifiers);
    m_s.sync();
}

// 镜像 eSearch「框选.自动框选.图像识别」默认 false：图片边缘识别默认不启用，
// 仅当用户手动开启时，智能框选才叠加图像边缘候选（eSearch 中还需按 S 触发）
bool Settings::smartImageRecognitionEnabled() const
{
    return m_s.value("smart/imageRecognition", false).toBool();
}

void Settings::setSmartImageRecognitionEnabled(bool on)
{
    m_s.setValue("smart/imageRecognition", on);
    m_s.sync();
}

} // namespace snipaste
