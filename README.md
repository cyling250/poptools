# MySnipaste

个人使用的全场景截图软件 —— Snipaste 免费替代方案，内置 **OCR 文字识别** 与 **翻译** 功能（无需购买 Snipaste PRO）。

## 功能特性

- **区域截图**：全局热键 `F1` 一键截图，支持多显示器（拼接捕捉，扩展屏无黑屏）
- **OCR 文字识别**：基于 Tesseract 5，本地离线识别中英文（语言包随程序分发）
- **翻译**：OpenAI 兼容大模型 API（可配置 Base URL / Key / Model），也预留本地引擎接口
- **系统托盘常驻**：后台运行，托盘菜单快捷操作
- **剪贴板联动**：截图自动复制到剪贴板

## 技术栈

| 组件 | 说明 |
|---|---|
| 语言 | C++17 |
| GUI | Qt 6.8.3 LTS（LGPL v3 开源版，动态链接；套件本地下载于 `tools/qt`） |
| 构建 | CMake + MSVC（Visual Studio 2022/2026 均可） |
| OCR | Tesseract 5（`libtesseract-5.dll` 进程内 LoadLibrary 加载，无外挂 exe，含 chi_sim / eng 语言包） |
| 翻译 | OpenAI 兼容 `/chat/completions` API（DeepSeek / Ollama 等） |
| 热键 | Windows `RegisterHotKey`（user32） |

## 目录结构

采用标准 Qt/C++ 中大型项目组织：**三层分层（core / ui / app）+ 领域模块化**，引入 `snipaste` 命名空间。

```
├── CMakeLists.txt              # 根构建：配置 + 组装可执行
├── src/
│   ├── main.cpp                # 程序入口
│   ├── app/MainWindow.h/.cpp   # 应用控制器：托盘 + 全局热键 + 服务编排
│   ├── core/                   # 核心域（静态库 snipaste_core，与 UI 解耦）
│   │   ├── annotation/         #   标注模型 + 渲染（AnnotationCanvas/Painter）
│   │   ├── capture/            #   多屏捕获 + DPI 映射（ScreenCapturer）
│   │   ├── hotkey/             #   全局热键（GlobalHotKey）
│   │   ├── ocr/                #   OCR 服务（进程内加载 libtesseract-5.dll）
│   │   ├── settings/           #   设置持久化
│   │   └── translation/        #   翻译服务
│   ├── ui/                     # 界面层（静态库 snipaste_ui，依赖 core）
│   │   ├── capture/            #   截图画布 + 标注工具栏
│   │   ├── result/             #   结果面板（截图 + OCR + 翻译）
│   │   ├── settings/           #   设置对话框
│   │   └── pin/                #   贴图窗口
│   └── resources/              # 资源（qrc / 图标）
├── scripts/                    # 复用辅助脚本（纳入版本控制）：依赖获取
│   ├── install_qt.py           #   Qt 套件下载（aqtinstall，经 7897 代理）
│   └── install_tesseract.py    #   OCR 引擎获取（7-Zip 便携解压，免安装）
├── temp_scripts/               # 实验/一次性脚本与中间产物（不推送）
│   ├── out/                    #   脚本中间产物（下载的安装包等，可清理）
│   └── tool/                   #   外部工具（便携 7-Zip，免安装、不写注册表）
├── tools/                      # 第三方二进制资源（不推送，由脚本生成）
│   ├── qt/6.8.3/msvc2022_64/   #   Qt 套件（qtbase/qtsvg/qttools）
│   └── tesseract/              #   OCR 引擎（libtesseract-5.dll + 依赖 DLL + tessdata）
├── packaging/                  # Inno Setup 安装脚本
│   └── MySnipaste.iss
└── build/                      # 构建输出（CMake 生成，被忽略）
```

## 外部依赖的获取

项目不依赖任何"全局安装"的 SDK：Qt 与 OCR 引擎都是**免安装的绿色套件**，经 `scripts/` 下的脚本一键下载到项目内，构建时自动复制到可执行文件旁。共两个外部依赖：

### 1. Qt 6.8.3 套件 → `tools/qt`

由 [scripts/install_qt.py](scripts/install_qt.py) 完成：

