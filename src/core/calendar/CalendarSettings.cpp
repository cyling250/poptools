#include "CalendarSettings.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonParseError>

namespace poptools {

namespace {
QString g_dirOverride; // 单测重定向（非线程安全；数据路径仅主线程访问）
}

void CalendarSettings::setDataDirOverride(const QString &dir)
{
    g_dirOverride = dir;
}

QString CalendarSettings::dataDir()
{
    if (!g_dirOverride.isEmpty()) {
        QDir dir(g_dirOverride);
        if (!dir.exists())
            QDir().mkpath(dir.absolutePath());
        return dir.absolutePath();
    }
    QString base = qEnvironmentVariable("LOCALAPPDATA");
    if (base.isEmpty())
        base = QDir::homePath();
    QDir dir(base + QStringLiteral("/PopTools/calendar"));
    if (!dir.exists())
        QDir().mkpath(dir.absolutePath());
    return dir.absolutePath();
}

QString CalendarSettings::settingsPath() { return dataDir() + QStringLiteral("/settings.json"); }

QString CalendarSettings::tasksPath() { return dataDir() + QStringLiteral("/tasks.json"); }

QJsonObject CalendarSettings::toJson() const
{
    return QJsonObject{
        { "X", x },           { "Y", y },                 { "Width", width },
        { "Height", height }, { "Hidden", hidden },       { "ShowLunar", showLunar },
        { "ShowWeekNumbers", showWeekNumbers },           { "ShowEvents", showEvents },
        { "GlassEffect", glassEffect },                   { "FontScale", fontScale },
        { "ShowGuide", showGuide },                       { "ShowNextMonthPill", showNextMonthPill }
    };
}

void CalendarSettings::fromJson(const QJsonObject &o)
{
    x = o.value("X").toInt(640);
    y = o.value("Y").toInt(20);
    width = o.value("Width").toInt(1260);
    height = o.value("Height").toInt(795);
    hidden = o.value("Hidden").toBool(false);
    showLunar = o.value("ShowLunar").toBool(true);
    showWeekNumbers = o.value("ShowWeekNumbers").toBool(true);
    showEvents = o.value("ShowEvents").toBool(true);
    glassEffect = o.value("GlassEffect").toBool(true);
    fontScale = o.value("FontScale").toInt(100);
    showGuide = o.value("ShowGuide").toBool(true);
    showNextMonthPill = o.value("ShowNextMonthPill").toBool(true);
}

CalendarSettings CalendarSettings::load()
{
    CalendarSettings s;
    QFile f(settingsPath());
    if (!f.open(QIODevice::ReadOnly))
        return s;
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject())
        return s;
    s.fromJson(doc.object());
    return s;
}

void CalendarSettings::save() const
{
    QJsonDocument doc(toJson());
    QFile f(settingsPath());
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(doc.toJson(QJsonDocument::Indented));
}

} // namespace poptools
