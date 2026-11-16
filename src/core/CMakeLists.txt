# ============================================================
# poptools_core：核心域静态库（与 UI 解耦）
# 包含：标注模型/渲染、屏幕捕获、OCR、翻译、设置、全局热键
# ============================================================
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Network Svg)

add_library(poptools_core STATIC
    assistant/ChatSession.cpp
    assistant/ChatSession.h
    assistant/DailyAggregator.cpp
    assistant/DailyAggregator.h
    assistant/DailyRecordJob.cpp
    assistant/DailyRecordJob.h
    assistant/FsTool.cpp
    assistant/FsTool.h
    assistant/LlmClient.cpp
    assistant/LlmClient.h
    assistant/McpStdioClient.cpp
    assistant/McpStdioClient.h
    assistant/MemoryStore.cpp
    assistant/MemoryStore.h
    assistant/MonitorQueryTool.cpp
    assistant/MonitorQueryTool.h
    assistant/PermissionManager.cpp
    assistant/PermissionManager.h
    assistant/Scheduler.cpp
    assistant/Scheduler.h
    assistant/ShellTool.cpp
    assistant/ShellTool.h
    assistant/ToolRegistry.cpp
    assistant/ToolRegistry.h
    assistant/ToolText.cpp
    assistant/ToolText.h
    assistant/WorkingMemory.cpp
    assistant/WorkingMemory.h
    annotation/AnnotationCanvas.cpp
    annotation/AnnotationCanvas.h
    annotation/AnnotationItem.h
    annotation/AnnotationPainter.cpp
    annotation/AnnotationPainter.h
    annotation/AnnotationTypes.h
    calendar/ChineseCalendar.cpp
    calendar/ChineseCalendar.h
    calendar/CalendarLayout.cpp
    calendar/CalendarLayout.h
    calendar/CalendarSettings.cpp
    calendar/CalendarSettings.h
    calendar/CalendarTaskStore.cpp
    calendar/CalendarTaskStore.h
    box/BoxCatalog.cpp
    box/BoxCatalog.h
    box/BoxLayout.cpp
    box/BoxLayout.h
    box/IconService.cpp
    box/IconService.h
    capture/ScreenCapturer.cpp
    capture/ScreenCapturer.h
    capture/SmartRectDetector.cpp
    capture/SmartRectDetector.h
    capture/QrScanner.cpp
    capture/QrScanner.h
    hotkey/GlobalHotKey.cpp
    hotkey/GlobalHotKey.h
    hotkey/HotKeyNames.h
    hotkey/GlobalMouseHook.cpp
    hotkey/GlobalMouseHook.h
    monitor/ActivityStore.cpp
    monitor/ActivityStore.h
    monitor/ClipboardCodec.cpp
    monitor/ClipboardCodec.h
    monitor/ClipboardListener.cpp
    monitor/ClipboardListener.h
    monitor/ForegroundTracker.cpp
    monitor/ForegroundTracker.h
    monitor/GlobalKeyboardHook.cpp
    monitor/GlobalKeyboardHook.h
    monitor/ImeCommitDetector.cpp
    monitor/ImeCommitDetector.h
    monitor/InputActivityMonitor.cpp
    monitor/InputActivityMonitor.h
    monitor/KeyboardTextComposer.cpp
    monitor/KeyboardTextComposer.h
    monitor/OwnWriteGuard.cpp
    monitor/OwnWriteGuard.h
    monitor/PasswordFieldGuard.cpp
    monitor/PasswordFieldGuard.h
    monitor/SnapshotOcrPipeline.cpp
    monitor/SnapshotOcrPipeline.h
    monitor/UiaTextReader.cpp
    monitor/UiaTextReader.h
    ocr/RapidOcrService.cpp
    ocr/RapidOcrService.h
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

# Windows 全局热键、窗口/控件枚举（user32）、DWM 扩展窗口边界（dwmapi）、
# 回收站删除（shell32：SHFileOperation，FsTool）、
# UIA 取文（ole32：CoInitializeEx/CoCreateInstance；oleaut32：BSTR）、
# 收纳盒图标提取（shell32：SHGetFileInfo/SHGetImageList；gdi32：GetIconInfo/GetDIBits）
if(WIN32)
    target_link_libraries(poptools_core PRIVATE user32 dwmapi shell32 gdi32 ole32 oleaut32)
endif()

# ---- 二维码识别（可选）：zxing-cpp 静态编入（源码由 scripts/install_zxing.py
#      下载到 tools/zxing，不入库）。缺失时 QrScanner 编译为"不可用"，不阻断构建。
#      注意开关名是 BUILD_*（ZXING_* 无效）：BUILD_EXAMPLES 默认 ON，会经
#      FetchContent 联网拉取 stb —— 必须关掉，保持离线可构建 ----
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/zxing/CMakeLists.txt")
    set(BUILD_SHARED_LIBS OFF)
    set(BUILD_WRITERS OFF)          # 只需解码
    set(BUILD_EXAMPLES OFF)
    set(BUILD_BLACKBOX_TESTS OFF)
    set(BUILD_UNIT_TESTS OFF)
    set(BUILD_PYTHON_MODULE OFF)
    set(BUILD_C_API OFF)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../tools/zxing"
                     "${CMAKE_BINARY_DIR}/tools/zxing-build" EXCLUDE_FROM_ALL)
    target_link_libraries(poptools_core PUBLIC ZXing::ZXing)
    target_compile_definitions(poptools_core PUBLIC POPTOOLS_HAVE_ZXING)
    message(STATUS "QR: zxing-cpp enabled (tools/zxing)")
else()
    message(STATUS "QR: tools/zxing 不存在，二维码识别编译为不可用（scripts/install_zxing.py 可安装）")
endif()

# 导出头文件目录（core/* 以相对路径包含）
target_include_directories(poptools_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
