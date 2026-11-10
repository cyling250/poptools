#include "PermissionManager.h"

#include <QDateTime>

#include "../monitor/ActivityStore.h"

namespace poptools {

PermissionManager::Mode PermissionManager::modeFromSetting(const QString &value)
{
    if (value == QLatin1String("smart"))
        return Mode::Smart;
    if (value == QLatin1String("auto"))
        return Mode::Auto;
    return Mode::Ask;
}

PermissionManager::PermissionManager(ActivityStore *auditStore, QObject *parent)
    : QObject(parent)
    , m_audit(auditStore)
{
}

void PermissionManager::grantAlways(const QString &tool)
{
    if (!m_alwaysAllow.contains(tool))
        m_alwaysAllow << tool;
}

void PermissionManager::revokeAlways(const QString &tool)
{
    m_alwaysAllow.removeAll(tool);
}

bool PermissionManager::shellCommandAllowed(const QJsonObject &args) const
{
    // 只看命令首段（去空白）：前缀匹配白名单即放行。
    // 注意这是"降低打扰"而非安全边界——Smart 模式下白名单外命令仍要确认
    const QString cmd = args.value(QStringLiteral("command")).toString().trimmed();
    if (cmd.isEmpty())
        return false;
    for (const QString &prefix : m_shellPrefixes) {
        const QString p = prefix.trimmed();
        if (!p.isEmpty() && cmd.startsWith(p))
            return true;
    }
    return false;
}

PermissionManager::Decision PermissionManager::decide(const QString &tool,
                                                      const QJsonObject &args) const
{
    if (m_mode == Mode::Auto)
        return Decision::Allow;

    // 只读/安全类工具任何模式都放行（查询、查看、软件内开关）
    // 注意：readOnly 标记由工具注册方声明；注册表不在手时按敏感处理
    if (tool == QLatin1String("fs.list") || tool == QLatin1String("fs.read")
        || tool == QLatin1String("monitor.query") || tool == QLatin1String("time.now")
        || tool == QLatin1String("memory.list") || tool == QLatin1String("memory.search")
        || tool == QLatin1String("task.list") || tool.startsWith(QLatin1String("app.")))
        return Decision::Allow;

    if (m_alwaysAllow.contains(tool))
        return Decision::Allow;

    if (m_mode == Mode::Smart && tool == QLatin1String("shell.run")
        && shellCommandAllowed(args))
        return Decision::Allow;

    return Decision::Confirm; // 敏感类：fs.write/fs.move/fs.delete/shell.run/未知工具
}

void PermissionManager::audit(const QString &tool, const QJsonObject &args,
                              const QString &decision, bool executed)
{
    if (!m_audit)
        return;
    QJsonObject o;
    o.insert("ts", QDateTime::currentMSecsSinceEpoch());
    o.insert("tool", tool);
    o.insert("args", args); // 参数原文入审计（本地文件，敏感度与日志同级）
    o.insert("decision", decision);
    o.insert("executed", executed);
    m_audit->append(QStringLiteral("audit"), o);
}

} // namespace poptools
