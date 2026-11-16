#include "ToolRegistry.h"

namespace poptools {

ToolRegistry::ToolRegistry(QObject *parent)
    : QObject(parent)
{
}

void ToolRegistry::registerTool(const ToolDef &def)
{
    if (def.name.isEmpty() || !def.fn)
        return;
    m_tools.insert(def.name, def);
}

bool ToolRegistry::has(const QString &name) const
{
    return m_tools.contains(name);
}

void ToolRegistry::invoke(const QString &name, const QJsonObject &args,
                          const std::function<void(QJsonObject)> &done) const
{
    const auto it = m_tools.constFind(name);
    if (it == m_tools.cend()) {
        done(QJsonObject{{"ok", false},
                         {"error", QStringLiteral("未知工具：%1").arg(name)}});
        return;
    }
    it->fn(args, done);
}

QJsonArray ToolRegistry::toolSchemas() const
{
    return schemas(false);
}

QString ToolRegistry::toApiName(const QString &internalName)
{
    // DeepSeek 校验 ^[a-zA-Z0-9_-]+$：点分名（fs.read）转下划线（fs_read）。
    // 内部命名空间里无同形歧义（不存在 fs_read 这类内部名），无碰撞风险
    QString out;
    for (const QChar &c : internalName)
        out += (c.isLetterOrNumber() || c == QLatin1Char('_') || c == QLatin1Char('-'))
            ? c : QLatin1Char('_');
    return out;
}

QString ToolRegistry::fromApiName(const QString &apiName) const
{
    // 反查：遍历比对转写名（工具总数 <50，开销可忽略）
    for (auto it = m_tools.cbegin(); it != m_tools.cend(); ++it)
        if (toApiName(it->name) == apiName)
            return it->name;
    return apiName; // 未注册（如模型幻觉的工具名）：原样返回，invoke 走"未知工具"
}

QJsonArray ToolRegistry::toolSchemasForApi() const
{
    return schemas(true);
}

// schema 装配主体（T-062: toolSchemas/toolSchemasForApi 双胞胎去重）——同一
// 循环体仅 name 取值分支（apiName=true 走点分名→API 名转写），其余字段一致；
// 公开方法各自一行委托，返回内容逐字段不变
QJsonArray ToolRegistry::schemas(bool apiName) const
{
    QJsonArray arr;
    for (auto it = m_tools.cbegin(); it != m_tools.cend(); ++it) {
        arr.append(QJsonObject{
            {"type", QStringLiteral("function")},
            {"function", QJsonObject{{"name", apiName ? toApiName(it->name) : it->name},
                                     {"description", it->description},
                                     {"parameters", it->parameters}}},
        });
    }
    return arr;
}

bool ToolRegistry::isReadOnly(const QString &name) const
{
    const auto it = m_tools.constFind(name);
    return it != m_tools.cend() && it->readOnly;
}

} // namespace poptools
