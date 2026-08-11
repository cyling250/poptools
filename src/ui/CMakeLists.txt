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
