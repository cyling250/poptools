cmake_minimum_required(VERSION 3.21)
project(Snipaste VERSION 0.1.3 LANGUAGES CXX)

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

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network Svg)

qt_standard_project_setup()

# ---- 分层：core（核心域） + ui（界面层） + app（可执行） ----
add_subdirectory(src/core)
add_subdirectory(src/ui)

# ---- 单元测试（可选，默认开启；ctest 运行） ----
option(SNIPASTE_BUILD_TESTS "Build unit tests" ON)
if(SNIPASTE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

qt_add_executable(Snipaste WIN32
    src/main.cpp
    src/app/MainWindow.cpp
    src/app/MainWindow.h
    src/app/AppServices.cpp
    src/app/AppServices.h
    src/app/FeatureRegistry.h
    src/app/IFeatureModule.h
    src/app/modules/CaptureModule.cpp
    src/app/modules/CaptureModule.h
    src/app/modules/PinModule.cpp
    src/app/modules/PinModule.h
    src/app/modules/SelectionTranslateModule.cpp
    src/app/modules/SelectionTranslateModule.h
    src/resources/resources.qrc
)

target_link_libraries(Snipaste PRIVATE
    snipaste_core
    snipaste_ui
)

# Qt 运行库：构建（make）完成后由本地 Qt 套件自带的 windeployqt 自动部署到
# 可执行文件旁（Release 输出目录），使 exe 不依赖全局 Qt 安装；同时供安装程序打包
set(QT_BIN_DIR "${CMAKE_PREFIX_PATH}/bin")
if(NOT EXISTS "${QT_BIN_DIR}/windeployqt.exe")
    message(FATAL_ERROR "windeployqt.exe not found at ${QT_BIN_DIR}. "
        "请先运行 script/install_qt.py 下载 Qt 到 tools/qt（或设置 QT_ROOT 指向 Qt 套件目录）")
endif()
add_custom_command(TARGET Snipaste POST_BUILD
    # -no-translations：本机 Qt 套件不含 translations/ 目录（未下载），且应用不加载
    # QTranslator/.qm，跳过翻译部署以消除 windeployqt 的 "(developer build?)" 警告
    COMMAND "${QT_BIN_DIR}/windeployqt.exe" --release -no-translations "$<TARGET_FILE:Snipaste>"
    COMMENT "Deploying Qt runtime via windeployqt"
)

# 部署 PaddleOCR-json（离线 OCR 引擎，QProcess 子进程调用）
# 整个 paddleocr 目录复制到可执行文件旁，包含 exe、模型、配置文件与
# 引擎自带的 opencv_world4100.dll（子进程以 paddleocr/ 为工作目录启动，
# Windows 加载器按 exe 所在目录 → CWD 找 DLL）
set(PADDLEOCR_DIR "${CMAKE_SOURCE_DIR}/tools/paddleocr")
if(EXISTS "${PADDLEOCR_DIR}/PaddleOCR-json.exe")
    add_custom_command(TARGET Snipaste POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${PADDLEOCR_DIR}"
            "$<TARGET_FILE_DIR:Snipaste>/paddleocr"
        COMMENT "Deploying PaddleOCR-json engine"
    )
endif()

# 安装规则（可选）
install(TARGETS Snipaste RUNTIME DESTINATION .)
