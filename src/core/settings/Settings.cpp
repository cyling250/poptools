#include "Settings.h"

#include <QCoreApplication>
#include <QDir>

namespace snipaste {

// 配置文件存放在可执行文件旁（构建输出/安装目录），随程序打包分发、便于查看
Settings::Settings()
    : m_s(QCoreApplication::applicationDirPath() + "/MySnipaste.ini",
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
    return autoStartRegistry().contains("MySnipaste");
}

void Settings::setAutoStartEnabled(bool on)
{
    QSettings reg = autoStartRegistry();
    if (on) {
        // Run 值要求带引号的可执行文件路径
        const QString exe =
            "\"" + QDir::toNativeSeparators(QCoreApplication::applicationFilePath()) + "\"";
        reg.setValue("MySnipaste", exe);
    } else {
        reg.remove("MySnipaste");
    }
    reg.sync();
}

} // namespace snipaste
