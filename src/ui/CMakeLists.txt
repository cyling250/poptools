# ============================================================
# snipaste_ui：界面层静态库（依赖 snipaste_core）
# 包含：截图窗口/工具栏、结果面板、设置对话框、贴图窗口
# ============================================================
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

add_library(snipaste_ui STATIC
    capture/CaptureWindow.cpp
    capture/CaptureWindow.h
    capture/ToolBar.cpp
    capture/ToolBar.h
    pin/PinWindow.cpp
    pin/PinWindow.h
    result/ResultDialog.cpp
    result/ResultDialog.h
    settings/SettingsDialog.cpp
    settings/SettingsDialog.h
)

target_link_libraries(snipaste_ui PUBLIC
    snipaste_core
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Svg
)

target_include_directories(snipaste_ui PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
