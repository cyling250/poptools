cmake_minimum_required(VERSION 3.21)
# CMake 3.30+ 策略 CMP0144：find_package 使用与包名大小写一致的 <PackageName>_ROOT
# （如 OpenCV_ROOT）而非全大写 <PACKAGENAME>_ROOT，显式启用新行为以消除兼容性警告
cmake_policy(SET CMP0144 NEW)
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

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network Svg Concurrent)

qt_standard_project_setup()

# ---- OpenCV SDK 位置（本地下载，仿 Qt 模式）----
# 优先级：环境变量 OPENCV_ROOT > CMake 参数 -DOPENCV_ROOT=... > 项目本地 tools/opencv/opencv
# 由 script/install_opencv.py 下载官方 Windows 预编译包，供 eSearch 窗口识别算法移植使用
if(DEFINED ENV{OPENCV_ROOT} AND NOT "$ENV{OPENCV_ROOT}" STREQUAL "")
    set(OpenCV_ROOT "$ENV{OPENCV_ROOT}")
elseif(DEFINED OpenCV_ROOT)
    # OpenCV_ROOT 透传（CMP0144：find_package 按包名大小写读取 OpenCV_ROOT）
else()
    set(OpenCV_ROOT "${CMAKE_SOURCE_DIR}/tools/opencv/opencv")
endif()
if(NOT EXISTS "${OpenCV_ROOT}/build/OpenCVConfig.cmake")
    message(FATAL_ERROR "未找到 OpenCV：${OpenCV_ROOT}/build/OpenCVConfig.cmake 不存在。"
        "请先运行 script/install_opencv.py 下载 OpenCV 到 tools/opencv（或设置 OPENCV_ROOT 指向含 build/ 的 OpenCV SDK 目录）")
endif()
set(OpenCV_DIR "${OpenCV_ROOT}/build")
# 官方 Windows 包仅提供 x64/vc16 运行时库；新编译器如 MSVC 19.51（VS18）不被
# OpenCVConfig.cmake 识别，故显式指定 ARCH/RUNTIME 让 find_package 正确找到 vc16 二进制
set(OpenCV_ARCH "x64")
set(OpenCV_RUNTIME "vc16")
find_package(OpenCV REQUIRED)

# ---- 分层：core（核心域） + ui（界面层） + app（可执行） ----
add_subdirectory(src/core)
add_subdirectory(src/ui)

qt_add_executable(Snipaste WIN32
    src/main.cpp
    src/app/MainWindow.cpp
    src/app/MainWindow.h
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

# 部署 OpenCV 运行库（opencv_world 动态库）到可执行文件旁，供窗口识别算法使用
# DLL 名按 OpenCV 版本生成：opencv_world${MAJOR}${MINOR}${PATCH}（4.10.0 → opencv_world4100.dll）
set(OPENCV_DLL_NAME "opencv_world${OpenCV_VERSION_MAJOR}${OpenCV_VERSION_MINOR}${OpenCV_VERSION_PATCH}.dll")
if(EXISTS "${OpenCV_ROOT}/build/x64/vc16/bin/${OPENCV_DLL_NAME}")
    add_custom_command(TARGET Snipaste POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${OpenCV_ROOT}/build/x64/vc16/bin/${OPENCV_DLL_NAME}"
            "$<TARGET_FILE_DIR:Snipaste>/${OPENCV_DLL_NAME}"
        COMMENT "Deploying OpenCV runtime (${OPENCV_DLL_NAME})"
    )
endif()

# 部署 PaddleOCR-json（离线 OCR 引擎，QProcess 子进程调用）
# 整个 paddleocr 目录复制到可执行文件旁，包含 exe、模型、配置文件
# opencv_world4100.dll 与根目录共用同一物理文件（硬链接）：子进程以 paddleocr/
# 为工作目录启动，Windows 加载器按 exe 所在目录 → CWD 找 DLL，硬链接让其在本目录
# 可见又不占双份空间。硬链接依赖根目录 DLL 先由上方 OpenCV 部署步骤生成
set(PADDLEOCR_DIR "${CMAKE_SOURCE_DIR}/tools/paddleocr")
if(EXISTS "${PADDLEOCR_DIR}/PaddleOCR-json.exe")
    add_custom_command(TARGET Snipaste POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${PADDLEOCR_DIR}"
            "$<TARGET_FILE_DIR:Snipaste>/paddleocr"
        COMMAND ${CMAKE_COMMAND} -E remove
            "$<TARGET_FILE_DIR:Snipaste>/paddleocr/opencv_world4100.dll"
        COMMAND ${CMAKE_COMMAND} -E create_hardlink
            "$<TARGET_FILE_DIR:Snipaste>/opencv_world4100.dll"
            "$<TARGET_FILE_DIR:Snipaste>/paddleocr/opencv_world4100.dll"
        COMMENT "Deploying PaddleOCR-json engine (opencv_world4100.dll 共用硬链接)"
    )
endif()

# 安装规则（可选）
install(TARGETS Snipaste RUNTIME DESTINATION .)
