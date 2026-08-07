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

# 安装规则（可选）
install(TARGETS MySnipaste RUNTIME DESTINATION .)

# 拷贝 Tesseract 中文语言包到构建输出目录（exe 旁 tessdata\）
add_custom_command(TARGET MySnipaste POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/src/resources/tessdata
        $<TARGET_FILE_DIR:MySnipaste>/tessdata
)
