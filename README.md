# PopTools

> Windows 全局实用小工具箱：截图 + 贴图 + 离线 OCR + 大模型翻译，更多小工具持续加入

[![License](https://img.shields.io/badge/License-GPLv3-blue.svg)](LICENSE)
![C++17](https://img.shields.io/badge/C%2B%2B-17-00599C)
![Qt 6.8](https://img.shields.io/badge/Qt-6.8.3-41CD52)
![PaddleOCR](https://img.shields.io/badge/OCR-PaddleOCR%20v1.4-5C3EE8)

PopTools 是一个常驻系统托盘的 Windows 全局实用小工具箱——热键一按，工具就「Pop」地弹出来。当前内置**全场景截图、贴图、本地离线 OCR 文字识别与大模型翻译**，后续会持续加入更多即按即用的小工具。

**适合谁用**：需要截图 + 文字识别 + 翻译的开发者、学生、办公人员；偏好离线 OCR 和私有 API 部署的用户。

---

## 功能特性

| 功能 | 说明 | 快捷键 |
|------|------|--------|
| **区域截图** | 多屏拼接捕捉，支持标注工具栏（矩形/椭圆/箭头/文字/马赛克等） | `F1` |
| **贴图** | 将截图钉到屏幕上，支持拖动/滚轮缩放/右键清除 | `F3` |
| **OCR 文字识别** | 本地离线 PaddleOCR，识别文字以文本框覆盖到画布，或打开文字栏面板 | 截图工具栏 扫描/译 按钮 |
| **大模型翻译** | 支持 OpenAI 兼容（DeepSeek / Ollama 等）与 Anthropic（Claude）两种接口格式；截图翻译 + 划词翻译；自动关闭模型思考模式保证响应速度 | - |
| **划词翻译** | 选中任意文字后弹出卡片，动作可切换：**翻译 / 解释 / 总结 / 改写**；支持语言切换（自动记忆上次目标语言）、拖拽调整卡片大小、一键复制 | `Ctrl+Alt+Shift+X` |
| **贴图 OCR / 翻译** | 贴图右键「识别文字 / 翻译」：识别钉在屏幕上的图片文字，文本/译文直接进剪贴板并气泡提示 | 贴图右键菜单 |
| **领域与术语表** | 设置中自定义翻译领域与专有名词翻译对，自动注入翻译提示词 | - |
| **窗口智能识别** | 自动识别鼠标下的窗口/控件级矩形（输入框、按钮、局部面板），辅助精确框选 | 截图时自动 |
| **系统托盘常驻** | 后台运行，托盘菜单快捷操作，开机自启可选 | - |

## 快速开始

```powershell
# 构建并运行
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
build/Release/PopTools.exe
```

> VS 2026 的生成器名为 `"Visual Studio 18 2026"`。
> 首次运行前需获取 Qt 套件与 OCR 引擎，见下方「外部依赖获取」。

## 技术栈

| 组件 | 说明 |
|---|---|
| 语言 | C++17 |
| GUI | Qt 6.8.3 LTS（LGPL v3 动态链接，本地套件 `tools/qt`） |
| 构建 | CMake + MSVC（Visual Studio 2022/2026） |
| OCR | PaddleOCR（PaddleOCR-json.exe 子进程管道模式，本地离线） |
| 翻译 | 大模型 API：OpenAI 兼容 `/chat/completions`（默认）与 Anthropic `/v1/messages` 双协议（DeepSeek / Ollama / Claude 等） |
| 窗口/控件识别 | Win32 窗口枚举 + Windows UI Automation（系统组件，无第三方依赖） |
| 热键 | Windows `RegisterHotKey`（user32） |

## 目录结构

```
├── CMakeLists.txt              # 根构建：配置 + 组装可执行
├── src/
│   ├── main.cpp                # 程序入口
│   ├── app/                    # 应用外壳（组合根）：托盘 + 服务装配 + 功能模块
│   │   └── modules/            #   功能模块（截图 / 贴图 / 划词翻译，可拔插）
│   ├── core/                   # 核心域（静态库 poptools_core）
│   │   ├── annotation/         #   标注模型 + 渲染（AnnotationCanvas/Painter）
│   │   ├── capture/            #   多屏捕获 + DPI 映射 + 窗口检测
│   │   ├── hotkey/             #   全局热键（GlobalHotKey）
│   │   ├── ocr/                #   OCR 服务（PaddleOCR-json 子进程调用）
│   │   ├── settings/           #   设置持久化（INI）
│   │   └── translation/        #   翻译服务 + 划词翻译
│   ├── ui/                     # 界面层（按功能分库，聚合为 poptools_ui）
│   │   ├── capture/            #   截图画布 + 标注工具栏 + OCR 面板
│   │   ├── pin/                #   贴图窗口
│   │   ├── settings/           #   设置对话框
│   │   └── translate/          #   划词翻译卡片
│   └── resources/              # 资源（qrc / 图标）
├── tests/                      # 域逻辑单元测试（QtTest，ctest 运行）
├── scripts/                    # 辅助脚本（纳管）：Qt / OCR 获取
├── temp_scripts/               # 临时脚本与中间产物（不推送）
├── tools/                      # 第三方二进制（不推送，由脚本生成）
│   ├── qt/6.8.3/msvc2022_64/   #   Qt 套件
│   └── paddleocr/              #   OCR 引擎（PaddleOCR-json.exe + models）
├── packaging/                  # 安装脚本
│   └── PopTools.iss
└── build/                      # 构建输出（CMake 生成，忽略）
```

## 外部依赖获取

项目不依赖全局安装的 SDK，Qt / OCR 均为**免安装绿色套件**，由脚本一键下载到项目内。

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

## 构建

```powershell
# 前置：安装依赖
python scripts/install_qt.py
python scripts/install_paddleocr.py

# 配置（VS 2022；VS 2026 用 "Visual Studio 18 2026"）
cmake -S . -B build -G "Visual Studio 17 2022" -A x64

# 构建 Release
cmake --build build --config Release

# 运行
build/Release/PopTools.exe
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
   - **操作**：撤销（`Ctrl+Z`）/ 重做（`Ctrl+Y`）/ 保存 / 复制 / 完成
4. `Enter` / 双击选区 / 点击"完成" → 确认截图（复制到剪贴板）
5. `Esc` 退出截图：没有标注时直接退出；已有标注时首次按 `Esc` 会在工具栏上方出现「再按一次 ESC 退出」轻提示（1 秒自动消失，按其他键或点击鼠标即继续编辑），再按一次 `Esc` 才退出，防止误触丢失标注

### OCR 文字识别

在截图编辑态中：
- **扫描按钮**：识别选区文字，以文本框覆盖到画布
- **【译】按钮**：识别文字并打开文字栏面板，可手动翻译

### 翻译

- **截图翻译**：文字栏面板中选择源/目标语言，点击「翻译」按钮；结果下方可一键复制
- **划词翻译**（`Ctrl+Alt+Shift+X`）：选中任意文字后，弹出翻译卡片，选择语言后点击「翻译」；卡片支持拖拽调整大小（拖边缘，调整后不再随内容变化），译文过长可在文本区内滚动查看，结果右下方可一键复制
- **目标语言记忆**：在划词卡片或文字栏切换目标语言后自动记住，下次翻译直接使用，不再回到默认中文；源语言不做记忆，每次均由模型自动检测
- **领域与术语表**：设置 →「翻译领域与术语」中可填写翻译领域（逗号分隔）与专有名词翻译对（每行 `原文=译文`），翻译时注入系统提示词，让模型按领域惯例与指定译名输出
- **接口格式**：支持 OpenAI 兼容（`/chat/completions` + Bearer，默认）与 Anthropic（`/v1/messages` + `x-api-key`）两种协议，在设置 →「接口格式」切换；Anthropic 格式 Base URL 需填对应端点（DeepSeek：`https://api.deepseek.com/anthropic`，官方 Claude：`https://api.anthropic.com`）
- 翻译请求会显式关闭模型思考模式（`thinking` 参数，DeepSeek 两种格式均已实测生效；端点不支持时自动降级重发），并对混入正文的 `<think>` 思维链做兜底清洗，保证结果直出
- 翻译使用 OpenAI 兼容 API，在设置中配置 Base URL / API Key / Model

### 贴图（F3）

将截图钉到屏幕上，始终置顶显示：
- **拖动**：按住鼠标左键移动位置
- **滚轮缩放**：0.2x ~ 5x
- **双击**：关闭贴图
- **右键菜单**：识别文字 / 翻译（文本/译文进剪贴板 + 气泡提示）/ 清除（仅屏幕）/ 销毁（同时删除文件）
- 贴图自动保存到 `exe目录/tempPopTools/`

### 窗口智能识别

截图时自动识别鼠标下方的矩形区域并预高亮，从窗口级细化到**控件级**：输入框、按钮、卡片、列表项等页面内元素均可直接点选。识别基于系统窗口枚举 + Windows UI Automation，不依赖图像分析，不会误选文字笔画闭合区域（如字母 o/e 的内圈）；对不支持 UIA 的程序自动退回窗口级矩形。按住拖动可随时改为手动框选。

## 设置

托盘右键 → 设置：

> 「翻译 API」区提供**测试连接**按钮：用表单当前值（保存前也可）发一次真实翻译请求，成功/Key 无效/模型名错误均会给出中文原因；API Key 支持「显示」切换，便于核对粘贴内容。

| 配置项 | 说明 | 默认值 |
|---|---|---|
| 开机启动 | 登录时自动运行 | 关闭 |
| 接口格式 | 翻译 API 协议：OpenAI 兼容 / Anthropic | OpenAI 兼容 |
| API Base URL | OpenAI 兼容端点；选 Anthropic 格式时填其端点（如 DeepSeek `https://api.deepseek.com/anthropic`、官方 `https://api.anthropic.com`） | `https://api.deepseek.com/v1` |
| API Key | 密钥（自行填入） | 空 |
| Model | 模型名 | `deepseek-v4-flash` |
| 翻译领域与术语 | 自定义领域（逗号分隔）与专有名词翻译对（`原文=译文` 每行一条） | 空 |
| 划词翻译热键 | 虚拟键码 + 修饰符 | `Ctrl+Alt+Shift+X` |
| 贴图保存目录 | 贴图自动保存路径 | `exe目录/tempPopTools/` |

> 设置持久化到 `exe目录/PopTools.ini`，便于查看与备份。
> 翻译 API 可换成任意 OpenAI 兼容服务，如 Ollama 本地：`http://localhost:11434/v1`

## 项目架构

采用**分层 + 功能模块化**架构：core 与 ui 各为静态库（ui 内按功能分库），app 可执行承载组合根与功能模块，模块间通过 `poptools` 命名空间解耦。截图 / 贴图 / 划词翻译是三个自包含的功能模块（`src/app/modules/`），各自注册热键、贡献托盘菜单、管理自己的窗口；**拔插一个功能 = 组合根删一处 `add()` + CMake 删一个目标**，后续新增小工具即照此模式接入。

```
app/         应用层（可执行入口）
  ├─ MainWindow  组合根：托盘 + 服务装配 + 模块注册
  └─ modules     功能模块（capture / pin / selectionTranslate，可拔插）
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
  ├─ settings     设置对话框
  └─ translate    划词翻译卡片
```

## License

本项目采用 [GNU General Public License v3.0（GPL-3.0）](LICENSE)授权：任何二次开发、分发的衍生作品都必须以 GPL-3.0 开源，闭源商用不被允许。

Qt 按 LGPL v3 动态链接使用（Qt 自身许可证不变）。
