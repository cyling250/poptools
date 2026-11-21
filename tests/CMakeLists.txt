# 域逻辑单元测试（QtTest，无 GUI）：标注画板模型、语言映射、
# 翻译 API 辅助与请求体构造、功能模块注册表、设置读写回退。
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Test)

add_executable(poptools_domain_tests domain_tests.cpp)
target_link_libraries(poptools_domain_tests PRIVATE
    poptools_app_infra  # AppServices/FeatureRegistry（链接库目标，不再手抄源文件）
    poptools_core
    poptools_ui_goody   # GoodyBallLogic（悬浮球手势/位置钳制用例）
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Test
)
target_include_directories(poptools_domain_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../src"
)

# 二维码识别回归测试夹具（生成器脚本一次性生成，测试只依赖解码结果）
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/fixtures/qr_test.png"
               "${CMAKE_CURRENT_BINARY_DIR}/fixtures/qr_test.png" COPYONLY)

add_test(NAME domain_tests COMMAND poptools_domain_tests)
# Windows 下运行需 Qt DLL（Qt6Test/Qt6Core 等）在 PATH 中： prepend 本地 Qt 套件 bin
set_tests_properties(domain_tests PROPERTIES
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${CMAKE_PREFIX_PATH}/bin"
)
