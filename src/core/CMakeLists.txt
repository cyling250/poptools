# ============================================================
# poptools_core：核心域静态库（与 UI 解耦）
# 包含：标注模型/渲染、屏幕捕获、OCR、翻译、设置、全局热键
# ============================================================
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Network Svg)

add_library(poptools_core STATIC
    annotation/AnnotationCanvas.cpp
    annotation/AnnotationCanvas.h
    annotation/AnnotationItem.h
    annotation/AnnotationPainter.cpp
    annotation/AnnotationPainter.h
    annotation/AnnotationTypes.h
    capture/ScreenCapturer.cpp
    capture/ScreenCapturer.h
    capture/SmartRectDetector.cpp
    capture/SmartRectDetector.h
    hotkey/GlobalHotKey.cpp
    hotkey/GlobalHotKey.h
    hotkey/GlobalMouseHook.cpp
    hotkey/GlobalMouseHook.h
    ocr/PaddleOcrService.cpp
    ocr/PaddleOcrService.h
    settings/Settings.cpp
    settings/Settings.h
    translation/Translator.cpp
    translation/Translator.h
    translation/SelectionTranslator.cpp
    translation/SelectionTranslator.h
    translation/Languages.h
    translation/ApiTester.cpp
    translation/ApiTester.h
)

target_link_libraries(poptools_core PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::Network
)

# Windows 全局热键、窗口/控件枚举（user32）、DWM 扩展窗口边界（dwmapi）
if(WIN32)
    target_link_libraries(poptools_core PRIVATE user32 dwmapi)
endif()

# 导出头文件目录（core/* 以相对路径包含）
target_include_directories(poptools_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
