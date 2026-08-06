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

```
├── CMakeLists.txt          # CMake 构建配置
├── src/
│   ├── main.cpp            # 程序入口
│   ├── MainWindow.h/.cpp   # 主窗口：系统托盘 + 全局热键 + 结果面板
│   ├── CaptureWindow.h/.cpp# 截图窗口：全屏遮罩 + 选区
│   ├── OcrService.h/.cpp   # Tesseract OCR 服务
│   ├── Translator.h/.cpp   # 翻译服务（本地/大模型 API 可配置）
│   ├── Settings.h/.cpp     # 设置持久化（QSettings）
│   └── resources/tessdata/ # Tesseract 中文语言包
└── .trae/rules/            # 项目规则与文档
```

## 构建

前置依赖：
- [Qt 6.8.3 MSVC 2022 64bit](https://www.qt.io/download-open-source)（LGPL 开源版）
- CMake ≥ 3.21
- Visual Studio（含 C++ 桌面开发）
- [Tesseract 5](https://github.com/UB-Mannheim/tesseract/releases)（用于 OCR，安装后保留默认路径即可）

```powershell
# 修改 CMakeLists.txt 中 CMAKE_PREFIX_PATH 为你的 Qt 安装路径
set(CMAKE_PREFIX_PATH "F:/Qt/6.8.3/msvc2022_64")

cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release

# 部署 Qt 运行库（拷贝 DLL 到 exe 目录）
F:/Qt/6.8.3/msvc2022_64/bin/windeployqt.exe --release build/Release/MySnipaste.exe
```

## 使用

1. 运行 `build/Release/MySnipaste.exe`，程序驻留系统托盘
2. 按 `F1` 进入截图模式，拖拽框选区域，松开即完成截图（自动复制到剪贴板）
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

个人使用，非商用。Qt 按 LGPL v3 动态链接使用。
