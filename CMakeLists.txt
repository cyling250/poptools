cmake_minimum_required(VERSION 3.21)
project(MySnipaste VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Qt SDK 位置（LGPL 开源版）
# 优先级：环境变量 QT_ROOT > CMake 参数 -DQT_ROOT=... > 默认路径
if(DEFINED ENV{QT_ROOT} AND NOT "$ENV{QT_ROOT}" STREQUAL "")
    set(CMAKE_PREFIX_PATH "$ENV{QT_ROOT}")
elseif(DEFINED QT_ROOT)
    set(CMAKE_PREFIX_PATH "${QT_ROOT}")
else()
    set(CMAKE_PREFIX_PATH "D:/Qt/6.8.3/msvc2022_64")
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network Svg)

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

# 内置 OCR 引擎：构建后把 tools/tesseract 复制到可执行文件旁（开发/安装同布局）
# 运行时 OcrService 优先从 {exeDir}/tesseract/tesseract.exe 加载
add_custom_command(TARGET MySnipaste POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/tools/tesseract"
        "$<TARGET_FILE_DIR:MySnipaste>/tesseract"
    COMMENT "Copying bundled tesseract engine"
)

# 安装规则（可选）
install(TARGETS MySnipaste RUNTIME DESTINATION .)
