# 项目交接文档

> 本文档由 AI 维护，用于任务交接。任务完成后写入「历史任务」，未完成任务见「当前任务」。
> 最后更新：2026-09-02

## 项目快照

- **项目名称**：PopTools（前身为 Snipaste，2026-09-02 更名；更早为 MySnipaste）
- **一句话描述**：Windows 全局实用小工具箱（常驻托盘、热键唤起、功能模块可拔插）——当前内置区域截图 + 本地离线 OCR（PaddleOCR）+ 大模型翻译（OpenAI 兼容 / Anthropic 双协议）+ 贴图，持续新增小工具
- **开源协议**：GPL-3.0（2026-09-02 由 Apache-2.0 变更；衍生作品必须同协议开源，闭源商用不被允许）
- **当前版本**：0.2.0。版本号在**三处**需同步改：`CMakeLists.txt` 的 `project(... VERSION ...)`、`packaging/PopTools.iss` 的 `#define MyAppVersion`、`src/app/MainWindow.cpp` 托盘「关于」对话框的硬编码文案（v0.2.0 起记入第三处）
- **技术栈**：
  - C++17，代码统一在 `poptools` 命名空间
  - Qt 6.8.3 LTS（LGPL 动态链接，本地免安装套件 `tools/qt/6.8.3/msvc2022_64`，不依赖全局 Qt）
  - CMake + MSVC（VS 2022，VS 2026 亦可，见坑 P-001）
  - OCR：PaddleOCR-json v1.4.1（QProcess 常驻子进程管道模式：stdin 发图片路径，stdout 读 JSON；初始化约 0.6s，之后约 200ms/张；其自带 opencv_world4100.dll 随 paddleocr/ 目录部署）
  - 翻译：大模型 API 双协议（设置 `translate/apiFormat`）——OpenAI 兼容 `/chat/completions` + Bearer（默认 DeepSeek，可换 Ollama 等）与 Anthropic `/v1/messages` + `x-api-key`（DeepSeek 兼容端点 / 官方 Claude）；`QNetworkAccessManager` 实现；请求显式关闭思考模式（`thinking` 字段，端点不支持时 400/422 降级重发），响应做 `<think>` 兜底清洗；提示词注入用户配置的翻译领域与专有名词术语表
  - 窗口/控件识别：Win32 窗口枚举（user32 + dwmapi）+ Windows UI Automation（UIA 控件树，系统组件无第三方依赖），实现见 `core/capture/SmartRectDetector`
  - 热键：Win32 `RegisterHotKey`（user32），经 `core/hotkey/GlobalHotKey` 封装（nativeEventFilter）
  - 设置持久化：QSettings 写 exe 同目录 `PopTools.ini`，单例 `core/settings/Settings`
- **架构概览**（分层 + 功能模块化：core 与 ui 各为静态库（ui 内按功能分库），依赖方向 ui → core；app 可执行承载组合根与功能模块）：
  - **功能模块**（`app/IFeatureModule` + `FeatureRegistry` + `AppServices`）：截图 CaptureModule / 贴图 PinModule / 划词翻译 SelectionTranslateModule 三个自包含模块（`app/modules/`），各自注册热键、贡献托盘菜单、管理自己的窗口/钩子/事件过滤器；MainWindow 是组合根（托盘 + 服务装配 + 模块注册 + 设置/关于/退出）。模块间互不 include，跨模块交互经 AppServices 能力槽（`captureActive`/`captureIntercept`/`lastCaptureRect`）。**拔插功能 = 组合根删一处 add() + CMake 删一个目标**
  - `app/MainWindow`（QObject，非窗体）＝应用控制器：系统托盘 + 模块装配
  - 截图主流程：`CaptureWindow`（全屏画布式编辑，内部按控制器拆分：SelectionController / AnnotationEditor / ToolDrawingController / TextEditingController / OcrController / OutputController）→ OutputController 产出到剪贴板 → PinWindow 贴图（构造注入应用级共享 `PaddleOcrService`，OCR 子进程全局唯一；OCR 面板/动效经 `OcrHostCallbacks` 窄接口注入 OcrController，CaptureContext 保持纯画布/选区共享态）
  - OCR 流程：OcrController → `PaddleOcrService`（子进程）→ 识别文本以文本框覆盖画布，或打开 `OcrPanel` 文字栏
  - 翻译流程：`Translator`（大模型 API，提示词=基础指令+领域/术语表；CaptureWindow 内嵌一例服务截图翻译，SelectionTranslateModule 内一例服务划词卡片，各自单消费者）→ OcrPanel（截图翻译）或 `TranslateCard`（划词翻译卡片，全局仅一张）；`SelectionTranslator` 负责模拟 Ctrl+C 抓取选中文本；目标语言在两处入口共享并持久化（`translate/targetLang`），切换即记忆；源语言不记忆，每次新任务重置为自动检测；语言显示名↔代码映射统一在 `core/translation/Languages.h`
  - 标注数据模型与渲染在 `core/annotation`（AnnotationCanvas / AnnotationItem / AnnotationPainter），UI 工具在 `ui/capture/tools`（ToolBase + ToolFactory + 各工具类）
- **目录含义**：
  - `src/main.cpp` — 程序入口
  - `src/app/` — 应用外壳（组合根）：MainWindow（托盘 + 模块装配）+ `IFeatureModule`/`FeatureRegistry`/`AppServices` 基础设施
  - `src/app/modules/` — 功能模块（capture 截图 / pin 贴图 / selectionTranslate 划词翻译），各自自包含
  - `src/core/` — 核心域静态库 `snipaste_core`（annotation 标注模型与渲染、capture 多屏捕获与窗口检测、hotkey 全局热键与全局鼠标钩子、ocr OCR 服务、settings 设置、translation 翻译与划词翻译）
  - `src/ui/` — 界面层按功能分库：`snipaste_ui_capture`（截图画布+工具栏+OCR 面板）/ `snipaste_ui_pin`（贴图窗口）/ `snipaste_ui_translate`（划词翻译卡片）/ `snipaste_ui_settings`（设置对话框），聚合为 INTERFACE 目标 `snipaste_ui`
  - `tests/` — QtTest 域逻辑单元测试（ctest 运行，`SNIPASTE_BUILD_TESTS` 默认 ON）
  - `src/resources/` — qrc 资源与图标：`app_icon.svg` 为应用图标源文件（「破框而出」设计，托盘/窗口图标直接引用，见 T-026）；`icons/` 为 24×24 工具条线性图标（svg）
  - `scripts/` — 第三方依赖一键下载脚本（install_qt.py / install_paddleocr.py），走本机代理 `127.0.0.1:7897`
  - `tools/` — 第三方绿色套件（qt / paddleocr），不入库，由脚本生成
  - `packaging/` — Inno Setup 6 安装包脚本 `PopTools.iss`（AppId 已随更名更换为全新 GUID），打包源为 `build/Release` 整目录（T-027 起排除 `poptools_domain_tests.exe`）；`PopTools.ico` 不再手工维护，由 `scripts/make_ico.py` 从 app_icon.svg 生成（Edge 渲染 512 主图 → Pillow 合成 7 帧，见 T-026）
  - `dist/` — 安装包输出（已忽略；现存更名后 `PopTools-Setup-0.2.0.exe` 与旧名 Snipaste 的 0.1.3 / 0.2.0 Setup）
  - `temp_scripts/` — 一次性脚本与中间产物（不入库）
  - `build/` — CMake 构建输出（已忽略；`build/Release/` 有完整可运行产物，含 Qt 运行库、paddleocr/ 引擎）
- **常用命令**：
  - 安装依赖（新机器必做，需代理 127.0.0.1:7897）：`python scripts/install_qt.py`、`python scripts/install_paddleocr.py`
  - 配置：`cmake -S . -B build -G "Visual Studio 18 2026" -A x64`（本机 VS 2026，见坑 P-013；换生成器前必须删除 build/）
  - 构建：`cmake --build build --config Release`（POST_BUILD 自动执行 windeployqt、剔除 D3D 着色器编译器三件、从 VS Redist 部署 VC 运行库、拷贝整个 paddleocr/ 目录，见 T-027）
  - 测试：`cd build && ctest -C Release --output-on-failure`
  - 运行：`build/Release/PopTools.exe`
  - 打包：`ISCC.exe packaging\PopTools.iss`（输出 `dist/PopTools-Setup-<版本>.exe`）
  - Python 环境：项目根 `.venv`（Python 3.12.14，uv 管理）
- **当前状态**：v0.2.0 已发布（旧名 Snipaste，tag v0.2.0）；2026-09-02 完成全局更名 PopTools + GPL-3.0 协议变更（T-024），远程仓库已改名 `https://gitee.com/richardogu/poptools`（remote 已更新并验证）；同日更换应用图标（T-026，含补上 exe 内嵌图标缺口）与安装包瘦身第一阶段（T-027，剔除 D3D 着色器编译器 + 捆绑 VC 运行库，111.0→105.7MB）；安装包 `dist/PopTools-Setup-0.2.0.exe` 为 T-026/T-027 后版本（**均未发布**——T-026/T-027 未提交、未打新 tag）；本地项目文件夹已改名为 `F:\poptools`；无 CI；OCR 引擎缺失时不阻断构建（运行时提示不可用）

## 历史任务

> 按任务组织（不是按对话），最新的在最上面。编号从 T-001 递增，永不复用。
> 注：本节由 git 提交历史（2026-08-06 ~ 2026-08-24，共 19 个提交）在交接文档首次创建时整理而成。

