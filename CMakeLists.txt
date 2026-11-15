cmake_minimum_required(VERSION 3.21)
project(PopTools VERSION 0.4.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 源码/执行字符集统一 UTF-8：源文件为 UTF-8（无 BOM），MSVC 默认按系统 ACP
# 解释裸中文字面量，在 GBK 代码页机器上编译期即乱码（枚举提示/工具栏文案等）
if(MSVC)
    add_compile_options(/utf-8)
endif()

# Release 构建生成 PDB（T-051 崩溃自诊的符号化需要；/Zi 不改变代码生成，
# /OPT:REF /OPT:ICF 维持 Release 尺寸纪律）
option(POPTOOLS_RELEASE_PDB "Release 构建生成 PDB（崩溃栈符号化）" ON)
if(MSVC AND POPTOOLS_RELEASE_PDB)
    string(APPEND CMAKE_CXX_FLAGS_RELEASE " /Zi")
    string(APPEND CMAKE_EXE_LINKER_FLAGS_RELEASE " /DEBUG /OPT:REF /OPT:ICF")
endif()

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

# ---- 分层：core（核心域） + ui（界面层） + app（基础设施 + 功能模块 + 可执行） ----
add_subdirectory(src/core)
add_subdirectory(src/ui)
add_subdirectory(src/app)

# ---- 桌面日历 Explorer 注入 DLL（照搬 E:\rlqd DesktopHook 机制，纯 Win32）----
# WH_CALLWNDPROC 钩子注入 explorer，子类化 SHELLDLL_DefView/SysListView32，
# WM_ERASEBKGND 中 AlphaBlend 共享内存位图；输出与 PopTools.exe 同目录
#（应用侧经 QLibrary 从 exe 目录加载并 StartDock）
add_library(poptools_calendar_hook SHARED src/hook/calendar_hook.cpp)
set_target_properties(poptools_calendar_hook PROPERTIES OUTPUT_NAME PopToolsCalendarHook)
target_compile_definitions(poptools_calendar_hook PRIVATE WIN32_LEAN_AND_MEAN)
target_link_libraries(poptools_calendar_hook PRIVATE user32 gdi32 comctl32 msimg32)

# ---- 单元测试（可选，默认开启；ctest 运行） ----
option(POPTOOLS_BUILD_TESTS "Build unit tests" ON)
if(POPTOOLS_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

qt_add_executable(PopTools WIN32
    src/main.cpp
    src/app/MainWindow.cpp
    src/app/MainWindow.h
    src/app/app.rc
    src/resources/resources.qrc
)

target_link_libraries(PopTools PRIVATE
    poptools_modules      # 功能模块聚合（截图 / 贴图 / 划词翻译；拔插改此处一行）
    poptools_ui_settings  # 设置界面（外壳自有，不属于任何模块）
    poptools_app_infra    # 组合根基础设施（AppServices / FeatureRegistry）
)

# Qt 运行库：构建（make）完成后由本地 Qt 套件自带的 windeployqt 自动部署到
# 可执行文件旁（Release 输出目录），使 exe 不依赖全局 Qt 安装；同时供安装程序打包
set(QT_BIN_DIR "${CMAKE_PREFIX_PATH}/bin")
if(NOT EXISTS "${QT_BIN_DIR}/windeployqt.exe")
    message(FATAL_ERROR "windeployqt.exe not found at ${QT_BIN_DIR}. "
        "请先运行 script/install_qt.py 下载 Qt 到 tools/qt（或设置 QT_ROOT 指向 Qt 套件目录）")
endif()
add_custom_command(TARGET PopTools POST_BUILD
    # -no-translations：本机 Qt 套件不含 translations/ 目录（未下载），且应用不加载
    # QTranslator/.qm，跳过翻译部署以消除 windeployqt 的 "(developer build?)" 警告。
    # 不用 windeployqt 的 --compiler-runtime：其探测逻辑识别不了本机 VS 2026 的
    # Redist 目录，加了也不会拷贝（已实测），VC 运行库改由下方显式部署
    COMMAND "${QT_BIN_DIR}/windeployqt.exe" --release
        -no-translations "$<TARGET_FILE:PopTools>"
    COMMENT "Deploying Qt runtime via windeployqt"
    # 纯 Widgets 应用不会加载 D3D 着色器编译器（dxcompiler/dxil/D3Dcompiler
    # 仅供 RHI/Qt Quick 的 D3D 后端使用，Qt6Gui/qwindows 均无静态导入），
    # windeployqt 默认会部署三件约 20MB，紧随其后剔除以免混入安装包
    COMMAND ${CMAKE_COMMAND} -E rm -f
        "$<TARGET_FILE_DIR:PopTools>/dxcompiler.dll"
        "$<TARGET_FILE_DIR:PopTools>/dxil.dll"
        "$<TARGET_FILE_DIR:PopTools>/D3Dcompiler_47.dll"
)

# VC 运行库（msvcp140/vcruntime140 等）：从当前编译器路径回溯 VS 安装根，
# 定位 VC/Redist/MSVC/<版本>/x64/Microsoft.VC*.CRT（多版本并存时取排序最后，
# 跳过 onecore 变体），部署到 exe 旁——根目录此前缺这些 DLL，未装 VC++ Redist
# 的机器无法启动
get_filename_component(_vs_root "${CMAKE_CXX_COMPILER}" DIRECTORY) # .../bin/Hostx64/x64
foreach(_i RANGE 0 6)
    get_filename_component(_vs_root "${_vs_root}" DIRECTORY)
endforeach()
# 此时 _vs_root = <VS 安装根>（x64 向上 7 级：Hostx64/bin/<ver>/MSVC/Tools/VC/<VS根>）
file(GLOB _vc_crt_dirs "${_vs_root}/VC/Redist/MSVC/*/x64/Microsoft.VC*.CRT")
list(FILTER _vc_crt_dirs EXCLUDE REGEX "/onecore/")
list(SORT _vc_crt_dirs)
if(_vc_crt_dirs)
    list(GET _vc_crt_dirs -1 _vc_crt_dir)
    message(STATUS "VC CRT redist: ${_vc_crt_dir}")
    file(GLOB _vc_crt_dlls "${_vc_crt_dir}/*.dll")
    foreach(_dll IN LISTS _vc_crt_dlls)
        add_custom_command(TARGET PopTools POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_dll}" "$<TARGET_FILE_DIR:PopTools>"
        )
    endforeach()
else()
    message(WARNING "未找到 VC Redist CRT（${_vs_root}/VC/Redist/MSVC/*/x64），"
        "部署目录将缺 VC 运行库，请手动拷贝")
endif()

# 部署 RapidOCR-json（离线 OCR 引擎，onnxruntime 静态单文件，QProcess 子进程调用；
# 替代 PaddleOCR-json 全栈 297MB → 55MB：中/英模型，四套未使用的多语言 rec 已由
# install_rapidocr.py 裁剪，协议同构见 RapidOcrService）
# 整个 rapidocr 目录复制到可执行文件旁（exe + models/，子进程以 rapidocr/ 为
# 工作目录启动）；并清理旧引擎 paddleocr/ 残留，防其混入安装包
set(RAPIDOCR_DIR "${CMAKE_SOURCE_DIR}/tools/rapidocr")
if(EXISTS "${RAPIDOCR_DIR}/RapidOCR-json.exe")
    add_custom_command(TARGET PopTools POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E rm -rf
            "$<TARGET_FILE_DIR:PopTools>/paddleocr"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${RAPIDOCR_DIR}"
            "$<TARGET_FILE_DIR:PopTools>/rapidocr"
        COMMENT "Deploying RapidOCR-json engine"
    )
endif()

# 安装规则（可选）
install(TARGETS PopTools RUNTIME DESTINATION .)
