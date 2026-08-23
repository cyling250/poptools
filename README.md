# MySnipaste

> 全场景截图软件：截图 + OCR 文字识别 + 大模型翻译 + 贴图，无需购买 Snipaste PRO

[![License](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](LICENSE)
![C++17](https://img.shields.io/badge/C%2B%2B-17-00599C)
![Qt 6.8](https://img.shields.io/badge/Qt-6.8.3-41CD52)
![PaddleOCR](https://img.shields.io/badge/OCR-PaddleOCR%20v1.4-5C3EE8)

个人使用的全场景截图软件 —— **Snipaste 免费替代方案**，内置本地离线 OCR 文字识别与大模型翻译，无需购买 Snipaste PRO。

**适合谁用**：需要截图 + 文字识别 + 翻译的开发者、学生、办公人员；偏好离线 OCR 和私有 API 部署的用户。

---

## 功能特性

| 功能 | 说明 | 快捷键 |
|------|------|--------|
| **区域截图** | 多屏拼接捕捉，支持标注工具栏（矩形/椭圆/箭头/文字/马赛克等） | `F1` |
| **贴图** | 将截图钉到屏幕上，支持拖动/滚轮缩放/右键清除 | `F3` |
| **OCR 文字识别** | 本地离线 PaddleOCR，识别文字以文本框覆盖到画布，或打开文字栏面板 | 截图工具栏【文】【译】 |
| **大模型翻译** | OpenAI 兼容 API（DeepSeek / Ollama 等），支持截图翻译与划词翻译 | - |
| **划词翻译** | 选中任意文字后弹出翻译卡片，支持语言切换 | `Ctrl+X` |
| **窗口智能识别** | 自动检测窗口边缘 + 图像边缘识别，辅助精确框选 | 截图时自动 |
| **系统托盘常驻** | 后台运行，托盘菜单快捷操作，开机自启可选 | - |

## 快速开始

```powershell
# 构建并运行
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
build/Release/MySnipaste.exe
```

> 首次运行前需获取 Qt 套件与 OCR 引擎，见下方「外部依赖获取」。

## 技术栈

| 组件 | 说明 |
|---|---|
| 语言 | C++17 |
| GUI | Qt 6.8.3 LTS（LGPL v3 动态链接，本地套件 `tools/qt`） |
| 构建 | CMake + MSVC（Visual Studio 2022/2026） |
| OCR | PaddleOCR（PaddleOCR-json.exe 子进程管道模式，本地离线） |
| 翻译 | OpenAI 兼容 `/chat/completions` API（DeepSeek / Ollama 等） |
| 窗口识别 | OpenCV 4.10（图像边缘检测，eSearch 算法移植，`tools/opencv`） |
| 热键 | Windows `RegisterHotKey`（user32） |

## 目录结构

```
├── CMakeLists.txt              # 根构建：配置 + 组装可执行
├── src/
│   ├── main.cpp                # 程序入口
│   ├── app/MainWindow.h/.cpp   # 应用控制器：托盘 + 全局热键 + 服务编排
│   ├── core/                   # 核心域（静态库 snipaste_core）
│   │   ├── annotation/         #   标注模型 + 渲染（AnnotationCanvas/Painter）
│   │   ├── capture/            #   多屏捕获 + DPI 映射 + 窗口检测
│   │   ├── hotkey/             #   全局热键（GlobalHotKey）
│   │   ├── ocr/                #   OCR 服务（PaddleOCR-json 子进程调用）
│   │   ├── settings/           #   设置持久化（INI）
│   │   └── translation/        #   翻译服务 + 划词翻译
│   ├── ui/                     # 界面层（静态库 snipaste_ui）
│   │   ├── capture/            #   截图画布 + 标注工具栏 + OCR 面板
│   │   ├── pin/                #   贴图窗口
│   │   ├── result/             #   结果面板
│   │   ├── settings/           #   设置对话框
│   │   └── translate/          #   划词翻译卡片
│   └── resources/              # 资源（qrc / 图标）
├── scripts/                    # 辅助脚本（纳管）：Qt / OCR / OpenCV 获取
├── temp_scripts/               # 临时脚本与中间产物（不推送）
├── tools/                      # 第三方二进制（不推送，由脚本生成）
│   ├── qt/6.8.3/msvc2022_64/   #   Qt 套件
│   ├── paddleocr/              #   OCR 引擎（PaddleOCR-json.exe + models）
│   └── opencv/                 #   OpenCV 4.10（opencv_world 动态库）
├── packaging/                  # 安装脚本
│   └── MySnipaste.iss
└── build/                      # 构建输出（CMake 生成，忽略）
```

## 外部依赖获取

项目不依赖全局安装的 SDK，Qt / OCR / OpenCV 均为**免安装绿色套件**，由脚本一键下载到项目内。

### 1. Qt 6.8.3 → `tools/qt`

```powershell
python scripts/install_qt.py
```

经 `127.0.0.1:7897` 代理访问 Qt 官方仓库，仅下载 `qtbase` / `qtsvg` / `qttools` 模块。构建后 `windeployqt` 自动部署 DLL 到 exe 旁。

### 2. PaddleOCR → `tools/paddleocr`

```powershell
python scripts/install_paddleocr.py
```

免安装，下载 PaddleOCR-json v1.4.1 发布包 + 7-Zip 解压，附带简中/繁中/英/日/韩/俄模型。运行时由 PaddleOcrService 通过 QProcess 子进程管道模式调用。

### 3. OpenCV 4.10 → `tools/opencv`

```powershell
python scripts/install_opencv.py
```

下载官方 Windows 预编译包（vc16），供窗口图像边缘识别使用。

## 构建

```powershell
# 前置：安装依赖
python scripts/install_qt.py
python scripts/install_paddleocr.py
python scripts/install_opencv.py

# 配置（VS 2022）
cmake -S . -B build -G "Visual Studio 17 2022" -A x64

# 构建 Release
cmake --build build --config Release

# 运行
build/Release/MySnipaste.exe
```

> 环境变量 `QT_ROOT` 或 CMake 参数 `-DQT_ROOT=...` 可覆盖 Qt 套件路径。
> 若 `tools/paddleocr` 不存在，构建跳过 OCR 引擎拷贝，不阻断构建（运行时 OCR 提示不可用）。

## 使用指南

### 截图（F1）

1. 按 `F1` 进入截图模式，拖拽框选区域
2. 松开鼠标进入**编辑态**，选区高亮，底部弹出标注工具栏
3. 工具栏支持：
   - **形状**：矩形 / 椭圆 / 直线 / 箭头
   - **画笔**：铅笔 / 涂鸦笔 / 马赛克 / 橡皮擦
   - **文字**：在选区中添加文字标注
   - **属性**：线宽 / 填充 / 16 色颜色板
   - **操作**：撤销 / 重做 / 保存 / 复制 / 完成
4. `Enter` / 双击选区 / 点击"完成" → 确认截图（复制到剪贴板）
5. `Esc` 取消退出

### OCR 文字识别

在截图编辑态中：
- **【文】按钮**：识别选区文字，以文本框覆盖到画布
- **【译】按钮**：识别文字并打开文字栏面板，可手动翻译

### 翻译

- **截图翻译**：文字栏面板中选择源/目标语言，点击「翻译」按钮
- **划词翻译**（`Ctrl+X`）：选中任意文字后，弹出翻译卡片，选择语言后点击「翻译」
- 翻译使用 OpenAI 兼容 API，在设置中配置 Base URL / API Key / Model

### 贴图（F3）

将截图钉到屏幕上，始终置顶显示：
- **拖动**：按住鼠标左键移动位置
- **滚轮缩放**：0.2x ~ 5x
- **双击**：关闭贴图
- **右键菜单**：清除（仅屏幕）/ 销毁（同时删除文件）
- 贴图自动保存到 `exe目录/tempSnipaste/`

### 窗口智能识别

截图时自动检测窗口边缘，鼠标悬停时高亮提示可框选的窗口。同时支持图像边缘识别（需在设置中开启「框选图像识别」）。

## 设置

托盘右键 → 设置：

| 配置项 | 说明 | 默认值 |
|---|---|---|
| 开机启动 | 登录时自动运行 | 关闭 |
| API Base URL | OpenAI 兼容端点 | `https://api.deepseek.com/v1` |
| API Key | 密钥（自行填入） | 空 |
| Model | 模型名 | `deepseek-v4-flash` |
| 划词翻译热键 | 虚拟键码 + 修饰符 | `Ctrl+X` |
| 框选图像识别 | 启用图像边缘检测辅助框选 | 关闭 |
| 贴图保存目录 | 贴图自动保存路径 | `exe目录/tempSnipaste/` |

> 设置持久化到 `exe目录/MySnipaste.ini`，便于查看与备份。
> 翻译 API 可换成任意 OpenAI 兼容服务，如 Ollama 本地：`http://localhost:11434/v1`

## 项目架构

采用**三层分层**架构，模块间通过 `snipaste` 命名空间解耦：

```
app/         应用层（可执行入口）
  └─ MainWindow  托盘 + 热键 + 服务编排
core/        核心域（静态库，与 UI 解耦）
  ├─ annotation   标注模型与渲染
  ├─ capture      多屏捕获与窗口检测
  ├─ hotkey       全局热键
  ├─ ocr          OCR 服务
  ├─ settings     设置持久化
  └─ translation  翻译与划词翻译
ui/          界面层（静态库，依赖 core）
  ├─ capture      截图窗口 + 工具栏 + OCR 面板
  ├─ pin          贴图窗口
  ├─ result       结果面板
  ├─ settings     设置对话框
  └─ translate    划词翻译卡片
```

## License

本项目采用 [Apache License 2.0](LICENSE)。

Qt 按 LGPL v3 动态链接使用（Qt 自身许可证不变）。