- 经 `127.0.0.1:7897` 代理访问 Qt 官方仓库（首次运行自动用 `uv` 创建 `.venv` 并安装 aqtinstall）
- 仅下载构建/运行所需模块：`qtbase`（Core/Gui/Widgets/Network）、`qtsvg`、`qttools`（含 windeployqt），避免整套 Qt（约 2GB）
- 输出到 `tools/qt/6.8.3/msvc2022_64`，作为 CMake 的 `CMAKE_PREFIX_PATH`（见根 [CMakeLists.txt](CMakeLists.txt#L14-L25)）
- 构建完成后由 `windeployqt` 把 Qt 运行库（DLL + 插件）自动部署到 Release 输出目录 → **exe 不依赖系统 Qt**

```powershell
python scripts\install_qt.py
```

### 2. Tesseract OCR 引擎 → `tools/tesseract`

由 [scripts/install_tesseract.py](scripts/install_tesseract.py) 完成，全程**免安装、不污染注册表**：

- 经 `127.0.0.1:7897` 代理下载 UB-Mannheim 官方 NSIS 安装器（Windows 唯一官方二进制来源）
- 用 **7-Zip 便携版**解压（NSIS 格式 RAR 不支持；7-Zip 由 Windows 自带 `msiexec /a` 从官方 MSI 管理式解包，工具落在 `temp_scripts/tool/7zip`，不装系统）
- 补齐 `eng` / `chi_sim` 语言包（tessdata_fast），清理安装器残留
- 输出 `libtesseract-5.dll` + MSYS2 依赖 DLL + `tessdata/`

```powershell
python scripts\install_tesseract.py
```

**OCR 工作原理**：运行时 [OcrService.cpp](src/core/ocr/OcrService.cpp) 通过 `LoadLibrary` 加载 `{exeDir}/tesseract/libtesseract-5.dll`（进程内 DLL 调用，无外挂 exe、无临时文件），用 C API（`TessBaseAPICreate` / `Init2` / `SetImage` / `GetIterator` 等）识别并输出行级文本框。构建时由 CMake POST_BUILD 把整个 `tools/tesseract` 复制到可执行文件旁（见根 [CMakeLists.txt](CMakeLists.txt#L43-L54)）。

## 构建

前置依赖：
- CMake ≥ 3.21
- Visual Studio（含 C++ 桌面开发，2022 / 2026 均可）
- Python ≥ 3.10 或 [uv](https://docs.astral.sh/uv/)（仅用于依赖获取脚本）

```powershell
# 1) 获取 Qt 套件（tools/qt）
python scripts\install_qt.py

# 2) 获取 OCR 引擎（tools/tesseract）
python scripts\install_tesseract.py

# 3) 配置 + 构建（默认使用 tools/qt 本地套件，无需设置 QT_ROOT）
#    VS 2022:  -G "Visual Studio 17 2022"     VS 2026: -G "Visual Studio 18 2026"
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release

# 4) 运行（Qt 运行库与 OCR 引擎已由 POST_BUILD 自动部署到 Release）
build/Release/MySnipaste.exe
```

> 如需覆盖 Qt 套件位置，可设置环境变量 `QT_ROOT` 或传 `-DQT_ROOT=...`（优先级高于默认的 `tools/qt`）。
> 若 `tools/tesseract` 不存在，构建会跳过 OCR 引擎拷贝，不阻断构建（运行时 OCR 提示引擎缺失）。

## 使用

1. 运行 `build/Release/MySnipaste.exe`，程序驻留系统托盘
2. 按 `F1` 进入截图模式，拖拽框选区域：
   - 松开鼠标进入**编辑态**：选区高亮（外部灰色蒙版），蓝色 1.5pt 边框
   - 拖动 8 个控制点（四角 + 四边中点）精调选区
   - 点击选区内拖动可整体移动选区
   - 底部弹出**标注工具栏**（图标式，两行紧凑布局）：
     - 工具行：矩形 / 椭圆 / 直线 / 箭头 / 铅笔 / 涂鸦笔 / 马赛克 / 文字 / 橡皮擦（互斥高亮，点灭进入选择模式可拖动画布）
     - 属性行：小 / 中 / 大线宽、填充 / 纯线、16 色颜色板 + 当前色块
     - 操作：撤销 / 重做 / 保存 / 复制 / 完成
   - **Enter / 双击选区 / 工具栏"完成"** 确认截图（自动复制到剪贴板）
   - **Esc** 取消退出
3. 结果面板中：
   - **识别文字 (OCR)**：识别截图中的文字（引擎随程序分发，无需安装）
   - **翻译**：将识别文本翻译为目标语言（需在"设置"中配置 API）

## 设置

托盘右键 → 设置：

| 配置项 | 说明 | 默认值 |
|---|---|---|
| 开机启动 | 登录时自动运行（写入 HKCU\...\Run，按钮随状态切换"加入/取消"） | 关闭 |
| 翻译模式 | `api`（大模型 API）/ `local`（本地引擎，预留） | `api` |
| API Base URL | OpenAI 兼容端点 | `https://api.deepseek.com/v1` |
| API Key | 密钥（自行填入） | 空 |
| Model | 模型名 | `deepseek-v4-flash` |
| 目标语言 | 翻译目标语言代码 | `zh` |

> 设置持久化到**可执行文件旁**的 `MySnipaste.ini`（构建输出/安装目录下），便于查看与备份；
> 贴图（F3）默认保存到软件目录下的 `tempSnipaste` 文件夹。

> 翻译 API 可换成任意 OpenAI 兼容服务，如 Ollama 本地：`http://localhost:11434/v1`

## License

本项目采用 [Apache License 2.0](LICENSE)。

Qt 按 LGPL v3 动态链接使用（Qt 自身许可证不变）。
