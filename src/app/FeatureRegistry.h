#pragma once

#include <memory>
#include <vector>

#include "IFeatureModule.h"

namespace poptools {

class AppServices;

// ============================================================
// 功能模块注册表：组合根集中注册全部功能模块并统一装配。
// 模块生命周期 = 应用生命周期（静态注册，非动态插件）；
// 拔插功能 = 组合根增删一处 add() + CMake 增删对应源文件。
// ============================================================
class FeatureRegistry
{
public:
    // 注册顺序即 initAll 装配顺序，亦即托盘菜单中功能项的排列顺序
    void add(std::unique_ptr<IFeatureModule> module)
    {
        m_modules.push_back(std::move(module));
    }

    // 按注册顺序装配全部模块
    void initAll(AppServices &services)
    {
        for (auto &m : m_modules)
            m->init(services);
    }

    // 反装配全部模块（应用退出前的系统级资源清理）
    void shutdownAll()
    {
        for (auto &m : m_modules)
            m->shutdown();
    }

    IFeatureModule *module(const QString &id) const
    {
        for (auto &m : m_modules)
            if (m->id() == id)
                return m.get();
        return nullptr;
    }

private:
    std::vector<std::unique_ptr<IFeatureModule>> m_modules;
};

} // namespace poptools
