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

# ---- 共享窗口技术（贴桌 Z 序协调器；日历/收纳盒共用） ----
add_library(poptools_ui_shared STATIC
    shared/DesktopStack.cpp
    shared/DesktopStack.h
)
target_link_libraries(poptools_ui_shared PUBLIC
    poptools_core
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
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
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
)
target_include_directories(poptools_ui_translate PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")

# ---- 桌面日历功能 ----
add_library(poptools_ui_calendar STATIC
    calendar/CalendarWindow.cpp
    calendar/CalendarWindow.h
    calendar/CalendarMiniWidget.cpp
    calendar/CalendarMiniWidget.h
    calendar/MonthView.cpp
    calendar/MonthView.h
    calendar/ItemDelegate.cpp
    calendar/ItemDelegate.h
    calendar/ItemEditDialog.cpp
    calendar/ItemEditDialog.h
)
target_link_libraries(poptools_ui_calendar PUBLIC
    poptools_core
    poptools_ui_shared
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
)
target_include_directories(poptools_ui_calendar PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")

# ---- 桌面收纳盒功能（贴桌分类卡片） ----
add_library(poptools_ui_box STATIC
    box/BoxWindow.cpp
    box/BoxWindow.h
)
target_link_libraries(poptools_ui_box PUBLIC
    poptools_core
    poptools_ui_shared
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
)
target_include_directories(poptools_ui_box PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")

# ---- Goody 管家聊天窗（无边框工具窗；会话/工具在 core/assistant） ----
add_library(poptools_ui_goody STATIC
    goody/GoodyWindow.cpp
    goody/GoodyWindow.h
)
target_link_libraries(poptools_ui_goody PUBLIC
    poptools_core
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
)
target_include_directories(poptools_ui_goody PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")

# ---- 设置界面（跨功能配置 UI，由外壳使用） ----
add_library(poptools_ui_settings STATIC
    settings/SettingsDialog.cpp
    settings/SettingsDialog.h
    settings/CategoryManagerDialog.cpp
    settings/CategoryManagerDialog.h
)
target_link_libraries(poptools_ui_settings PUBLIC
    poptools_core
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
)
target_include_directories(poptools_ui_settings PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")

# ---- 聚合目标：可执行目标只需链接 poptools_ui ----
add_library(poptools_ui INTERFACE)
target_link_libraries(poptools_ui INTERFACE
    poptools_ui_capture
    poptools_ui_pin
    poptools_ui_translate
    poptools_ui_calendar
    poptools_ui_box
    poptools_ui_goody
    poptools_ui_settings
    poptools_ui_shared
)
