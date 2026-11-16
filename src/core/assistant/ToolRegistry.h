#pragma once

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <functional>

namespace poptools {

// ============================================================
// goody 工具注册表：内置工具 + 各功能模块贡献工具 + MCP 工具的统一入口。
//
// 工具执行是【异步回调】风格（不给主线程引入任何阻塞——shell 跑
// QProcess 事件循环、查询类挪线程池），fn 完成后以结果 JSON 回调：
//   {"ok": true|false, ...结果字段}  或  {"ok": false, "error": "…"}
//
// 工具 schema 走 OpenAI function-calling 格式（DeepSeek 兼容）。
// AppServices.toolRegistry 槽位由组合根创建并注入，其他功能模块在
// init 中经 registerTool 贡献"掌控本软件"的能力。
// ============================================================
struct ToolDef
{
    QString name;        // 全局唯一（MCP 工具带 mcp__<server>__ 前缀）
    QString description; // 给模型看的说明（中文，含使用边界）
    QJsonObject parameters; // JSON Schema（type/properties/required）
    bool readOnly = false;  // 只读/安全类（权限管理器据此放行）
    // 执行：args → 异步回调结果（回调可能同步发生（纯内存工具）也可能延后）
    std::function<void(const QJsonObject &args,
                       const std::function<void(QJsonObject)> &done)>
        fn;
};

// 无参数工具的空 JSON Schema（object + 空 properties）：各模块 ToolDef 装配
// 样板去重用（T-062），生成内容与原先各处内联写法逐字段相同
inline QJsonObject emptyObjectSchema()
{
    return QJsonObject{{"type", QStringLiteral("object")},
                       {"properties", QJsonObject{}}};
}

class ToolRegistry : public QObject
{
    Q_OBJECT
public:
    explicit ToolRegistry(QObject *parent = nullptr);

    // 注册/覆盖同名工具
    void registerTool(const ToolDef &def);

    bool has(const QString &name) const;
    // 异步执行：name 不存在时立即回调错误结果
    void invoke(const QString &name, const QJsonObject &args,
                const std::function<void(QJsonObject)> &done) const;

    // 全部工具的 OpenAI schema 数组（发给模型）
    QJsonArray toolSchemas() const;

    // DeepSeek/OpenAI 限制工具名只能 [a-zA-Z0-9_-]，内部点分名（fs.read 等）
    // 在 API 边界转写；schema 数组用转写名，调用回包时用 fromApiName 还原
    QJsonArray toolSchemasForApi() const;
    QString fromApiName(const QString &apiName) const;
    static QString toApiName(const QString &internalName);

    // 只读性查询（权限管理器用）
    bool isReadOnly(const QString &name) const;

private:
    // schema 装配主体（apiName=true 时工具名走 toApiName 转写）；
    // toolSchemas / toolSchemasForApi 的共用实现
    QJsonArray schemas(bool apiName) const;

    QHash<QString, ToolDef> m_tools;
};

} // namespace poptools
