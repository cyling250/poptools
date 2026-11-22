# ============================================================
# poptools_ui：界面层——按功能拆分静态库，聚合为 poptools_ui 接口目标。
#
# 功能边界在链接图中显式表达：
#   * 每个功能一个静态库，互不链接（截图 / 贴图 / 划词翻译 / 设置界面）；
#   * app 只链接聚合目标 poptools_ui；
#   * 拔插功能 = 增删对应目标与聚合行 + 组合根增删一处模块注册。
# 共享依赖（poptools_core）经 PUBLIC 传递，跨层包含路径统一为 src/ 根。
# ============================================================
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Svg)

# ---- Fluent 主题 token 底座（2026-11-19 UI 现代化改版 M0） ----
# 全部界面库 PUBLIC 链接：token/QSS 生成器/窗口 chrome 一处定义
add_library(poptools_ui_theming STATIC
    theming/FluentTheme.cpp
    theming/FluentTheme.h
)
target_link_libraries(poptools_ui_theming PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
)
if(WIN32)
    target_link_libraries(poptools_ui_theming PUBLIC dwmapi)
endif()
target_include_directories(poptools_ui_theming PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")

# ---- 共享窗口技术（贴桌 Z 序协调器 + 桌面表面注入通道 T-059） ----
add_library(poptools_ui_shared STATIC
    shared/DesktopStack.cpp
    shared/DesktopStack.h
    shared/DesktopSharedCanvas.cpp
    shared/DesktopSharedCanvas.h
    shared/DesktopSurfaceHost.cpp
    shared/DesktopSurfaceHost.h
    shared/DesktopAdjustOverlay.cpp
    shared/DesktopAdjustOverlay.h
    shared/DesktopWallpaperProvider.cpp
    shared/DesktopWallpaperProvider.h
)
target_link_libraries(poptools_ui_shared PUBLIC
    poptools_core
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    user32
    gdi32
    msimg32
)
target_include_directories(poptools_ui_shared PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")

# ---- 截图功能：画布窗口 + 控制器 + 工具栏 + 标注工具 ----
add_library(poptools_ui_capture STATIC
    capture/CaptureWindow.cpp
    capture/CaptureWindow.h
    capture/CaptureContext.h
    capture/SelectionController.cpp
    capture/SelectionController.h
    capture/AnnotationEditor.cpp
    capture/AnnotationEditor.h
    capture/ToolDrawingController.cpp
    capture/ToolDrawingController.h
    capture/TextEditingController.cpp
    capture/TextEditingController.h
    capture/OcrController.cpp
    capture/OcrController.h
    capture/OcrPanel.cpp
    capture/OcrPanel.h
    capture/OutputController.cpp
    capture/OutputController.h
    capture/ToolBar.cpp
    capture/ToolBar.h
    capture/tools/ToolBase.h
    capture/tools/ShapeTool.h
    capture/tools/SegmentTool.h
    capture/tools/BrushTool.h
    capture/tools/PencilTool.h
    capture/tools/DoodleTool.h
    capture/tools/MosaicTool.h
    capture/tools/TextTool.h
    capture/tools/EraserTool.h
    capture/tools/ToolFactory.h
)
target_link_libraries(poptools_ui_capture PUBLIC
    poptools_core
    poptools_ui_theming
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Svg
)
target_include_directories(poptools_ui_capture PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")

# ---- 贴图功能 ----
add_library(poptools_ui_pin STATIC
    pin/PinWindow.cpp
    pin/PinWindow.h
)
target_link_libraries(poptools_ui_pin PUBLIC
    poptools_core
    poptools_ui_theming
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
)
target_include_directories(poptools_ui_pin PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")

# ---- 划词翻译功能 ----
add_library(poptools_ui_translate STATIC
    translate/TranslateCard.cpp
    translate/TranslateCard.h
)
target_link_libraries(poptools_ui_translate PUBLIC
    poptools_core
    poptools_ui_theming
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
)
target_include_directories(poptools_ui_translate PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")

# ---- DeepSeek Harness 悬浮球（T-074：对话窗已切 dsh 常驻窗 poptools_ui_dsh，
# 旧聊天窗全家退役；球保留为唯一唤起入口） ----
add_library(poptools_ui_dsh STATIC
    dsh/DshFloatingBall.cpp
    dsh/DshFloatingBall.h
    dsh/DshBallLogic.h
    dsh/DshBallFx.cpp
    dsh/DshBallFx.h
)
target_link_libraries(poptools_ui_dsh PUBLIC
    poptools_core
    poptools_ui_theming
    poptools_ui_shared
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
)
target_include_directories(poptools_ui_dsh PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")

# ---- 桌面日历（照搬 rlqd 复刻；注入通道/画布/覆盖层 T-059 起在 ui/shared） ----
add_library(poptools_ui_calendar STATIC
    calendar/CalendarRenderer.cpp
    calendar/CalendarRenderer.h
    calendar/CalendarPanel.cpp
    calendar/CalendarPanel.h
    calendar/CalendarEventEditor.cpp
    calendar/CalendarEventEditor.h
    calendar/CalendarSearchForm.cpp
    calendar/CalendarSearchForm.h
    calendar/CalendarPaintUtils.h
)
target_link_libraries(poptools_ui_calendar PUBLIC
    poptools_core
    poptools_ui_theming
    poptools_ui_shared
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    user32
    gdi32
)
target_include_directories(poptools_ui_calendar PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")

# ---- 桌面收纳盒（T-060 文件夹门户式多盒，共用桌面表面注入通道） ----
add_library(poptools_ui_box STATIC
    box/BoxRenderer.cpp
    box/BoxRenderer.h
    box/BoxPanel.cpp
    box/BoxPanel.h
    box/BoxAdjustOverlay.cpp
    box/BoxAdjustOverlay.h
    box/BoxManagerDialog.cpp
    box/BoxManagerDialog.h
)
target_link_libraries(poptools_ui_box PUBLIC
    poptools_core
    poptools_ui_theming
    poptools_ui_shared
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    user32
    gdi32
)
target_include_directories(poptools_ui_box PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")

# ---- dsh 常驻窗（T-074：dsh 原样移植——WebView2 宿主 + sidecar 运行时） ----
# （T-078 M1）原 poptools_ui_dsh（DshWindow/WebView2Host/DshRuntime/
# DshUpdateService 嵌入壳）随主界面切换 dsh 官方 Electron 桌面端退役；
# goody 悬浮球库改名 poptools_ui_dsh 承接（见上方 Goody→dsh 重命名）。

# ---- 设置界面（跨功能配置 UI，由外壳使用） ----
# 2026-11-19 Fluent 改版 M1：SettingCard/ToggleSwitch 为 Win11 设置卡片组件
add_library(poptools_ui_settings STATIC
    settings/SettingsDialog.cpp
    settings/SettingsDialog.h
    settings/SettingCard.cpp
    settings/SettingCard.h
    settings/ToggleSwitch.cpp
    settings/ToggleSwitch.h
)
target_link_libraries(poptools_ui_settings PUBLIC
    poptools_app_infra
    poptools_core
    poptools_ui_theming
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
)
target_include_directories(poptools_ui_settings PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")

# ---- 聚合目标：可执行目标只需链接 poptools_ui ----
add_library(poptools_ui INTERFACE)
target_link_libraries(poptools_ui INTERFACE
    poptools_ui_theming
    poptools_ui_capture
    poptools_ui_pin
    poptools_ui_translate
    poptools_ui_calendar
    poptools_ui_box
    poptools_ui_dsh
    poptools_ui_dsh
    poptools_ui_settings
    poptools_ui_shared
)
