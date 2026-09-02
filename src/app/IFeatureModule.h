#pragma once

#include <QString>

namespace snipaste {

class AppServices;

// ============================================================
// 功能模块接口：一个自包含的功能单元（截图 / 贴图 / 划词翻译…）。
//
// 模块在 init 中创建自己的窗口与服务、注册热键、贡献托盘菜单、
// 发布/消费 AppServices 上的能力槽与共享态；模块之间互不 include，
// 跨模块交互只经 AppServices（能力槽/共享态）或组合根的信号撮合。
//
// 拔插一个功能 = 增删组合根里的一处模块注册 + 对应 CMake 源文件。
// ============================================================
class IFeatureModule
{
public:
    virtual ~IFeatureModule() = default;

    // 模块标识（日志/查询用，全局唯一）
    virtual QString id() const = 0;

    // 装配：创建窗口/服务、注册热键、贡献托盘菜单、发布能力槽。
    // 由 FeatureRegistry 在应用启动时按注册顺序调用一次。
    virtual void init(AppServices &services) = 0;

    // 反装配系统级资源（全局钩子、应用级事件过滤器等）。
    // 窗口析构与热键注销随应用退出，无需在此处理。
    virtual void shutdown() { }
};

} // namespace snipaste
