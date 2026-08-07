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
| GUI | Qt 6.8.3 LTS（LGPL v3 开源版，动态链接，个人使用免费） |
| 构建 | CMake + MSVC（Visual Studio 2022/2026 均可） |
| OCR | Tesseract 5（命令行调用，含 chi_sim / eng 语言包） |
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
│   │   ├── ocr/                #   Tesseract OCR 服务
│   │   ├── settings/           #   设置持久化
│   │   └── translation/        #   翻译服务
│   ├── ui/                     # 界面层（静态库 snipaste_ui，依赖 core）
│   │   ├── capture/            #   截图画布 + 标注工具栏
│   │   ├── result/             #   结果面板（截图 + OCR + 翻译）
│   │   ├── settings/           #   设置对话框
│   │   └── pin/                #   贴图窗口
│   └── resources/              # 资源（qrc / 图标 / tessdata 语言包）
└── .trae/rules/                # 项目规则与文档
```

## 构建

前置依赖：
- [Qt 6.8.3 MSVC 2022 64bit](https://www.qt.io/download-open-source)（LGPL 开源版）
- CMake ≥ 3.21
- Visual Studio（含 C++ 桌面开发）
- [Tesseract 5](https://github.com/UB-Mannheim/tesseract/releases)（用于 OCR，安装后保留默认路径即可）

```powershell
# 设置 Qt 路径（环境变量 QT_ROOT，若已设置可跳过）
$env:QT_ROOT = "D:\Qt\6.8.3\msvc2022_64"   # 或用 [Environment]::SetEnvironmentVariable("QT_ROOT", "...", "User") 持久化

cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release

# 部署 Qt 运行库（拷贝 DLL 到 exe 目录）
& "$env:QT_ROOT\bin\windeployqt.exe" --release build/Release/MySnipaste.exe
```

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
   - **识别文字 (OCR)**：识别截图中的文字（需已安装 Tesseract）
   - **翻译**：将识别文本翻译为目标语言（需在"设置"中配置 API）

## 设置

托盘右键 → 设置：

| 配置项 | 说明 | 默认值 |
|---|---|---|
| Tesseract 路径 | OCR 引擎可执行文件路径 | `C:\Program Files\Tesseract-OCR\tesseract.exe` |
| 翻译模式 | `api`（大模型 API）/ `local`（本地引擎，预留） | `api` |
| API Base URL | OpenAI 兼容端点 | `https://api.deepseek.com/v1` |
| API Key | 密钥（自行填入） | 空 |
| Model | 模型名 | `deepseek-chat` |
| 目标语言 | 翻译目标语言代码 | `zh` |

> 翻译 API 可换成任意 OpenAI 兼容服务，如 Ollama 本地：`http://localhost:11434/v1`

## License

本项目采用 [Apache License 2.0](LICENSE)。

Qt 按 LGPL v3 动态链接使用（Qt 自身许可证不变）。
