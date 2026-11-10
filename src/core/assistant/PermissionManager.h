#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>

namespace poptools {

class ActivityStore;
class ToolRegistry;

// ============================================================
// goody 权限管理器（对齐 ZCode 的三模式，用户在设置中自选）：
//   * Ask   逐条确认：只读/安全类放行，敏感类（写文件/执行命令/删除）
//           弹确认卡片；"总是允许此类"按工具名记忆；
//   * Smart 智能自动：安全类放行；shell.run 命中安全命令前缀白名单
//           自动执行，其余确认；
//   * Auto  完全自动：一律放行（审计照记，风险自负）。
//
// 全部工具调用（含被拒的）写审计日志 goody/audit.jsonl（复用
// ActivityStore 的 worker 写盘）。
// ============================================================
class PermissionManager : public QObject
{
    Q_OBJECT
public:
    enum class Mode { Ask, Smart, Auto };
    enum class Decision { Allow, Confirm };

    // 设置存储值（"ask"/"smart"/"auto"）→ 枚举，未知回退 Ask
    static Mode modeFromSetting(const QString &value);

    // auditStore 为组合根/模块注入的 ActivityStore（baseDir=goody），
    // 为空时不落审计（单测）
    explicit PermissionManager(ActivityStore *auditStore, QObject *parent = nullptr);

    // 注入工具注册表：readOnly 判定以注册表声明为准；不注入时走保守
    // 硬编码白名单（单测/极端场景）
    void setToolRegistry(const ToolRegistry *registry) { m_registry = registry; }

    void setMode(Mode mode) { m_mode = mode; }
    Mode mode() const { return m_mode; }

    // "总是允许此类"名单（工具名级，持久化于 assistant/alwaysAllowTools）
    void setAlwaysAllowTools(const QStringList &tools) { m_alwaysAllow = tools; }
    QStringList alwaysAllowTools() const { return m_alwaysAllow; }
    void grantAlways(const QString &tool);
    void revokeAlways(const QString &tool);

    // Smart 模式的 shell 安全命令前缀（如 "git status"；设置 shellAllowPrefixes）
    void setShellAllowPrefixes(const QStringList &prefixes) { m_shellPrefixes = prefixes; }

    // 单次调用决策（不落审计；执行与否由调用方在审计中记录）
    Decision decide(const QString &tool, const QJsonObject &args) const;

public slots:
    // 审计一行：工具、参数摘要、决策与是否实际执行
    void audit(const QString &tool, const QJsonObject &args, const QString &decision,
               bool executed);

private:
    bool shellCommandAllowed(const QJsonObject &args) const;

    Mode m_mode = Mode::Ask;
    QStringList m_alwaysAllow;
    QStringList m_shellPrefixes;
    const ToolRegistry *m_registry = nullptr;
    ActivityStore *m_audit = nullptr;
};

} // namespace poptools
