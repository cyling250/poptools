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

# ---- OpenCV SDK 位置（本地下载，仿 Qt 模式）----
# 优先级：环境变量/dp OPENCV_ROOT > 项目本地 tools/opencv/opencv
# 由 script/install_opencv.py 下载官方 Windows 预编译包，供 eSearch 窗口识别算法移植使用
if(DEFINED ENV{OPENCV_ROOT} AND NOT "$ENV{OPENCV_ROOT}" STREQUAL "")
    set(OPENCV_ROOT "$ENV{OPENCV_ROOT}")
elseif(DEFINED OPENCV_ROOT)
    # OPENCV_ROOT 透传
else()
    set(OPENCV_ROOT "${CMAKE_SOURCE_DIR}/tools/opencv/opencv")
endif()
if(NOT EXISTS "${OPENCV_ROOT}/build/OpenCVConfig.cmake")
    message(FATAL_ERROR "未找到 OpenCV：${OPENCV_ROOT}/build/OpenCVConfig.cmake 不存在。"
        "请先运行 script/install_opencv.py 下载 OpenCV 到 tools/opencv（或设置 OPENCV_ROOT 指向含 build/ 的 OpenCV SDK 目录）")
endif()
set(OpenCV_DIR "${OPENCV_ROOT}/build")
# 官方 Windows 包仅提供 x64/vc16 运行时库；新编译器如 MSVC 19.51（VS18）不被
# OpenCVConfig.cmake 识别，故显式指定 ARCH/RUNTIME 让 find_package 正确找到 vc16 二进制
set(OpenCV_ARCH "x64")
set(OpenCV_RUNTIME "vc16")
find_package(OpenCV REQUIRED)

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

# 部署 OpenCV 运行库（opencv_world 动态库）到可执行文件旁，供窗口识别算法使用
# DLL 名按 OpenCV 版本生成：opencv_world${MAJOR}${MINOR}${PATCH}0.dll
set(OPENCV_DLL_NAME "opencv_world${OpenCV_VERSION_MAJOR}${OpenCV_VERSION_MINOR}${OpenCV_VERSION_PATCH}0.dll")
if(EXISTS "${OPENCV_ROOT}/build/x64/vc16/bin/${OPENCV_DLL_NAME}")
    add_custom_command(TARGET MySnipaste POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${OPENCV_ROOT}/build/x64/vc16/bin/${OPENCV_DLL_NAME}"
            "$<TARGET_FILE_DIR:MySnipaste>/${OPENCV_DLL_NAME}"
        COMMENT "Deploying OpenCV runtime (${OPENCV_DLL_NAME})"
    )
endif()

# 安装规则（可选）
install(TARGETS MySnipaste RUNTIME DESTINATION .)
