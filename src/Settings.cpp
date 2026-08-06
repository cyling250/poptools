#include "Settings.h"

Settings::Settings()
    : m_s(QSettings::IniFormat,
          QSettings::UserScope,
          "MySnipaste", "MySnipaste")
{
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
}

QString Settings::tesseractPath() const
{
    return m_s.value("ocr/tesseractPath",
                     "C:/Program Files/Tesseract-OCR/tesseract.exe").toString();
}

void Settings::setTesseractPath(const QString &path)
{
    m_s.setValue("ocr/tesseractPath", path);
}

QString Settings::translateMode() const
{
    return m_s.value("translate/mode", "api").toString(); // api | local
}

void Settings::setTranslateMode(const QString &mode)
{
    m_s.setValue("translate/mode", mode);
}

QString Settings::apiBaseUrl() const
{
    return m_s.value("translate/apiBaseUrl", "https://api.deepseek.com/v1").toString();
}

void Settings::setApiBaseUrl(const QString &url)
{
    m_s.setValue("translate/apiBaseUrl", url);
}

QString Settings::apiKey() const
{
    return m_s.value("translate/apiKey", "").toString();
}

void Settings::setApiKey(const QString &key)
{
    m_s.setValue("translate/apiKey", key);
}

QString Settings::apiModel() const
{
    return m_s.value("translate/apiModel", "deepseek-chat").toString();
}

void Settings::setApiModel(const QString &model)
{
    m_s.setValue("translate/apiModel", model);
}

QString Settings::targetLang() const
{
    return m_s.value("translate/targetLang", "zh").toString();
}

void Settings::setTargetLang(const QString &lang)
{
    m_s.setValue("translate/targetLang", lang);
}