### T-027 安装包瘦身第一阶段：剔除 D3D 着色器编译器并捆绑 VC 运行库
- **完成日期**：2026-09-02
- **背景**：安装包 111MB 偏大。全量体积分析（2026-09-02）：打包源 `build/Release` 343MB = `paddleocr/` 297MB（mklml 89 / models 63 / opencv_world4100 62 / paddle_inference 49 / mkldnn 21 / onnxruntime 7.6 / paddle2onnx 3.8 / libiomp5md 1.7）+ Qt 运行库约 46MB（其中 dxcompiler 14.3 / D3Dcompiler_47 4.7 / dxil 1.5）+ 其余小项；安装包已 lzma2/ultra64 固实压缩，压缩参数无优化空间，只能砍文件；实测语言模型权重压缩率仅约 11%（几乎不压），保留 6 语言时模型压缩后约 50MB 即体积地板。**用户约束：OCR 多语言模型包保留不删**。
- **结果**：
  1. **剔除 D3D 着色器编译器**：dxcompiler/dxil/D3Dcompiler_47（20.5MB 原始、压缩后约 7MB）非静态依赖——dumpbin 证实 Qt6Gui 与 qwindows.dll 的导入表只有系统自带 d3d11/dxgi/d3d12，纯 Widgets 应用不加载（RHI/Qt Quick 的 D3D 后端专用）。POST_BUILD 在 windeployqt 之后 `cmake -E rm -f` 剔除。
  2. **捆绑 VC 运行库（顺带修隐性缺陷）**：部署目录根此前完全没有 msvcp140/vcruntime140 等（只在 paddleocr/ 子目录有一份），依赖用户机器碰巧装过 VC++ Redist，干净机器可能无法启动。windeployqt `--compiler-runtime` 实测无效（见 [P-019](#p-019-windeployqt-的-compiler-runtime-选项探测不到-vs-2026)），改为 CMake 从 `CMAKE_CXX_COMPILER` 路径回溯 VS 安装根、定位 `VC/Redist/MSVC/*/x64/Microsoft.VC*.CRT`（过滤 onecore、取排序最后）逐 DLL `copy_if_different` 部署（9 个 DLL，约 2.5MB 原始）。
  3. **排除测试程序**：`PopTools.iss` 加 `Excludes: "poptools_domain_tests.exe"`（此前整目录打包误把 141KB 单元测试带进安装包）。
  4. **mklml.dll（89MB）删除不可行**：虽有分析佐证（无静态导入、无延迟导入），但实证隔离测试失败（见 [P-018](#p-018-mklmldll-无静态引用却是-paddle-硬依赖)），已恢复原状。
  5. **验证**：重建 + ctest 1/1 + 启动冒烟（无 dx* 启动正常）通过；ISCC 重打 `dist/PopTools-Setup-0.2.0.exe`：**111.0MB → 105.7MB**（净省 5.3MB；理论 dx* 收益 7MB 被新增 CRT 约 1.2MB 抵掉一部分）。部署目录 343MB → 325MB。
- **后续可选项（未做，待用户决策）**：① opencv_world4100 最小化重建（62MB → 约 20-25MB 原始，安装包约再省 10MB 量级；需 dumpbin 核对 exe 导入符号 + 全语言 OCR 回归）；② OCR 引擎换 RapidOCR-json（onnxruntime）：Paddle 运行库栈 235MB 原始换 onnxruntime+opencv 约 70MB，收益最大（安装包估约 55-70MB，已贴近多语言模型地板），需核实多语言 onnx 模型可得性 + 适配 `PaddleOcrService`（同为管道 JSON 协议）+ 全语言回归。
- **关键改动**：`CMakeLists.txt`（windeployqt 参数注释、dx* 剔除、VC CRT 显式部署）、`packaging/PopTools.iss`（Excludes）
- **相关坑**：[P-018](#p-018-mklmldll-无静态引用却是-paddle-硬依赖)、[P-019](#p-019-windeployqt-的-compiler-runtime-选项探测不到-vs-2026)

### T-026 更换应用图标：「破框而出」新形象 + 补上 exe 内嵌图标缺口
- **完成日期**：2026-09-02
- **背景**：用户要求更换图标（原 `app_icon.svg` 为 Material 蓝色相机）。梳理发现图标共三条链路且原有一个缺口：托盘/窗口图标（qrc SVG）、安装包图标（`packaging/PopTools.ico`）、**exe 内嵌图标原本根本不存在**（全项目无 .rc 文件，资源管理器/任务栏里 exe 显示系统默认白框图标）。
- **结果**：
  1. **新图标设计**（AI 手写 SVG，概念「破框而出」）：蓝→紫渐变圆角方砖 + 白色截图取景框四角（呼应应用内 scan 图标的选区语言）+ 斜向弹出的白色面板（pop/贴图，板上两条淡蓝文字线代表 OCR/翻译）+ 琥珀色激活圆点。**严格限定 QtSvg 渲染子集**（仅 path/rect/circle/linearGradient/transform/opacity，无 filter/mask/clipPath），保证托盘端 Qt 渲染与 Edge 预览一致。
  2. **三链路全部换新**：① `src/resources/app_icon.svg` 直接替换（Qt 原生支持 SVG）；② `packaging/PopTools.ico` 由脚本重新生成——Edge headless（`--default-background-color=00000000` 透明底）渲染 512px 主图 → Pillow LANCZOS 合成 16/24/32/48/64/128/256 七帧 32bpp BMP（解析 ICO 目录逐帧校验过，361KB）；③ 新增 `src/app/app.rc`（`1 ICON`）挂入根 CMakeLists 的 qt_add_executable 源，exe 内嵌图标补缺。
  3. `src/main.cpp` 补 `QApplication::setWindowIcon(":/app_icon.svg")`（此前窗口/任务栏运行期图标未设置）。
  4. **验证**：多尺寸预览（512 主图缩放 16/24/32/48/64/256，明暗双底）人工检视通过，16px 下图形仍可辨；Release 构建、ctest、启动冒烟、ISCC 重打 `dist/PopTools-Setup-0.2.0.exe` 全部通过；exe 内嵌图标经 PowerShell `ExtractAssociatedIcon` 提取比对确认为新图标。
  - **待用户真机目检**：托盘图标的 QtSvg 实际渲染观感（预览管线是 Edge，Qt 为另一渲染器，特性子集已兼容但像素级观感可能有细微差异）。
- **关键改动**：`src/resources/app_icon.svg`、`packaging/PopTools.ico`、`src/app/app.rc`（新增）、`CMakeLists.txt`、`src/main.cpp`、`scripts/make_ico.py`（.ico 再生成管线，已沉淀入库）；设计期预览脚本（Edge 多尺寸渲染条）留在 `temp_scripts/` 默认保留
- **相关坑**：[P-017](#p-017-edge-headless-按尺寸逐个渲染小尺寸会随机挂起)

### T-025 更名后首打包：PopTools-Setup-0.2.0
- **完成日期**：2026-09-02
- **背景**：T-024 全局更名后尚未产出新安装包；且本地文件夹随后由 `F:\snipaste` 改名 `F:\poptools`，原 `build/` 的 CMake 缓存全部指向旧路径，不可增量构建（见 [P-015](#p-015-项目文件夹改名后-cmake-缓存指向旧路径)）。
- **结果**：
  1. 清空 `build/` 重新配置（VS 18 2026 生成器）→ Release 构建通过（windeployqt 与 paddleocr/ 部署正常，PaddleOCR-json.exe 位于 `build/Release/paddleocr/`）→ ctest 1/1 通过 → 启动冒烟通过（进程拉起确认后清理）。
  2. ISCC（`D:\Inno Setup 6\ISCC.exe`）编译 `packaging/PopTools.iss`，产出 `dist/PopTools-Setup-0.2.0.exe`（约 106MB；AppId 为 T-024 更换的新 GUID，安装器视作新产品）；dist/ 中旧名 Snipaste 的 0.1.3 / 0.2.0 安装包保留未动。
  3. **仅构建产物，未发布**：版本号三处保持 0.2.0 未动，未提交、未打 tag——是否升版本走完整发布流程由用户决定。
- **关键改动**：无源码/配置变更（仅 `docs/handover.md` 与 `dist/` 构建产物）
- **相关坑**：[P-015](#p-015-项目文件夹改名后-cmake-缓存指向旧路径)、[P-016](#p-016-git-bash-会把-taskkill-的斜杠参数误转换为路径)

### T-024 项目更名 PopTools、协议换 GPL-3.0、清理 OpenCV 遗留
- **完成日期**：2026-09-02
- **背景**：① 项目名 Snipaste 与同名商业截图产品（snipaste.com）撞名，且完全不体现「Windows 全局实用小工具箱」定位（后续会持续加入其他小工具）；② Apache-2.0 过于宽松允许商用，用户要求换强传染协议，选定 GPL-3.0；③ T-016 移除 OpenCV 后 `tools/opencv/` 本地遗留 1.1GB。
- **结果**：
  1. **全局更名（78 个跟踪文件 + 2 个改名）**：三档大小写机械替换（`SNIPASTE_*`→`POPTOOLS_*` / `Snipaste`→`PopTools` / `snipaste`→`poptools`），覆盖命名空间、CMake 目标（`poptools_core`/`poptools_ui_*`）、exe（`PopTools.exe`）、ini（`PopTools.ini`）、默认贴图目录（`tempPopTools`）、注册表自启键、默认保存文件名、托盘/关于文案、`SNIPASTE_SMART_BUDGET` 环境变量。批量脚本 `temp_scripts/script_20260902_rename.py`（BOM/CRLF/编码保留，git ls-files 圈定范围）。
  2. **安装包**：`packaging/Snipaste.iss`/`.ico` 经 `git mv` 更名 `PopTools.iss`/`PopTools.ico`，**AppId 更换为全新 GUID**（安装器视作新产品，不覆盖旧 Snipaste 安装）；输出名 `PopTools-Setup-<版本>.exe`。
  3. **README 重写**：PopTools 工具箱定位（去除 Snipaste 替代品话术），GPLv3 徽章，修正过期描述（ui/result 已删、OCR 扫描图标、模块化架构、VS 2026 生成器名）。
  4. **协议**：LICENSE 换为 GNU 官方 GPL-3.0 文本；README License 章节同步（衍生须同协议开源，闭源商用不允许）。
  5. **清理**：删除 `tools/opencv/`（1.1GB，零引用，不入库）。
  6. **验证**：清空 build 重新配置（VS 18 2026）→ Release 构建通过 → ctest 通过 → PopTools.exe 启动冒烟通过。
  - **遗留（已于同日收尾）**：Gitee 仓库已由用户网页改名 `richardogu/poptools`，本地 remote 已 `set-url` 并推送验证通过；本地文件夹 `F:\snipaste`→`F:\poptools` 仍待用户关闭会话后手动改（会话占用工作目录无法代改）。
- **关键改动**：全仓 78 文件、`packaging/PopTools.iss` + `PopTools.ico`（git mv + 新 AppId）、`LICENSE`、`README.md`、`docs/handover.md`；更名后设置文件更名为 PopTools.ini、贴图目录更名为 tempPopTools（旧用户设置与贴图目录不迁移，v0.2.0 用户量可忽略）
- **相关坑**：无

### T-023 v0.2.0 版本发布
- **完成日期**：2026-09-02
- **结果**：版本号三处同步升至 0.2.0（构建脚本 / 安装包脚本 / 关于对话框文案——第三处为本次补记）；清理重建（ctest 通过、启动冒烟通过）；ISCC 打包 `dist/Snipaste-Setup-0.2.0.exe`（约 110MB，0.1.3 安装包保留）；git 分三个逻辑提交（`refactor(app)` 模块化+ESC 单次退出 / `fix(capture)` 笔刷修复 / `chore(release)` 发版）推送 origin master，并打 tag `v0.2.0`。
- **本版内容**（自 0.1.3 以来）：翻译体验系列（语言记忆/关思考/领域术语/Anthropic 双协议/测试连接/卡片缩放与复制）、智能框选重写（UIA 控件树）、贴图无损 1:1 与高斯投影、OCR 原位拟合、涂鸦半透明与马赛克笔宽修复、ESC 单次退出、应用层模块化架构 + 域逻辑单元测试。
- **关键改动**：`CMakeLists.txt`、`packaging/Snipaste.iss`、`src/app/MainWindow.cpp`（版本文案）、`docs/handover.md`
- **相关坑**：[P-014](#p-014-会话-shell-工作目录占用-build-目录导致无法整目录删除)

### T-022 ESC 退出改为单次按压
- **完成日期**：2026-09-02
- **背景**：用户要求——截图界面在「编辑态 + 选择模式（无激活工具）」下需连按两次 ESC 才退出，且第一次会在画布中央弹出 2 秒的「再按 ESC 退出」提示框（原防误触设计），改为**按一次 ESC 立即退出**。
- **结果**：
  1. `keyPressEvent`：编辑态选择模式分支由「武装/两连按」改为直接 `cancel()`。其余 ESC 路径保持不变——文字输入态 ESC 仍是取消输入（不退出截图）、工具激活态 ESC 仍是退回选择模式（放弃草稿）、框选态原本就是单次退出。
  2. 拆除全部配套：画布中央「再按 ESC 退出」半透明提示框绘制（paintEvent）、`m_escArmed`/`m_escTimer` 成员、构造中的定时器装配、`startCapture`/`onToolSelected` 中的复位代码。
  - Release 构建通过、ctest 通过、启动冒烟通过；交互手感待用户复检。
- **关键改动**：`src/ui/capture/CaptureWindow.h`、`src/ui/capture/CaptureWindow.cpp`
- **相关坑**：无

### T-021 标注笔刷真机反馈修复：涂鸦半透明与马赛克笔宽
- **完成日期**：2026-09-02
- **背景**：T-020 真机回归中用户反馈两个标注笔刷问题：① 涂鸦笔字迹应为半透明（类似"划重点"荧光笔）；② 马赛克笔触太粗，应与涂鸦笔笔触大小类似。
- **原因**：① DoodleTool 与铅笔共用 BrushTool 默认渲染，颜色不透明；② `MosaicTool::strokeWidth` 为线宽 5 倍（下限 10px），而涂鸦笔为线宽 1 倍。
- **结果**：
  1. **涂鸦半透明**：`DoodleTool::draftColor()` 把 alpha≈45%（115）烘焙进标注色。渲染端 `AnnotationPainter::drawOne` 直接以 `it.color` 描线，无需改渲染器；拖动草稿预览与最终渲染（画布/复制/保存）天然一致。`drawPolyline` 为单次描边操作，同一笔迹内自交不会叠加变深；跨笔画重叠处像真实荧光笔一样加深。
  2. **马赛克笔宽**：`MosaicTool::strokeWidth` 从 `max(10, width×5)` 改为 `max(6, width)`——与涂鸦笔一致随线宽缩放，6px 下限保证小线宽下马赛克块仍可辨。`buildMosaicItem` 按传入宽度生成足迹 mask，无需其他改动。
  - Release 构建通过、ctest 通过、启动冒烟通过；笔刷观感待用户复检。
- **关键改动**：`src/ui/capture/tools/DoodleTool.h`、`src/ui/capture/tools/MosaicTool.h`
- **相关坑**：无

### T-020 架构模块化改造：功能模块可拔插
- **完成日期**：2026-09-02
- **背景**：架构评审（同日）发现——ResultDialog 死路径仍被供养并可能拉起第二个 PaddleOCR 子进程；`m_selTranslateActive` 布尔旗标路由翻译结果；MainWindow 集中耦合全部功能的具体类型；`CaptureContext` 混入 OCR/翻译功能回调正在滑向 Service Locator；ui 单体静态库；零测试。
- **结果**：
  1. **P0 还债**：删除 `ui/result/`（ResultDialog 死路径，全代码无 show/exec 入口）及 MainWindow 为它供养的第二个 `PaddleOcrService`/`Translator`——OCR 引擎回到全局唯一；翻译服务改为"单服务单消费者"，`m_selTranslateFinished` 中以"卡片可见与否"判定丢弃在途结果，布尔旗标 `m_selTranslateActive` 移除；死成员 `m_lastPixmap` 移除（F3 贴图取剪贴板，`m_lastRect` 保留定位用）。
  2. **P1 模块化**：新增 `src/app/IFeatureModule.h`（id / init / shutdown 接口）、`FeatureRegistry.h`（按注册顺序装配 + 按 id 查找）、`AppServices.{h,cpp}`（QObject 服务容器：共享服务 hotkey/ocr/trayMenu + 跨模块能力槽 `captureActive`/`captureIntercept`/`lastCaptureRect` + 贡献点 `addTrayAction`/`registerHotKey`——热键 id 由 AppServices 内部分配并自动路由）。**截图 / 贴图 / 划词翻译改为独立功能模块**（`src/app/modules/`），各自收编自己的热键、托盘贡献、全局鼠标钩子与应用级事件过滤器；`MainWindow` 退化为组合根（托盘 + 服务装配 + 三行模块注册 + 设置/关于/退出）。`CaptureWindow` 构造注入共享 OCR 服务（null 时内部自建兜底）；`OcrController` 因共享服务寿命长于自己而改为 QObject（connect 以 this 为 context，析构自动断连）。
  3. **P2 接缝治理**：`CaptureContext` 剥离 OCR 专属回调（showOcrPanel/translateRequested/setPanelBusy/ocrBusy）→ 新增 `OcrHostCallbacks` 窄接口由 CaptureWindow 注入 OcrController，通用上下文回归纯画布/选区共享态。**Settings 变更通知经评估不做**：SettingsDialog 无热键配置项，通知无消费方，避免投机设计；Translator 直读 `Settings::instance()` 的现状保留，记录为后续可选项（连同版本号两处同步、core 内部 platform/domain 子分层）。
  4. **P3 基建**：ui 层按功能拆四个静态库（`snipaste_ui_capture/pin/translate/settings`，互不链接）+ INTERFACE 聚合目标 `snipaste_ui`（app 链接方式不变）；新增 `tests/`（QtTest）：标注画板撤销/重做/交互修改/清除、语言映射往返、API 格式与端点辅助、FeatureRegistry 装配顺序与查找，`ctest -C Release` 1/1 通过（`SNIPASTE_BUILD_TESTS` 默认 ON）。
  5. **架构纪律复检**：core→ui/app 零反向依赖；ui 四功能目录零互引；每个 app 模块只 include 自己的功能目录。此后拔插一个功能 = 组合根删一处 `add()` + CMake 删一个目标。
  - Release 构建通过、ctest 通过、应用启动冒烟通过；**截图/贴图/划词翻译/OCR 的真机交互回归待用户目检**。
- **关键改动**：`src/app/`（MainWindow 重写为组合根，新增 AppServices/IFeatureModule/FeatureRegistry/modules 三模块）、`src/ui/CMakeLists.txt`（功能拆库）、`src/ui/capture/CaptureWindow.*`（OCR 注入 + host 回调）、`src/ui/capture/OcrController.*`（QObject 化 + OcrHostCallbacks）、`src/ui/capture/CaptureContext.h`（去功能化）、`src/ui/result/`（删除）、`tests/`（新增）、`CMakeLists.txt`（测试开关）
- **相关坑**：[P-013](#p-013-本机-vs-2026-生成器名与失败配置的残留缓存)

### T-019 OCR 覆盖文字原位拟合修复
- **完成日期**：2026-09-02
- **背景**：OCR 识别文字本身正确，但覆盖到截图上的文字明显偏小（宽高同比例缩到约 7 折），没有"原位识别"的覆盖效果。
- **原因**：`OcrController::fitFontHeight` 用 `QFontMetricsF::height()`（行高 = ascent+descent）去匹配 OCR 框高度，但字体行高远大于字形实际墨迹——微软雅黑 em=100 时行高 133px、中文字形墨迹仅约 97px，按行高拟合后实际渲染字形只有框的约 73%。
- **结果**：
  1. **墨迹紧贴拟合**：`fitFontHeight` 改用 `tightBoundingRect(text)` 实测文字墨迹高度拟合框高（先按墨迹∝pixelSize 一次比例修正，再 ±1px 精调）。
  2. **渲染端精确覆盖**：`AnnotationItem` 新增 `inPlaceFit` 标记（仅 OCR 项设置，不影响用户文字工具）；`AnnotationPainter` 纯文本分支遇该标记时，用 `tightBoundingRect` 把文字墨迹矩形映射到 OCR 框。**第二轮反馈（横向拉伸感）后改进**：纵向精确匹配；横向优先调字距（每字符间隙分摊宽度差，钳制在 [-0.1em, 0.35em]），残余差异再以 ±10% 内的横向缩放兜底，字形基本无形变。
  3. **引擎坐标验证**：用 PIL 生成已知字号测试图直跑 PaddleOCR-json（`temp_scripts/script_ocr_box_scale.py`），确认引擎返回框坐标与真实文字墨迹一致（24px 字→26px 框），排除引擎内部缩图嫌疑。
  4. **顺带修复两个交互 BUG（用户反馈）**：① 右键不再无条件退出截图——改为与左键一致，仅点击选区外退出（Selecting 态无选区，右键任意处退出；Editing 态右键选区内忽略）；② 预选高亮闪回——用户在预高亮框外按下拖动手动框选时，在途的智能检测结果回包会把高亮重新画出来（界面双框），现按下预高亮框外即清除高亮并递增 `m_smartGen` 作废在途结果。
  - Release 构建通过、应用启动冒烟通过；原位覆盖观感与交互修复待用户真机目检。
- **关键改动**：`src/core/annotation/AnnotationItem.h`（inPlaceFit 字段）、`src/core/annotation/AnnotationPainter.cpp`（原位仿射渲染）、`src/ui/capture/OcrController.*`（墨迹拟合）
- **相关坑**：无

### T-018 贴图无损 1:1 与真实投影重做
- **完成日期**：2026-09-02
- **背景**：三项问题——① 截图/保存/贴图全链路需确保无损画质（保存对话框曾提供 JPEG 有损选项）；② 全屏截图后 F3 贴图比全屏小一圈（`PinWindow::pin` 把初始尺寸限制在屏幕可用区的 80%）；③ 贴图阴影是两条手绘线性渐变（左/下各一条三阶渐变），观感生硬假造，用户要求实现自然的"浮于屏幕之上"。用户目检确认新阴影效果清晰自然。
- **结果**：
  1. **无损审计**：捕获（grabWindow 物理像素 1:1 拼接）→ 选区裁剪 `render()`（copy 1:1 + 标注按物理分辨率矢量重绘）→ 剪贴板（CF_DIB 无压缩位图）/ 落盘（PNG）本就全程无损；唯一有损点是手动保存对话框的 JPEG 选项 → 移除，只留 PNG（无损），并补默认文件名 `Snipaste_时间戳.png`。
  2. **贴图 1:1**：删除 pin() 的 80% 缩放限制，超大图也按原始尺寸/位置张贴（可滚轮缩小）；`m_pixmap` 记录张贴屏幕的 dpr，scale==1 时 `drawPixmap` 走物理像素 1:1 blit（缩放时才启用平滑变换）。
  3. **真实投影**：弃用线性渐变，改为「内容矩形整体下移 6px 后的轮廓做高斯模糊」（3 次盒模糊近似，σ≈12 逻辑像素，峰值不透明度约 40%）——与操作系统原生窗口投影同源的视觉语言（Windows UWP DropShadow 默认即约 9px 高斯 + 向下偏移）。利用盒模糊可分离性，只对两条一维剖面卷积再外积合成 Alpha8 图（免整幅大图多遍卷积），张贴/滚轮缩放时重算无卡顿；窗口四周留 32px 透明边距容纳衰减，内容左上角仍精确对齐传入的全局位置。
  4. **真机像素级验证**（`temp_scripts/script_pin_verify.py`，PIL ImageGrab + numpy）：区域贴图矩形内 vs 落盘 PNG、vs 贴图前原屏均逐字节一致（diff=0），矩形外仅阴影带变暗；全屏贴图精确 1920×1080 覆盖整屏（旧代码会缩到 80%）、矩形内 diff=0、贴图前后整屏无可见差异（"看不出是贴图"达成）。
- **关键改动**：`src/ui/pin/PinWindow.*`（重写）、`src/ui/capture/OutputController.cpp`（保存仅 PNG 无损）
- **相关坑**：[P-012](#p-012-贴图像素级自动验证的三个坑)

### T-017 标注工具栏可维护性重构：调色盘移第二行 + OCR 换扫描图标
- **完成日期**：2026-09-02
- **背景**：工具栏（ToolBar）评审发现可维护性差——跨两行 QGridLayout 手写列号 0~18（`i >= 8 ? i + 3 : i` 补位编排、跨行分隔线），增删按钮需全盘重排；工具图标/枚举/tooltip 散落三处 switch；程序性刷新回射用户信号。另有两处体验问题：调色盘占最右侧且跨两行；OCR 按钮用文字「文」易误解为文字输入工具。用户确认修复生效。
- **结果**：
  1. **布局重构**：跨两行 QGridLayout 改为每行一个 QHBoxLayout + QVBoxLayout 收口，增删按钮只影响所在行；重复的分隔线代码收敛为 `makeSeparator()`；删除头文件中声明但从未实现的 `makeToolButton`/`makeActionButton` 死声明。
  2. **元数据表单一数据源**：新增文件级 `kTools[]`（tool/icon/tip）+ `findTool()`，按钮创建、tooltip、`setTool()` 图标刷新三处共用，新增工具只改一处。
  3. **信号语义修正**：`applyWidthOptions()` 程序性刷新（`setTool`/`setWidth`/构造期间）不再发射 `widthSelected`（原先每次切工具都触发宿主 `m_textEditCtrl->commit()`，构造期间也向空发射）；仅用户点击线宽档位时发射。切工具后的线宽值同步改为 `CaptureWindow::onToolSelected` 主动读 `toolbar->currentWidth()`（新增取值器；注意不能命名为 `width()`，会遮蔽 `QWidget::width()` 导致 `positionToolbar` 等调用静默变义）。
  4. **字号提交去重**：activated + returnPressed + editingFinished 三路连接在回车时双发 `fontSizeSelected`，删去 returnPressed（editingFinished 已覆盖回车与失焦两条路径）。
  5. **调色盘移到第二行**：16 色 2×8 色板（色块 18px→12px，2×12+行距 2=26 恰为行高）+ 22px 当前色大色块置于第二行尾部（行尾 addStretch 防间距均摊），工具栏总宽仍由第一行决定、整体不增宽；色板按钮补 `setCheckable(true)` 使"当前色蓝框描边"真正生效（原先 `setChecked` 在非 checkable 按钮上是无效调用，选中态从未显示过）。
  6. **OCR 按钮图标化**：新增 `resources/icons/scan.svg`（四角取景框 + 扫描线，24×24 单路径与现有图标同风格），替代文字「文」，消除"文字输入工具"歧义；删除其手写样式表、走全局 QToolButton 样式。【译】为语义文字按钮，保留文字形态。
  7. **图标高 DPI 适配**：`svgIcon`/`widthIcon`/`fillIcon` 按屏幕 `devicePixelRatio` 渲染位图（原固定 20 逻辑像素，高分屏被放大发虚）。
  - Release 构建通过；用户真机验证确认三项改动生效。
- **关键改动**：`src/ui/capture/ToolBar.*`（重构主体）、`src/ui/capture/CaptureWindow.cpp`（onToolSelected 同步线宽）、`src/resources/icons/scan.svg`（新增）、`src/resources/resources.qrc`
- **相关坑**：无（构建时再次遇到运行实例锁产物，见 [P-008](#p-008-运行中的实例会锁住-buildrelease-产物导致构建失败)）

### T-016 智能框选重写：窗口枚举 + UIA 控件树（移除 OpenCV 图像识别）
- **完成日期**：2026-09-02
- **背景**：原实现（顶层窗口枚举 + eSearch 图像边缘识别移植）只能识别应用级矩形；图像识别产生的"连通域外接矩形"恰会选中文字笔画闭合区域（字母环），只能默认关闭，页面级预选能力为零。经审核后整体废弃重写。
- **结果**：
  1. **点查询模型**：废弃"一次构建候选区 + 最小面积包含"模型，改为随鼠标移动按点查询"该点下的元素矩形"（`core/capture/SmartRectDetector`，常驻工作线程 COM STA + IUIAutomation，latest-wins 队列）。
  2. **三层来源取最小包含矩形**：① 顶层窗口（EnumWindows 按 Z 序几何命中 + DWM 扩展边界 `DWMWA_EXTENDED_FRAME_BOUNDS`，过滤 cloaked/点击穿透/系统窗口）；② 原生子控件（EnumChildWindows 最小可见包含窗口）；③ UIA 控件树（从顶层窗口元素沿 ControlViewWalker 下行，仅走包含点的分支，CacheRequest 批量取子元素矩形，32 层/300ms 预算）——第③层覆盖浏览器/Electron/WPF/Qt 等无子 HWND 的现代界面元素（输入框/按钮/卡片）。
  3. **关键约束**：截图全屏覆盖层会挡住一切命中测试 API（WindowFromPoint / UIA ElementFromPoint 只会命中覆盖层自身），因此全程只用"枚举 + 几何包含"判定，UIA 从目标窗口句柄走树而非按桌面点查；UIA 元素最小粒度为控件/文本行，结构上不会选中文字笔画闭合区域。
  4. **顺带修复的确定性缺陷**：不再高亮挂起 UWP/其它虚拟桌面的不可见窗口（cloaked 过滤）；高亮框不再比可见窗口大一圈（扩展边界）；混合 DPI 多屏跨屏窗口矩形错位（按查询点所在屏 dpr 换算，与 ScreenCapturer 物理映射一致）；挂起窗口跳过 UIA（`IsHungAppWindow`）防工作线程被阻塞。
  5. **移除 OpenCV**：主程序不再依赖 OpenCV（删除 `WindowDetector.*`、`scripts/install_opencv.py`、`tools/opencv/`）；paddleocr 子进程自带 opencv_world4100.dll 随目录整体拷贝（不再与根目录硬链接）；设置项 `smart/imageRecognition` 与设置界面「框选图像识别」勾选框删除。
  6. 交互不变：悬停预高亮（橙色虚线）、按下点选、按住拖动超阈值转手动框选。
  7. **Chromium 系适配（真机反馈后三轮修复）**：① 批量取子元素（FindAllBuildCache）在 Chromium/Qt Provider 上返回空 → 回退 TreeWalker 兄弟链遍历；② Chromium 顶层窗口下同尺寸的 `Intermediate D3D Window`（无子节点渲染表面）会截胡下行 → 改带回溯的 DFS（死胡同分支回退试兄弟，见坑 P-011）；③ 按窗口缓存发一次 `WM_GETOBJECT(OBJID_CLIENT)` 激活渲染无障碍树（见坑 P-011）；④ 兄弟链遍历遇首个包含点的孩子立即下行（扫全兄弟会耗尽预算）+ 暖启动缓存（鼠标仍在上次命中元素内则从该元素继续下行，深页面二次查询近 0ms）+ 批量不可用标记按窗口持久化 + 高亮裁剪到窗口可见范围。修复后 Edge / Electron / WebView2 应用的按钮、输入框、卡片均可控件级预选，ZCode 深页面实测命中 37×10px 的小元素。
  8. **稳定性修复（第一批真机验证暴露的崩溃，见坑 P-011）**：兄弟链回退空 vector 取 `back()` 越界、无效哨兵矩形"负×负面积为正"覆盖合法结果、暖启动引用计数转移契约错误（use-after-free 闪退）、工作线程 detect 加异常兜底。
  - Release 构建通过；真机验证基本成功（记事本/资源管理器优秀，Edge/ZCode 控件级预选生效）。
- **关键改动**：`src/core/capture/SmartRectDetector.*`（新增）、`src/core/capture/WindowDetector.*`（删除）、`src/ui/capture/CaptureWindow.*`（智能框选接入重写）、`src/core/CMakeLists.txt`、`CMakeLists.txt`（OpenCV 剥离 + Concurrent 移除）、`src/core/settings/Settings.*`、`src/ui/settings/SettingsDialog.*`、`packaging/Snipaste.iss`、`README.md`
- **相关坑**：无

### T-015 划词卡片单例化与点击外部关闭
- **完成日期**：2026-09-02
- **结果**：
  1. **卡片全局唯一（单例复用）**：去掉 `WA_DeleteOnClose`，`MainWindow::ensureSelectionCard()` 首次创建后一直复用，信号只连接一次。顺带修复旧缺陷：`onTranslateFinished` 只清 QPointer 未销毁卡片，旧卡片永久残留且失去跟踪，新划词再 new 一张导致多卡并存、点关闭前永不消失。
  2. **点击卡片外部即隐藏**：分两条互补链路——(a) 本应用内点击：应用级事件过滤器（`qApp->installEventFilter`）监听 `MouseButtonPress`，落点在卡片几何外即隐藏；用 `isAncestorOf` 豁免卡片自身的下拉弹层（语言下拉弹出窗是卡片控件的子级顶层窗口，不算"外部"）。(b) 其他应用的窗口：Qt 收不到这类事件（见坑 P-010），新增 `GlobalMouseHook`（WH_MOUSE_LL 低级鼠标钩子）感知，`WindowFromPoint` 查落点窗口、`GetWindowThreadProcessId` 判归属，属于其他进程即隐藏。钩子不吞事件，用户点击仍正常作用于目标程序；仅在卡片显示期间安装，隐藏即卸载。
  3. **隐藏即清空**：`TranslateCard::hideEvent` 统一清空原文/译文/错误样式并复位按钮文案与可用态，覆盖点外部/关闭按钮/Esc 全部退出路径；语言选择与用户拖拽过的尺寸作为记忆项保留（单例化后跨次使用仍记忆）。
  4. **再次划词流程**：卡片显示中则先 `hide()`（同步触发清空）→ 填新原文 → 显示在鼠标位置 → 立即翻译。
  5. **连带修复结果误投**：翻译在途时用户关掉卡片，返回结果不再误入旧结果面板路径（失败会弹模态报错框）；改用 `m_selTranslateActive` 在途标记路由，卡片已隐藏则静默丢弃结果。原 `m_selSourceText` 成员删除。
  - Release 构建通过，应用启动冒烟测试通过；点击外部的交互体验待用户日常使用验证。
- **关键改动**：`src/core/hotkey/GlobalMouseHook.*`（新增）、`src/core/CMakeLists.txt`、`src/ui/translate/TranslateCard.*`（单例化 + hideEvent 清空）、`src/app/MainWindow.*`（单例管理 + 事件过滤器 + 鼠标钩子接入 + 在途标记路由）
- **相关坑**：[P-010](#p-010-不抢焦点的浮窗收不到本应用外的鼠标事件)

### T-014 Translator 支持 Anthropic 接口格式（含配置项）
- **完成日期**：2026-09-01
- **结果**：
  1. **双协议支持**：Translator 与「测试连接」（ApiTester）均支持两种接口格式，设置对话框「翻译 API」区新增「接口格式」下拉（OpenAI 兼容 / Anthropic，存 `translate/apiFormat`，默认 openai）。格式相关端点/鉴权收敛为 Translator.h 内联辅助（`apiEndpointPath` / `applyApiAuthHeaders`），两处共用。
  2. **Anthropic 分支**：端点 `{base}/v1/messages`（base 已以 `/v1` 结尾则不再重复追加）；鉴权 `x-api-key` + `anthropic-version: 2023-06-01`；请求体 system 独立于 messages、`max_tokens: 8192` 必填；响应解析 `content[]` 块数组，仅拼接 `type=="text"` 块（thinking 块自动忽略）。错误响应 `error.message` 结构与 OpenAI 一致，报错中文化逻辑直接复用。
  3. **关思考字段两种格式通用**：DeepSeek 的 Anthropic 兼容端点实测默认开思考（content 首块为 thinking 块，白耗 token），带 `thinking: {"type":"disabled"}` 实测生效直出；该字段两种格式均随请求携带，端点不识别时 400/422 自动去字段降级重发（原 OpenAI 专属逻辑放宽为通用，覆盖官方 Claude 可能严格校验的情况）。「测试连接」的 Anthropic 探测不带该字段（只验证连通/鉴权/模型名）。
  4. **真机验证**（DeepSeek `deepseek-v4-flash`，同 Key）：`/anthropic/v1/messages` 无字段 → 200 但 16 token 全为 thinking 块；带 `disabled` → 200 纯 text 块 4 token 直出。OpenAI 路径行为不变。
- **关键改动**：`src/core/translation/Translator.*`（ApiFormat 枚举 + 双协议请求/解析）、`src/core/translation/ApiTester.*`（按格式构造探测请求）、`src/core/settings/Settings.*`（apiFormat 配置）、`src/ui/settings/SettingsDialog.*`（接口格式下拉 + Base URL 占位联动）、`README.md`
- **相关坑**：[P-009](#p-009-deepseek-anthropic-兼容端点默认开思考且挂在-anthropic-路径前缀下)

### T-013 翻译界面拖拽调整大小与一键复制
- **完成日期**：2026-09-01
- **结果**：
  1. **划词卡片支持拖拽调整大小**：文本区由 QLabel 改为只读 QTextEdit（长文内部滚动，保证任意长度内容都可见）；卡片四边 6px 内按住拖拽即可调整大小（带方向光标反馈），窗口最小 320×180。内容更新时仍按内容自适应高度（原文上限 200px、译文上限 340px，整体不超过屏幕可用高度 85%）；用户手动调整过后则尊重用户尺寸，不再自动变化（拖空白处移动卡片的行为保留）。
  2. **翻译结果一键复制**：划词卡片译文右下方与 OCR 文字栏翻译结果下方各加「复制译文」按钮，点击写入剪贴板并反馈"已复制"（1.2s 后恢复）；仅在有可复制内容时可用（翻译中/出错/清空后禁用，避免复制到"翻译中…"占位文本）。
  - Release 构建通过，应用启动冒烟测试通过；拖拽与复制的交互体验待用户实际验证。
- **关键改动**：`src/ui/translate/TranslateCard.*`（QTextEdit 化 + 边缘缩放 + 复制按钮）、`src/ui/capture/OcrPanel.*`（复制按钮）、`src/ui/capture/CaptureWindow.cpp`（setTranslateText/setBusy 调用顺序对调，见上）、`README.md`
- **相关坑**：[P-008](#p-008-运行中的实例会锁住-buildrelease-产物导致构建失败)

### T-012 认证报错诊断与「测试连接」功能
- **完成日期**：2026-09-01
- **结果**：
  1. **诊断 "Host requires authentication"**：该文案是 Qt 对 HTTP 401 的统一映射，不透出服务端原因。真机排查（curl -i + 读 exe 旁 Snipaste.ini）定位根因：配置中保存的 Key 与用户以为的不同（API Key 输入框为密码模式，粘贴成了旧 Key，服务端确认 `****7ede` invalid）；Base URL 与 Model 实际保存值均正确。模型名拼错时服务端返回的是 400 而非 401（错误信息含支持的模型名列表）。
  2. **设置对话框新增「测试连接」按钮**：core 新增 `ApiTester`，用表单当前值（未保存也能测）发一次与 Translator 同形态的最小请求（含 `thinking: disabled`），10 秒超时；结果就地显示——成功 / 401 认证失败 / 400/422 透出服务端 `error.message`（如"模型不存在"）/ 连接超时。
  3. **API Key 输入框增加「显示/隐藏」切换**（QToolButton），防密码框粘贴错无法察觉。
  4. **Translator 报错中文化**：401/403 → "API 认证失败（401）：API Key 无效或未授权…"；400/422 → "请求被拒绝：+服务端 message"；其余带状态码透出。新增 `normalizeApiBaseUrl` 容错 Base URL 尾部斜杠。
- **关键改动**：`src/core/translation/ApiTester.*`（新增）、`src/core/translation/Translator.*`、`src/ui/settings/SettingsDialog.*`、`src/core/CMakeLists.txt`、`README.md`
- **遗留问题**：强制结束 Snipaste.exe 后 PaddleOCR-json.exe 子进程会残留（占锁导致重新构建时 copy_directory 失败，开发时需先 taskkill 该进程）；Anthropic 格式支持仍待定（见「当前任务」）
- **相关坑**：[P-007](#p-007-qt-把-http-401-一律报为-host-requires-authentication)

### T-011 翻译体验三项改进（语言记忆 / 关闭思考 / 领域术语）
- **完成日期**：2026-09-01
- **结果**：
  1. **目标语言记忆**：在划词翻译卡片或 OCR 文字栏切换目标语言后立即持久化（`translate/targetLang`），下次弹出/自动翻译直接使用记忆值，不再回到默认中文。两处入口共用同一设置；源语言不做记忆，每次新任务（新卡片弹出 / OcrPanel 打开）都重置为"自动检测"，交由模型自动识别（用户确认：AI 自动检测足够鲁棒）。
  2. **关闭模型思考**：翻译请求体显式携带 `thinking: {"type": "disabled"}`。注意：DeepSeek V4 实测 `thinking.type` 合法值为 `adaptive`/`enabled`/`disabled`（早期网传的 `none` 会被 400 拒绝，见 [P-006](#p-006-deepseek-thinkingtype-合法值不含-none)）；端点不认识该字段返回 400/422 时自动去掉字段降级重发一次。响应中混入正文的 `<think>…</think>` 思维链兜底清洗。提示词同时禁止输出推理过程。翻译文本从 system 消息挪到 user 消息。**2026-09-01 真机实测**：`deepseek-v4-flash` 默认开思考（返回带 `reasoning_content`），带 `disabled` 字段后无 reasoning、3 token 直出。
  3. **领域与术语表**：设置对话框新增「翻译领域与术语」区——领域（逗号/顿号分隔）与术语对（每行 `原文=译文`），存为 `translate/domains` 与 `translate/termSources`+`translate/termTargets`（平行列表避免分隔符冲突），翻译时注入系统提示词（领域指令 + 术语映射表）。
  - 构建通过（Release），实际运行验证待用户进行。
- **关键改动**：`src/core/translation/Languages.h`（新增：语言显示名↔代码共享映射，替代卡片/面板中三处重复实现）、`src/core/settings/Settings.*`（目标语言/领域/术语持久化）、`src/core/translation/Translator.*`（禁思考+降级重试+think 清洗+领域术语提示词）、`src/ui/translate/TranslateCard.*` 与 `src/ui/capture/OcrPanel.*`（目标语言记忆、共享映射）、`src/ui/settings/SettingsDialog.*`（领域与术语编辑区）、`src/app/MainWindow.cpp`（旧结果面板路径改用记忆语言）、`README.md`
- **遗留问题**：划词卡片 ⇄ 交换按钮在源语言为"自动"时行为退化（源=自动、目标=中文 时交换得到 中文→中文），系历史实现（目标下拉无"自动"项），本次未改动
- **相关坑**：[P-005](#p-005-openai-兼容端点对请求体未知字段校验不一)

### T-010 划词翻译重构与 v0.1.3 发布
- **完成日期**：2026-08-24
- **结果**：重构划词翻译逻辑并优化体验（全局仅保留一张翻译卡片，结果异步回填）；版本升至 0.1.3，产出 `dist/Snipaste-Setup-0.1.3.exe`
- **关键改动**：`src/core/translation/SelectionTranslator.*`、`src/ui/translate/TranslateCard.*`、`src/app/MainWindow.*`、`CMakeLists.txt`、`packaging/Snipaste.iss`
- **相关坑**：[P-004](#p-004-划词翻译读到剪贴板旧内容)

### T-009 项目重命名为 Snipaste 并补齐全套标注工具
- **完成日期**：2026-08-24
- **结果**：项目由 MySnipaste 更名 Snipaste；标注工具补齐（矩形/椭圆/直线/箭头/铅笔/涂鸦/马赛克/橡皮/文字 + 线宽/填充/16 色板）
- **关键改动**：`README.md`、`CMakeLists.txt`、`packaging/Snipaste.iss`、`src/ui/capture/tools/*`、`src/resources/icons/*`
- **相关坑**：无

### T-008 OCR 引擎替换为 PaddleOCR
- **完成日期**：2026-08-23
- **结果**：移除 Tesseract 依赖，改用 PaddleOCR-json v1.4.1 子进程管道模式（常驻进程）；新增 `scripts/install_paddleocr.py` 一键下载（附简中/繁中/英/日/韩/俄模型）
- **关键改动**：`src/core/ocr/PaddleOcrService.*`、`scripts/install_paddleocr.py`、`CMakeLists.txt`（引擎部署与 DLL 硬链接）、`tools/paddleocr/`
- **相关坑**：[P-003](#p-003-paddleocr-子进程找不到-opencv_world-dll)

### T-007 划词翻译功能上线
- **完成日期**：2026-08-23
- **结果**：新增全局热键 Ctrl+Alt+Shift+X 触发划词翻译：模拟 Ctrl+C 抓取选中文本 → 大模型翻译 → 浮动翻译卡片，支持语言切换
- **关键改动**：`src/core/translation/SelectionTranslator.*`（新增）、`src/ui/translate/TranslateCard.*`（新增）、`src/app/MainWindow.*`、`core/settings`（划词热键配置）
- **相关坑**：无

### T-006 智能框选图像识别与 OpenCV 接入
- **完成日期**：2026-08-23
- **结果**：移植 eSearch「框选.自动框选.图像识别」算法，截图时除系统窗口检测外支持图像边缘检测辅助框选（设置可开关，默认关以避免文字笔画噪声）；新增 `scripts/install_opencv.py`
- **关键改动**：`src/core/capture/WindowDetector.*`、`scripts/install_opencv.py`、`CMakeLists.txt`（OpenCV 接入）、`core/settings`（smartImageRecognition）
- **相关坑**：[P-001](#p-001-opencv-官方预编译包不识别新版-msvc)

### T-005 依赖纳管脚本与 OCR 进程内化
- **完成日期**：2026-08-13
- **结果**：确立「第三方套件脚本下载到 tools/、不依赖全局安装」模式，新增 Qt 离线安装脚本；OCR 从外部程序改为进程内 DLL 调用（该形态后被 T-008 的子进程模式取代）
- **关键改动**：`scripts/install_qt.py`、`CMakeLists.txt`（QT_ROOT 本地套件优先级机制：环境变量 > CMake 参数 > tools/qt）
- **相关坑**：[P-002](#p-002-windeployqt-报-developer-build-警告)

### T-004 贴图 OCR 面板与安装包打包
- **完成日期**：2026-08-11
- **结果**：F3 贴图完整可用（拖动/滚轮缩放 0.2x~5x/右键菜单/双击关闭/自动落盘 exe 目录 tempSnipaste/）；OCR 文字栏面板；窗口智能框选（系统窗口枚举）；开机自启（HKCU Run 注册表项）；Inno Setup 打包支持
- **关键改动**：`src/ui/pin/PinWindow.*`、`src/ui/capture/OcrPanel.*` 与各控制器拆分、`src/ui/settings/SettingsDialog.*`、`core/settings`（pinSaveDir/autoStart）、`packaging/Snipaste.iss`
- **相关坑**：无

### T-003 分层架构重构
- **完成日期**：2026-08-07
- **结果**：重构为 core / ui 两个静态库 + app 可执行的分层结构，引入 `snipaste` 命名空间，core 与 UI 解耦
- **关键改动**：`src/core/CMakeLists.txt`、`src/ui/CMakeLists.txt`（新增）
- **相关坑**：无

### T-002 截图画布式编辑与标注工具体系
- **完成日期**：2026-08-07
- **结果**：截图交互从「框选即完成」改为画布式编辑（松开鼠标进入编辑态、显式操作才退出）；标注工具体系落地；文字工具改为画布内嵌输入框（PPT 风格）；标注支持选中/拖动/缩放
- **关键改动**：`src/core/annotation/*`、`src/ui/capture/AnnotationEditor.*`、`ToolBar.*`
- **相关坑**：无

### T-001 项目初始化与截图 MVP
- **完成日期**：2026-08-06
- **结果**：可运行 MVP——F1 区域截图（多屏拼接 + DPI 映射）、Tesseract OCR（中英文）、OpenAI 兼容 API 翻译（可配置）、系统托盘 + 设置面板；Qt 6.8.3 + CMake + MSVC 构建
- **关键改动**：初始提交全量
- **相关坑**：无

## 当前任务

<!-- 当前任务完成后：整理为历史任务条目移入上方，然后恢复此占位 -->

（暂无）

> 备注：安装包瘦身第一阶段已结算为 [T-027](#t-027-安装包瘦身第一阶段剔除-d3d-着色器编译器并捆绑-vc-运行库)，其中记录了两个未做的后续可选项（opencv 最小化重建 / 换 RapidOCR-json 引擎），若继续瘦身以该条目为起点。

## 踩坑记录

> 每条坑必须链接到产生它的历史任务条目。

### P-018 mklml.dll 无静态引用却是 Paddle 硬依赖
- **关联任务**：[T-027](#t-027-安装包瘦身第一阶段剔除-d3d-着色器编译器并捆绑-vc-运行库)
- **现象**：`paddleocr/mklml.dll`（89MB，安装包内最大单文件）不被任何模块静态导入、无延迟加载导入表，二进制里也仅有 Linux 路径字符串（`libmklml_intel.so`/`mklml_dir`），看似纯死重
- **原因**：Paddle 的 `phi/backends/dynload/dynamic_loader.cc` 在 CPU 推理初始化时**无条件 `LoadLibrary("mklml.dll")`**，加载失败即中止（报 `PreconditionNotMetError: The third-party dynamic library (mklml.dll) that Paddle depends on is not configured correctly. (error code is 126)`）
- **解决**：不能删。实证方法：隔离重命名 mklml.dll + libiomp5md.dll 后直跑 PaddleOCR-json（脚本 `temp_scripts/script_20260902_mklml_test.py`），初始化即报错；测毕恢复。教训：**导入表干净 ≠ 可删**，Paddle 这类运行时大量依赖运行期动态加载，剔除 DLL 前必须跑一次真实推理验证

### P-019 windeployqt 的 compiler-runtime 选项探测不到 VS 2026
- **关联任务**：[T-027](#t-027-安装包瘦身第一阶段剔除-d3d-着色器编译器并捆绑-vc-运行库)
- **现象**：windeployqt 加 `--compiler-runtime` 后部署目录根没有任何 VC 运行库 DLL；构建输出有 `Cannot find Visual Studio installation directory, VCINSTALLDIR is not set`（`--compiler-runtime` 静默不生效，只有这条无关警告提供线索）
- **原因**：Qt 6.8.3 的 windeployqt 依赖 VS 安装探测（VCINSTALLDIR 环境变量 / vswhere），本机 VS 2026 的新版目录结构探测不到，Redist（`VC/Redist/MSVC/14.51.36231/x64/Microsoft.VC145.CRT`）明明存在
- **解决**：CMake 从 `CMAKE_CXX_COMPILER`（cl.exe）路径向上 7 级回溯 VS 安装根，`file(GLOB)` 定位 `VC/Redist/MSVC/*/x64/Microsoft.VC*.CRT`（过滤 onecore 变体、取排序最后），逐 DLL `copy_if_different` 部署到 `$<TARGET_FILE_DIR:PopTools>`；不再依赖 windeployqt 的探测

### P-017 Edge headless 按尺寸逐个渲染小尺寸会随机挂起
- **关联任务**：[T-026](#t-026-更换应用图标破框而出新形象--补上-exe-内嵌图标缺口)
- **现象**：同一脚本内 Edge headless（`--headless=new --screenshot`）按 512/256/64/48 顺序渲染均正常，轮到 32px 窗口时进程挂起直至超时，且非必然复现
- **原因**：未深究（疑似小窗口与 headless 合成器的时序问题）；逐尺寸渲染本身也慢（每尺寸一次完整启动）
- **解决**：改为只渲染一张 512px 透明底主图，其余尺寸用 Pillow `LANCZOS` 缩放——与最终 .ico 生成管线一致，预览即所得；渲染约 2 秒且稳定

### P-015 项目文件夹改名后 CMake 缓存指向旧路径
- **关联任务**：[T-025](#t-025-更名后首打包poptools-setup-020)
- **现象**：T-024 后文件夹由 `F:\snipaste` 改名 `F:\poptools`，`build/` 随之移动，但 `CMakeCache.txt` 内 `CMAKE_HOME_DIRECTORY`、Qt 工具链路径、目标 SOURCE_DIR/BINARY_DIR 等仍为 `F:/snipaste`
- **原因**：CMake 与 MSBuild 生成文件内嵌配置时的绝对路径，项目根路径变更后全部失效，增量构建与打包源（`build/Release`）不可信
- **解决**：`rm -rf build` 整目录删除后重新 `cmake -S . -B build -G "Visual Studio 18 2026" -A x64` 全量重建；凡项目根路径变更，`build/` 必须整目录重建，不能增量

### P-016 Git Bash 会把 taskkill 的斜杠参数误转换为路径
- **关联任务**：[T-025](#t-025-更名后首打包poptools-setup-020)
- **现象**：Git Bash 中 `taskkill /F /IM PopTools.exe /T` 静默失败（进程原样存活），报错重定向到 /dev/null 时更难察觉；冒烟测试后残留两个实例
- **原因**：MSYS 参数转换把 `/F`、`/IM` 当作 Unix 路径转写成 Windows 路径（如 `F:\`），taskkill 收到无效参数
- **解决**：改用 PowerShell `Stop-Process -Name PopTools -Force`（本机已验证）；若坚持 taskkill，需双斜杠 `taskkill //F //IM` 或设 `MSYS_NO_PATHCONV=1`。教训：进程清理后必须再次 `tasklist` 复核为空

### P-014 会话 shell 工作目录占用 build 目录导致无法整目录删除
- **关联任务**：[T-023](#t-023-v020-版本发布)
- **现象**：`rm -rf build` 报 "Device or resource busy"，且已 `cd` 到其他目录仍失败；但 `rm -rf build/*`（清空内容）成功
- **原因**：AI 会话的终端工作目录在两次调用之间持久——此前某条命令 `cd` 进了 `build/` 跑 ctest，持有该目录句柄的 shell 进程一直存在；Windows 不允许删除任何进程的当前工作目录
- **解决**：`rm -rf build/*` 清空内容 + 原地重新 `cmake -S . -B build` 配置，等效于整目录清理；预防：构建/测试命令避免 `cd` 进 build（用 `ctest --test-dir build` 等绝对/相对路径形式）

### P-013 本机 VS 2026 生成器名与失败配置的残留缓存
- **关联任务**：[T-020](#t-020-架构模块化改造功能模块可拔插)
- **现象**：按旧命令 `cmake -G "Visual Studio 17 2022"` 配置报 "could not find any instance of Visual Studio"；改用 VS 2026 生成器重新配置又报 "does not match the generator used previously: Visual Studio 17 2022"
- **原因**：本机仅装 VS 2026 Community（`D:\Microsoft Visual Studio\18\Community`），生成器名为 **"Visual Studio 18 2026"**；第一次失败配置已写入 `build/CMakeCache.txt`，缓存锁定生成器导致后续换生成器被拒
- **解决**：删除 `build/` 后用 `cmake -S . -B build -G "Visual Studio 18 2026" -A x64`；本快照「常用命令」已同步更正。教训：换生成器必须先清缓存目录，只删 CMakeCache.txt 不够（CMakeFiles/ 也要删）

### P-012 贴图像素级自动验证的三个坑
- **关联任务**：[T-018](#t-018-贴图无损-11-与真实投影重做)
- **现象与解决**：用「屏幕截图 vs 落盘 PNG 逐像素比对」自动验证贴图 1:1 时（`temp_scripts/script_pin_verify.py`）：
  ① **拖拽末段移动事件被合并**：脚本高频 `SetCursorPos` 模拟拖拽时，最后几步移动可能未及处理就收到 `WM_LBUTTONUP`，选区比发送坐标小几个像素 → 不能假设贴图落在拖拽终点，须在截图里模板匹配定位贴图真实矩形。
  ② **纯色区域单 patch 模板匹配误定位**：桌面壁纸近纯色时，贴图左上角 40×40 patch 在错误偏移处也逐像素相等，首个匹配落点错 → 需左上/中心/右下三点探针同时命中才算定位成功。
  ③ **PIL ImageGrab 抓屏不含鼠标光标**（GDI BitBlt 特性），像素比对无需为光标让位，可放心在贴图区域内移动鼠标后再抓屏。

### P-011 Chromium 系窗口的 UIA 树下行的两个坑（D3D 死胡同分支与渲染树未激活）
- **关联任务**：[T-016](#t-016-智能框选重写窗口枚举--uia-控件树移除-opencv-图像识别)
- **现象**：智能框选对记事本/资源管理器能到控件级，对 Edge / Electron / WebView2 应用永远只有整窗矩形
- **原因**：两个问题叠加。① Chromium 顶层窗口（Chrome_WidgetWin_1）下有两个**同尺寸**子元素：`Intermediate D3D Window`（GPU 渲染表面，无任何子节点）与 RootView（真正的 UI 树）；按"包含点的最小面积子元素"下行时面积并列取第一个，一头扎进 D3D 死胡同即停止，RootView 分支从未被探索。② Chromium 渲染进程的网页无障碍树**默认不构建**，需以读屏客户端身份发 `WM_GETOBJECT(OBJID_CLIENT)` 激活（不过实测 ElementFromHandle 的 UIA 客户端连接通常也会异步触发激活，且进程内激活一次后常驻）。
- **解决**：① 下行改为**带回溯的 DFS**——子元素按面积升序逐分支尝试，死胡同（子分支未命中更深元素）回退试下一个兄弟；全部分支皆死胡同才取面积最小的叶子。② 首次查询某窗口前先 `SendMessageTimeoutW(WM_GETOBJECT, 0, OBJID_CLIENT)` 敲一次门（每窗口一次，缓存于 `m_poked`）。
- **附带教训**：`IUIAutomationElement::FindAllBuildCache(TreeScope_Children)` 在 XAML/UWP/WPF 上是高效批量路径，但 Chromium/Qt Provider 不支持（返回空）；必须同时保留 `TreeWalker` 的 `GetFirstChildElementBuildCache`/`GetNextSiblingElementBuildCache` 兄弟链遍历（最基础的 Navigate 原语，所有 Provider 都实现）。调试 UIA 树结构时可用 Current* 属性逐节点 dump（`temp_scripts/uia_dump.cpp`）。另有四个实现级坑（均已修复，三个曾导致真机崩溃/失效）：
  ① **兄弟链回退空 vector 取 `back()`**：首个兄弟不包含查询点时候选列表为空，`out.back()` 直接越界崩进程。第一批真机验证只测了走批量路径的记事本/资源管理器，未暴露——**UIA 代码必须用走兄弟链的应用（Chromium 系）做冒烟**，可用 `temp_scripts/detector_test.exe <进程名>`（链接真实检测器源码，免 GUI）。
  ② **无效哨兵矩形 `{0,0,-1,-1}` 的面积是"负×负=正"**：`areaOf>0` 不能当有效性判断，必须用宽高均为正的 `validRect`，否则 UIA 未命中时哨兵会覆盖合法的整窗矩形，导致永远返回空。
  ③ **滚动容器的虚拟布局矩形**（含滚出屏幕的内容）可能比窗口还大：UIA 命中直接采信 + 最后裁剪到顶层窗口范围即可，不能按面积与整窗比较。
  ④ **暖启动缓存的引用计数转移契约**：缓存元素引用转移给下行函数后，若下行原样返回该元素（死胡同/无更深命中），再用 AddRef/Release 配平就会双重释放（use-after-free 闪退，且崩溃前矩形已正确显示、极具迷惑性）。契约应为：转移进、转移出，`node != hit` 时才额外释放 node。另有三个实现级坑：① 兄弟链回退里对空 vector 取 `back()` 会直接崩进程（第一批真机验证只测了走批量路径的记事本/资源管理器，未暴露）；② 无效哨兵矩形 `{0,0,-1,-1}` 的面积是"负×负=正"，`areaOf>0` 不能当有效性判断，必须用宽高均为正的 `validRect`，否则 UIA 未命中时哨兵会覆盖合法的整窗矩形导致永远返回空；③ 滚动容器的虚拟布局矩形（含滚出屏幕的内容）可能比窗口还大，UIA 命中直接采信 + 最后裁剪到窗口范围即可，不能按面积与整窗比较。

### P-010 不抢焦点的浮窗收不到本应用外的鼠标事件
- **关联任务**：[T-015](#t-015-划词卡片单例化与点击外部关闭)
- **现象**：给划词卡片（`Qt::Tool` + `WA_ShowWithoutActivating`）装应用级事件过滤器（`qApp->installEventFilter`）监听 `MouseButtonPress`，点击**其他应用程序**的窗口时过滤器从不触发，"点击外部关闭"失效；点击本应用内的窗口则正常
- **原因**：`QApplication` 事件过滤器只能看到派发给**本进程对象**的事件；点击其他应用的窗口由对方进程的窗口过程接收，Qt 全程无感知。浮窗不抢焦点，也就没有焦点丢失（focusOut / WindowDeactivate）可用
- **解决**：`WH_MOUSE_LL` 低级鼠标钩子（`SetWindowsHookEx`）系统级感知鼠标按下，按落点窗口归属（`WindowFromPoint` → `GetWindowThreadProcessId`）判断是否点击了外部。注意：钩子回调在安装线程（Qt 主线程）执行且低级钩子有超时限制，槽内须轻量；钩子坐标是**物理像素**，与 Qt 逻辑坐标换算（多屏/DPI）易错，直接用 Win32 API 在物理坐标系内判断最稳；本应用内的点击仍走 Qt 事件过滤器（那里能用控件父子关系识别卡片自己的下拉弹层），两条链路按进程归属天然互补

### P-009 DeepSeek Anthropic 兼容端点默认开思考且挂在 /anthropic 路径前缀下
- **关联任务**：[T-014](#t-014-translator-支持-anthropic-接口格式含配置项)
- **现象**：同一 Key 走 Anthropic 格式时，不带任何思考参数的请求返回的 `content[]` 首块是 `type:"thinking"`，小 `max_tokens` 时 token 全被思考吃掉、拿不到 text；且端点不在 `api.deepseek.com/v1` 下，而在 `/anthropic` 前缀（`/anthropic/v1/messages`）
- **原因**：DeepSeek 的 Anthropic 兼容层默认开启思考模式（官方 Anthropic 恰好相反——思考默认关、需显式开启）；兼容端点路径与 OpenAI 端点不同源
- **解决**：请求显式带 `thinking: {"type":"disabled"}`（实测 200 直出纯 text 块），配合 400/422 去字段降级重发兜底；Base URL 文档与占位符明确提示填 `https://api.deepseek.com/anthropic`。教训同 P-006：同一服务商两种协议的行为差异也必须真机验证

### P-008 运行中的实例会锁住 build/Release 产物导致构建失败
- **关联任务**：[T-013](#t-013-翻译界面拖拽调整大小与一键复制)
- **现象**：重新构建时报错失败——不只是交接中已知的 PaddleOCR-json.exe 残留占锁，Snipaste.exe 本体（如上次调试/冒烟测试后未退出）也会锁住 `build/Release/Snipaste.exe` 及其加载的 Qt/OpenCV DLL，导致链接与 POST_BUILD 部署步骤失败
- **原因**：Windows 文件锁：运行中的进程以独占方式映射其 exe 与已加载 DLL
- **解决**：构建前 `tasklist | grep -i "Snipaste\|PaddleOCR"` 检查，有则 `taskkill /F` 全部结束再构建（本任务实操验证：结束 PID 后构建一次通过）

### P-007 Qt 把 HTTP 401 一律报为 Host requires authentication
- **关联任务**：[T-012](#t-012-认证报错诊断与测试连接功能)
- **现象**：应用报 "Host requires authentication"，但用同一 Key/URL 走 curl 却是 200；且配置了三种 Base URL 全部报同一错误
- **原因**：两层叠加——(1) Qt 网络层把 HTTP 401 一律映射为 `AuthenticationRequiredError`（文案 "Host requires authentication"），服务端的具体原因（如"Key 无效 ****7ede"）被吞掉；(2) 真实根因是 Snipaste.ini 里保存的 Key 与用户以为的不同——API Key 输入框是密码模式，粘贴了剪贴板里的旧 Key 不可见
- **解决**：Translator 报错中文化（401 直接提示 Key 无效）、设置加「测试连接」按钮（表单当前值实测）、Key 框加显示切换。诊断手法：直接读 exe 旁 Snipaste.ini 核对实际保存值 + `curl -i` 看真实状态码

### P-006 DeepSeek thinking.type 合法值不含 none
- **关联任务**：[T-011](#t-011-翻译体验三项改进语言记忆--关闭思考--领域术语)
- **现象**：请求体带 `thinking: {"type": "none"}` 时 DeepSeek 返回 400：`unknown variant 'none', expected one of 'adaptive', 'enabled', 'disabled'`，导致「关思考」静默失效（靠降级重试兜底，但思考仍开着、输出慢）
- **原因**：初版按网络旧资料写了 `none`；DeepSeek V4 的 `thinking.type` 实际合法值为 `adaptive`/`enabled`/`disabled`
- **解决**：改为 `{"type": "disabled"}`，2026-09-01 真机验证通过（无 reasoning_content、completion 仅 3 token）。教训：第三方 API 字段取值必须真机验证，不能只信搜索摘要

### P-005 OpenAI 兼容端点对请求体未知字段校验不一
- **关联任务**：[T-011](#t-011-翻译体验三项改进语言记忆--关闭思考--领域术语)
- **现象**：为关闭思考模式在 `/chat/completions` 请求体加 `thinking` 字段后，严格校验的端点（如 OpenAI 官方）会返回 400 拒绝整个请求，翻译完全不可用
- **原因**：`thinking` 是 DeepSeek/智谱等国内端点的约定字段，并非 OpenAI 规范；不同服务商对未知/不支持的参数处理不一（忽略或 400）
- **解决**：`Translator` 首次请求携带 `thinking: {"type": "none"}`，收到 400/422 时去掉该字段降级重发一次（仅损失"关思考"优化）；另加 `<think>` 标签兜底清洗，即使关不掉思考也不污染译文

### P-001 OpenCV 官方预编译包不识别新版 MSVC
- **关联任务**：[T-006](#t-006-智能框选图像识别与-opencv-接入)
- **现象**：用 VS 2026（MSVC 19.51）配置时 `find_package(OpenCV)` 失败
- **原因**：官方 Windows 预编译包仅提供 x64/vc16 运行时，`OpenCVConfig.cmake` 按编译器版本匹配，不认识更新的编译器
- **解决**：`CMakeLists.txt` 显式 `set(OpenCV_ARCH "x64")`、`set(OpenCV_RUNTIME "vc16")` 强制按 vc16 查找（`CMakeLists.txt:45-48`）；另注意 CMP0144 策略要求 `OpenCV_ROOT` 大小写与包名一致

### P-002 windeployqt 报 developer build 警告
- **关联任务**：[T-005](#t-005-依赖纳管脚本与-ocr-进程内化)
- **现象**：构建后 windeployqt 部署运行库时输出 "(developer build?)" 警告
- **原因**：本地 Qt 套件未下载 translations/ 目录，且应用不加载 QTranslator/.qm，windeployqt 部署翻译文件时告警
- **解决**：windeployqt 加 `-no-translations` 参数（`CMakeLists.txt:74-79`）

### P-003 PaddleOCR 子进程找不到 opencv_world DLL
- **关联任务**：[T-008](#t-008-ocr-引擎替换为-paddleocr)
- **现象**：部署后 PaddleOCR-json.exe 启动失败；若直接复制两份 DLL 则占双倍空间
- **原因**：子进程以 `paddleocr/` 为工作目录启动，Windows 加载器按「exe 所在目录 → CWD」顺序找 DLL，根目录有 DLL 但 paddleocr/ 里没有时找不到
- **解决**：根目录与 `paddleocr/` 用硬链接共用同一物理文件；CMake 中先 remove 再 create_hardlink，且硬链接依赖 OpenCV 部署步骤先生成根目录 DLL（`CMakeLists.txt:93-111`）

### P-004 划词翻译读到剪贴板旧内容
- **关联任务**：[T-010](#t-010-划词翻译重构与-v013-发布)
- **现象**：划词翻译偶尔翻译出上一次复制的内容
- **原因**：模拟 Ctrl+C 后立即依赖 Qt 剪贴板缓存读取，信号/缓存时序不可靠，可能读到旧内容
- **解决**：模拟 Ctrl+C 后轮询 Win32 `GetClipboardSequenceNumber`，序列号变化才用 Win32 API 直读文本；约 600ms 未生效重发一次 Ctrl+C，总超时报错（`src/core/translation/SelectionTranslator.h:10-20`）
