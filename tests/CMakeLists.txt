# 域逻辑单元测试（QtTest，无 GUI）：标注画板模型、语言映射、
# 翻译 API 辅助函数、功能模块注册表。
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Test)

add_executable(poptools_domain_tests domain_tests.cpp
    # AppServices（含 FeatureRegistry 依赖的 QObject 实现）随测试目标编译
    "${CMAKE_CURRENT_SOURCE_DIR}/../src/app/AppServices.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../src/app/AppServices.h"
)
target_link_libraries(poptools_domain_tests PRIVATE
    poptools_core
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Test
)
target_include_directories(poptools_domain_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../src"
)

add_test(NAME domain_tests COMMAND poptools_domain_tests)
# Windows 下运行需 Qt DLL（Qt6Test/Qt6Core 等）在 PATH 中： prepend 本地 Qt 套件 bin
set_tests_properties(domain_tests PROPERTIES
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${CMAKE_PREFIX_PATH}/bin"
)
