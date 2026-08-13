cmake_minimum_required(VERSION 3.21)
project(MySnipaste VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Qt SDK 位置（LGPL 开源版）——不依赖全局 Qt 安装：
# 默认使用项目本地 tools/qt/<QT_VERSION>/msvc2022_64（由 script/install_qt.py 下载）
# 优先级：环境变量 QT_ROOT > CMake 参数 -DQT_ROOT=... > 项目本地 tools/qt
set(QT_VERSION "6.8.3")
if(DEFINED ENV{QT_ROOT} AND NOT "$ENV{QT_ROOT}" STREQUAL "")
    set(CMAKE_PREFIX_PATH "$ENV{QT_ROOT}")
elseif(DEFINED QT_ROOT)
    set(CMAKE_PREFIX_PATH "${QT_ROOT}")
else()
    set(CMAKE_PREFIX_PATH "${CMAKE_SOURCE_DIR}/tools/qt/${QT_VERSION}/msvc2022_64")
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network Svg Concurrent)

qt_standard_project_setup()

# ---- 分层：core（核心域） + ui（界面层） + app（可执行） ----
add_subdirectory(src/core)
add_subdirectory(src/ui)

qt_add_executable(MySnipaste WIN32
    src/main.cpp
    src/app/MainWindow.cpp
    src/app/MainWindow.h
    src/resources/resources.qrc
)

target_link_libraries(MySnipaste PRIVATE
    snipaste_core
    snipaste_ui
)

# 内置 OCR 引擎：构建后把 tools/tesseract 复制到可执行文件旁（开发/安装同布局）。
# 运行时 OcrService 进程内 LoadLibrary 加载 {exeDir}/tesseract/libtesseract-5.dll
# （依赖 DLL 与 tessdata 语言包同目录分发，不依赖外部 tesseract.exe）；
# 未下载 OCR 引擎时跳过，不阻断构建
if(EXISTS "${CMAKE_SOURCE_DIR}/tools/tesseract")
    add_custom_command(TARGET MySnipaste POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/tools/tesseract"
            "$<TARGET_FILE_DIR:MySnipaste>/tesseract"
        COMMENT "Copying bundled tesseract engine"
    )
endif()

# Qt 运行库：构建（make）完成后由本地 Qt 套件自带的 windeployqt 自动部署到
# 可执行文件旁（Release 输出目录），使 exe 不依赖全局 Qt 安装；同时供安装程序打包
set(QT_BIN_DIR "${CMAKE_PREFIX_PATH}/bin")
if(NOT EXISTS "${QT_BIN_DIR}/windeployqt.exe")
    message(FATAL_ERROR "windeployqt.exe not found at ${QT_BIN_DIR}. "
        "请先运行 script/install_qt.py 下载 Qt 到 tools/qt（或设置 QT_ROOT 指向 Qt 套件目录）")
endif()
add_custom_command(TARGET MySnipaste POST_BUILD
    COMMAND "${QT_BIN_DIR}/windeployqt.exe" --release "$<TARGET_FILE:MySnipaste>"
    COMMENT "Deploying Qt runtime via windeployqt"
)

# 安装规则（可选）
install(TARGETS MySnipaste RUNTIME DESTINATION .)
