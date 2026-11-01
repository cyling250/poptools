# ============================================================
# poptools_app：应用层——组合根基础设施 + 功能模块目标。
#
# 目标划分（兑现 README「拔插功能 = 组合根删一处 add() + CMake 删一个目标」）：
#   * poptools_app_infra      组合根基础设施（AppServices / FeatureRegistry /
#                             IFeatureModule），供可执行与单元测试共用（测试
#                             目标不再手抄源文件）；
#   * poptools_module_<id>    一个功能模块一个静态库（模块源 + 其界面库），
#                             拔插功能 = 可执行链接行增删一个模块目标；
#   * poptools_modules        聚合接口目标，可执行只链接它。
# ============================================================
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

# ---- 组合根基础设施 ----
add_library(poptools_app_infra STATIC
    AppServices.cpp
    AppServices.h
    FeatureRegistry.h
    IFeatureModule.h
)
target_link_libraries(poptools_app_infra PUBLIC
    poptools_core
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
)
target_include_directories(poptools_app_infra PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")

# ---- 功能模块：截图（模块源 + 其界面库） ----
add_library(poptools_module_capture STATIC
    modules/CaptureModule.cpp
    modules/CaptureModule.h
)
target_link_libraries(poptools_module_capture PUBLIC
    poptools_app_infra
    poptools_ui_capture
)

# ---- 功能模块：贴图 ----
add_library(poptools_module_pin STATIC
    modules/PinModule.cpp
    modules/PinModule.h
)
target_link_libraries(poptools_module_pin PUBLIC
    poptools_app_infra
    poptools_ui_pin
)

# ---- 功能模块：划词翻译 ----
add_library(poptools_module_selectiontranslate STATIC
    modules/SelectionTranslateModule.cpp
    modules/SelectionTranslateModule.h
)
target_link_libraries(poptools_module_selectiontranslate PUBLIC
    poptools_app_infra
    poptools_ui_translate
)

# ---- 聚合目标：可执行只链接 poptools_modules ----
add_library(poptools_modules INTERFACE)
target_link_libraries(poptools_modules INTERFACE
    poptools_module_capture
    poptools_module_pin
    poptools_module_selectiontranslate
)
