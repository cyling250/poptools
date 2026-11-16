#include "PermissionManager.h"

#include <QDateTime>

#include "../monitor/ActivityStore.h"
#include "ToolRegistry.h"

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
    // 多段命令/命令替换一律不放行：分号、管道、逻辑连接、反引号、$()、
    // 换行都能在白名单前缀（如 "git status"）之后拼接任意命令，前缀
    // 匹配只对单条命令有意义（PowerShell 中 ; | && ` $( ) 与换行均
    // 可引出第二段执行体）
    for (const QChar &ch : cmd) {
        if (ch == QLatin1Char(';') || ch == QLatin1Char('|') || ch == QLatin1Char('&')
            || ch == QLatin1Char('`') || ch == QLatin1Char('\n') || ch == QLatin1Char('\r'))
            return false;
    }
    if (cmd.contains(QLatin1String("$(")))
        return false;
    // T-062 修复: 纯 startsWith 前缀匹配无词边界——白名单 "go" 会放行
    // "gotrojan.exe"、"git status" 会放行 "git statusx"，与上方"白名单前缀
    // 只对单条命令有意义"的防线意图相悖。命中后校验下一字符必须为空白或
    // 串尾（大小写口径不变：startsWith 默认区分大小写）
    for (const QString &prefix : m_shellPrefixes) {
        const QString p = prefix.trimmed();
        if (p.isEmpty() || !cmd.startsWith(p))
            continue;
        if (cmd.size() == p.size() || cmd.at(p.size()).isSpace())
            return true; // 词边界：白名单前缀后必须是空白或命令结束
    }
    return false;
}

PermissionManager::Decision PermissionManager::decide(const QString &tool,
                                                      const QJsonObject &args) const
{
    if (m_mode == Mode::Auto)
        return Decision::Allow;

    // 只读/安全类工具任何模式都放行（查询、查看、软件内开关）。
    // readOnly 以注册表声明为准——app.* 前缀不再一刀切放行（写类
    // app.* 如 app.box_create 会真实建目录）；注册表不在手时走保守
    // 硬编码白名单（仅纯查询类）
    if (m_registry) {
        if (m_registry->isReadOnly(tool))
            return Decision::Allow;
    } else if (tool == QLatin1String("fs.list") || tool == QLatin1String("fs.read")
               || tool == QLatin1String("monitor.query") || tool == QLatin1String("time.now")
               || tool == QLatin1String("memory.list") || tool == QLatin1String("memory.search")
               || tool == QLatin1String("task.list")) {
        return Decision::Allow;
    }

    if (m_alwaysAllow.contains(tool))
        return Decision::Allow;

    if (m_mode == Mode::Smart && tool == QLatin1String("shell.run")
        && shellCommandAllowed(args))
        return Decision::Allow;

    return Decision::Confirm; // 敏感类：fs.write/fs.move/fs.delete/shell.run/app.* 写类/未知工具
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
