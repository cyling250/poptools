# 项目交接文档

> 本文档由 AI 维护，用于任务交接。主档 ≤ 500 行，旧条目见 docs/handover/archive/（按编号按需检索，不通读）。
> 最后更新：2026-11-16 00:33（T-054 清零阶段完成：日历/收纳盒痕迹清零，ctest 复绿中；移植阶段进行中） · Agent_20261116003308

## 项目快照

> 维护智能体：Agent_20261116003308

- **项目名称**：PopTools（前身为 Snipaste，2026-09-02 更名；更早为 MySnipaste）
- **一句话描述**：Windows 全局实用小工具箱（常驻托盘、热键唤起、功能模块可拔插）——当前内置区域截图 + 本地离线 OCR（RapidOCR）+ 大模型翻译（OpenAI 兼容 / Anthropic 双协议）+ 贴图 + 活动监控（键入/时间线/OCR 快照本地日志）+ Goody 大模型管家（T-045）+ 桌面日历（T-054 起，照搬 E:\rlqd 注入机制），持续新增小工具
- **开源协议**：GPL-3.0（2026-09-02 由 Apache-2.0 变更；衍生作品必须同协议开源，闭源商用不被允许）
- **当前版本**：0.4.0。版本号在**三处**需同步改：`CMakeLists.txt` 的 `project(... VERSION ...)`、`packaging/PopTools.iss` 的 `#define MyAppVersion`、`src/app/MainWindow.cpp` 托盘「关于」对话框的硬编码文案（v0.2.0 起记入第三处）
- **技术栈**：
  - C++17，代码统一在 `poptools` 命名空间
  - Qt 6.8.3 LTS（LGPL 动态链接，本地免安装套件 `tools/qt/6.8.3/msvc2022_64`，不依赖全局 Qt）
  - CMake + MSVC（VS 2022，VS 2026 亦可，见坑 P-001）
  - OCR：RapidOCR-json v1.1.0（onnxruntime 静态单文件 16.8MB，hiroi-sora；QProcess 常驻子进程管道模式：stdin 发图片路径 JSON，stdout 读结果；协议与原 PaddleOCR-json 同构；简中配置启动（det v3/cls v3/rec v4 + dict_chinese，T-047 起 rec 切 v4——A/B 实测小字 CER 减半）；中/英模型随包，cht/japan/korean/cyrillic 四套未使用的 rec 已由 install_rapidocr.py 幂等裁剪（T-041，恢复 = 移除 TRIM_REC_MODELS 表重跑）；引擎 53MB 见 tools/rapidocr，空闲 3 分钟自动回收）
  - 翻译：大模型 API 双协议（设置 `translate/apiFormat`）——OpenAI 兼容 `/chat/completions` + Bearer（默认 DeepSeek，可换 Ollama 等）与 Anthropic `/v1/messages` + `x-api-key`（DeepSeek 兼容端点 / 官方 Claude）；`QNetworkAccessManager` 实现；请求显式关闭思考模式（`thinking` 字段，端点不支持时 400/422 降级重发），响应做 `<think>` 兜底清洗；提示词注入用户配置的翻译领域与专有名词术语表
  - 窗口/控件识别：Win32 窗口枚举（user32 + dwmapi）+ Windows UI Automation（UIA 控件树，系统组件无第三方依赖），实现见 `core/capture/SmartRectDetector`
  - 热键：Win32 `RegisterHotKey`（user32），经 `core/hotkey/GlobalHotKey` 封装（nativeEventFilter）
  - 设置持久化：QSettings 写 exe 同目录 `PopTools.ini`，单例 `core/settings/Settings`
- **架构概览**（分层 + 功能模块化：core 与 ui 各为静态库（ui 内按功能分库），依赖方向 ui → core；app 可执行承载组合根与功能模块）：
  - **功能模块**（`app/IFeatureModule` + `FeatureRegistry` + `AppServices`）：截图 CaptureModule / 贴图 PinModule / 划词翻译 SelectionTranslateModule / 活动监控 ActivityMonitorModule / Goody GoodyModule / 桌面日历 CalendarModule 六个自包含模块（`app/modules/`），各自注册热键、贡献托盘菜单、管理自己的窗口/钩子/事件过滤器；MainWindow 是组合根（托盘 + 服务装配 + 模块注册 + 设置/关于/退出）。模块间互不 include，跨模块交互经 AppServices 能力槽（`captureActive`/`captureIntercept`/`lastCaptureRect`）与共享服务槽（`ocr`）；设置保存后组合根经 `AppServices::settingsChanged` 信号广播，有缓存外观的模块各自重应用。**拔插功能 = 组合根删一处 add() + CMake 删一个目标**
  - `app/MainWindow`（QObject，非窗体）＝应用控制器：系统托盘 + 模块装配
  - 截图主流程：`CaptureWindow`（全屏画布式编辑，内部按控制器拆分：SelectionController / AnnotationEditor / ToolDrawingController / TextEditingController / OcrController / OutputController）→ OutputController 产出到剪贴板 → PinWindow 贴图（构造注入应用级共享 `PaddleOcrService`，OCR 子进程全局唯一；OCR 面板/动效经 `OcrHostCallbacks` 窄接口注入 OcrController，CaptureContext 保持纯画布/选区共享态）
  - OCR 流程：OcrController → `RapidOcrService`（子进程）→ 识别文本以文本框覆盖画布，或打开 `OcrPanel` 文字栏；T-047 起监控快照取文升级——`UiaTextReader`（STA worker UIA：TextPattern→ValuePattern、IsPassword 判密、进程对号防串窗）为主路记 source:"uia"，截图+RapidOCR 兜底记 source:"ocr"（uia/ocr 双基线隔离）；`ClipboardListener` 增 clipboard.jsonl 流（300ms 防抖/OwnWriteGuard 自写排除/高熵打码/前 64KB 哈希去重）
  - 翻译流程：`Translator`（大模型 API，提示词=基础指令+领域/术语表；CaptureWindow 内嵌一例服务截图翻译，SelectionTranslateModule 内一例服务划词卡片，各自单消费者）→ OcrPanel（截图翻译）或 `TranslateCard`（划词翻译卡片，全局仅一张）；`SelectionTranslator` 负责模拟 Ctrl+C 抓取选中文本；目标语言在两处入口共享并持久化（`translate/targetLang`），切换即记忆；源语言不记忆，每次新任务重置为自动检测；语言显示名↔代码映射统一在 `core/translation/Languages.h`
  - Goody 流程（T-045/T-050）：GoodyFloatingBall 贴底悬浮球（拖动记忆/单击与 Ctrl+Alt+G 开窗/聊天隐没/workingChanged 呼吸光效）→ GoodyWindow 卡片式聊天窗（会话侧栏 + 消息气泡 + MarkdownRenderer 150ms 节流流式 + CodeHighlighter 块复制 + 思考折叠区 reasoning_content + 消息四件套）；ChatSession v2 多会话持久化
  - 标注数据模型与渲染在 `core/annotation`（AnnotationCanvas / AnnotationItem / AnnotationPainter），UI 工具在 `ui/capture/tools`（ToolBase + ToolFactory + 各工具类）
- **目录含义**：
  - `src/main.cpp` — 程序入口
  - `src/app/` — 应用外壳（组合根）：MainWindow（托盘 + 模块装配）+ `IFeatureModule`/`FeatureRegistry`/`AppServices` 基础设施；CMake 目标：`poptools_app_infra`（基础设施，可执行与测试共链）+ `poptools_module_<id>`（每功能模块一个静态库，含其界面库）+ 聚合 `poptools_modules`（T-036 起「拔插=删一个目标」字面成立）
  - `src/app/modules/` — 功能模块（capture 截图 / pin 贴图 / selectionTranslate 划词翻译 / activitymonitor / goody / calendar 桌面日历），各自自包含
  - `src/core/` — 核心域静态库 `poptools_core`（annotation 标注模型与渲染、capture 多屏捕获与窗口检测、hotkey 全局热键与全局鼠标钩子、monitor 活动监控（键盘钩子/文本组装/输入状态机/前台跟踪/OCR 快照管线/UIA 取文与剪贴板流/JSONL 存储，T-045/T-047）、ocr OCR 服务、settings 设置、translation 翻译与划词翻译、assistant Goody 助手域（LLM 流式客户端/工具注册表/权限/会话/每日记录/记忆/MCP，T-045））
  - `src/ui/` — 界面层按功能分库：`poptools_ui_capture`（截图画布+工具栏+OCR 面板）/ `poptools_ui_pin`（贴图窗口）/ `poptools_ui_translate`（划词翻译卡片）/ `poptools_ui_settings`（设置对话框：左侧导航分页，跟随系统明暗主题），`poptools_ui_goody`（悬浮球+卡片聊天窗，T-045/T-050）/ `poptools_ui_shared`（DesktopStack 贴桌 Z 序协调器），聚合为 INTERFACE 目标 `poptools_ui`
  - `tests/` — QtTest 域逻辑单元测试（ctest 运行，`SNIPASTE_BUILD_TESTS` 默认 ON）
  - `src/resources/` — qrc 资源与图标：`app_icon.svg` 为应用图标源文件（「破框而出」设计，托盘/窗口图标直接引用，见 T-026）；`icons/` 为 24×24 工具条线性图标（svg）
  - `scripts/` — 第三方依赖一键下载脚本（install_qt.py / install_paddleocr.py），走本机代理 `127.0.0.1:7897`
  - `tools/` — 第三方绿色套件（qt / rapidocr / zxing），不入库，由脚本生成（install_qt.py / install_rapidocr.py / install_zxing.py；install_paddleocr.py 为旧引擎遗留备查）
  - `packaging/` — Inno Setup 6 安装包脚本 `PopTools.iss`（AppId 已随更名更换为全新 GUID），打包源为 `build/Release` 整目录（T-027 起排除 `poptools_domain_tests.exe`）；`PopTools.ico` 不再手工维护，由 `scripts/make_ico.py` 从 app_icon.svg 生成（Edge 渲染 512 主图 → Pillow 合成 7 帧，见 T-026）
  - `dist/` — 安装包输出（已忽略；仅保留最新 `PopTools-Setup-0.4.0-20261101_2359.exe`，发新包后删旧包）
  - `temp_scripts/` — 一次性脚本与中间产物（不入库）
  - `build/` — CMake 构建输出（已忽略；`build/Release/` 有完整可运行产物，含 Qt 运行库、paddleocr/ 引擎）
- **性能红线（新增功能必查——本项目「卡」的根源是踩主线程红线，不是功能数量；基线实测：启动 54~66ms、主进程 ~35MB、OCR 子进程 ~775MB 懒启动 + 3 分钟空闲自动回收）**：
  1. GUI 线程红线：跨进程枚举（UIA/Win32 树）、网络、磁盘 IO、大图缩放/编码**禁入主线程**。线程化警惕 moveToThread 对带 parent 对象**静默失败**（P-025），线程化后必须验证确实在别的线程执行
  2. 全局钩子按需装卸：WH_MOUSE_LL 等系统级钩子禁止常驻，用完即卸（先例：GlobalMouseHook 仅卡片显示期间安装）。**豁免条款（T-045，用户 2026-11-09 批准）**：活动监控开启期间 WH_KEYBOARD_LL / 共享 WH_MOUSE_LL 允许常驻——前提：钩子回调零耗时只做入队；打码判定/文本翻译/磁盘写/OCR 全部在事件循环或 worker；设置关闭时立即完全卸载
  3. 共享服务回包必须带归属防护：多消费者共享单例（OCR 服务）时，回包处理前校验请求归属（先例：OcrController::m_ocrPending + PinModule SingleShotConnection）
  4. 覆盖层绘制必须脏矩形：paintEvent 新增动态元素一律 update(新旧矩形并集)，禁止全屏 update()（先例：取色卡片/扫描线/智能高亮）
  5. 重资源用完即释放：会话级大缓冲退出即清空（先例：m_baseScaled）；子进程类资源空闲自动回收（先例：PaddleOcrService 3 分钟回收）
  6. 模块 init 只装配：重资源懒加载，启动路径禁网络/磁盘重 IO
- **常用命令**：
  - 安装依赖（新机器必做，需代理 127.0.0.1:7897）：`python scripts/install_qt.py`、`python scripts/install_rapidocr.py`、`python scripts/install_zxing.py`
  - 配置：`cmake -S . -B build -G "Visual Studio 18 2026" -A x64`（本机 VS 2026，见坑 P-013；换生成器前必须删除 build/）
  - 构建：`cmake --build build --config Release`（POST_BUILD 自动 windeployqt / 剔 D3D 三件 / 部署 VC 运行库 / 拷 paddleocr/。**构建前先结束 PopTools.exe 与 RapidOCR-json.exe 两个进程**，见坑 P-008）
  - 测试：`ctest -C Release --test-dir build --output-on-failure`（域逻辑 QtTest——标注画板/监控键入与状态机/上屏 diff/SSE 与 reasoning/权限与会话 v2/工具注册表/记忆/MCP 配置/悬浮球手势/渲染节流等，全清单见 tests/domain_tests.cpp 用例名；**跑前先查 GetAsyncKeyState 无 Ctrl/Alt 残留**，见 P-046）
  - 运行：`build/Release/PopTools.exe`
  - 性能基线：`python scripts/perf_baseline.py`（启动耗时+内存采样，追加 temp_scripts/out/perf_baseline.jsonl；发版前跑一次对照历史）
  - 打包：`"D:\Inno Setup 6\ISCC.exe" packaging\PopTools.iss`（ISCC 6.7.3；输出 `dist/PopTools-Setup-<版本>.exe`）
  - Python 环境：项目根 `.venv`（Python 3.12，uv 管理；本机 Git Bash 无全局 python/cmake/uv，需用绝对路径 `D:\CMake\bin`、`D:\uv` 或临时 PATH）
  - UI 实拍冒烟脚本：`temp_scripts/script_ui_review_20261101.py`（模拟 F1 + 拖选区 + ESC，存 out/*.png）
- **当前状态**：**v0.4.0 已发布**（2026-11-02，GitHub Release 直发）；**T-054 进行中**（用户指令：日历+收纳盒痕迹清零后照搬 E:\rlqd 移植 Qt 桌面日历；清零已完成，移植进行中）；无 CI；OCR 引擎缺失时不阻断构建（运行时提示不可用）

## 历史任务

> 按任务组织（不是按对话），最新的在最上面。编号从 T-001 递增，永不复用。
> 注：本节由 git 提交历史（2026-08-06 ~ 2026-08-24，共 19 个提交）在交接文档首次创建时整理而成。
> 维护智能体：Agent_20261116003308

### T-050 Goody 管家界面升级·阶段一悬浮球 + 阶段二聊天窗重构（阶段三待开工）
- **执行智能体**：Agent_20261112003400（夜间全自动，子代理开发/复审流水线）
- **结果**：✅ 阶段一（1d85540，ctest 99 绿）：贴桌悬浮球（拖动记忆 assistant/ballPos、单击/热键开窗、聊天隐没、工作呼吸光效、设置页开关，结构冒烟端到端验证）；阶段二（5a00f7e，ctest 108 绿）：GoodyWindow 推倒重写为卡片式聊天窗（会话侧栏 + 消息气泡 + MarkdownRenderer 150ms 节流流式 + 代码高亮与块复制 + 思考折叠区 reasoning_content 透传/持久化/请求体剥离 + 消息四件套），ChatSession 升 v2 多会话（v1 无损迁移）；阶段三未开工（见当前任务）
- **相关坑**：P-039 ｜ 细档：[handover/t047_048_050_night_batch.md](handover/t047_048_050_night_batch.md) §4

### T-049 R6 批量修复轮（探查工作单 A~E → 9 条修复 + 测试补齐 16 例）
- **执行智能体**：Agent_20261112003400
- **结果**：✅ 四张只读探查单（DPI 扫描 / QSS 主题 / 性能红线 / 测试缺口）+ 复审闭环：修复 9 条（R6-B1 底图 DPR 直绘、B2 快照归属时序、B3 GoodyWindow 双通道、B4 翻译目标语言双入口同步、C1 fs.list 异步化、C2 MCP 懒启动、C3~C5 卫生）+ ToolText/consumeBaselineAdded 抽取 + 16 新用例；ctest 60→77 绿；复审跟进 R6-B1a 混合 DPR 回退随 T-047 提交。C17/C18 两个大重构按计划缓议（见当前任务遗留候修）
- **相关坑**：P-038 ｜ 细档：[handover/t047_048_050_night_batch.md](handover/t047_048_050_night_batch.md) §1

### T-047 深度输入监控升级（UIA 取文 + 剪贴板流 + OCR rec v4）
- **执行智能体**：Agent_20261112003400
- **结果**：✅ ①UIA-first 取文：UiaTextReader（STA worker，TextPattern→ValuePattern，IsPassword 精确判密，进程号对号防串窗，elPid 救回浏览器场景）+ uia/ocr 双基线隔离 + 滑动窗口 diff，失败回落截图+RapidOCR（ocrtext.jsonl 增 source 字段）；②clipboard.jsonl 新流（300ms 防抖、OwnWriteGuard 9 写入点自写排除、高熵打码、前 64KB 哈希去重 repeat 冲账、图片仅元数据）；③OCR rec 切 v4（A/B 实测小字 CER 5.17%→2.59%、时延 +11%、零回归——**T-041 悬置的 ch v4 去留就此结算**；v4 det 劣化与 2x 上采样均不采用）。密码三流全静默（500ms TTL 共享缓存）。ctest 77→86 绿；真机走查留用户
- **相关坑**：无新增（复审 P2×2+P3×8 当轮修毕）｜ 细档：[handover/t047_048_050_night_batch.md](handover/t047_048_050_night_batch.md) §2

### T-046 代码审查修复四连 R1~R4（T-045 代码全量复审，共修复 63 项）
- **执行智能体**：未登记（2026-11-11 凌晨会话，成果仅存于 git；本条目为交接文档会话依提交痕迹回填）
- **结果**：✅ 四轮「审查→修复→复审」闭环共修复 63 项（R1 全部 P0/P1×14 → R2 复审新发现 P1×2+P2×14+P3×6 → R3 旧代码区 P1×1+P2×5+P3×16 → R4 复审 P0×1+P2×2+P3×2），波及 monitor/assistant/goody/ocr/pin/annotation/settings/memory；提交 5b1de29 → 42d5c58 → a8b84b2 → 480877d
- **相关坑**：无新增 ｜ 细档：缺（细节以四个提交的 diff 为准）；R5 轮已于 2026-11-12 夜结算（7 项+复审修正，d1eeaf2，ctest 60 绿；B3/B4/C4 三项 P3 有意跳过——见 [handover/t047_048_050_night_batch.md](handover/t047_048_050_night_batch.md) §0）

### T-045 活动监控 + Goody 大模型管家（监控层 / LLM 全栈 / 每日记录 / 记忆 / MCP）
- **完成日期**：2026-11-09（用户夜间授权全自动开发， Morning 睡醒验收）
- **执行智能体**：Agent_20261109024000
- **背景**：用户三大需求——①全局键盘监听原样落盘（知道"写了什么"，便于 AI 做日志）；②全局鼠标监听；③内置大模型管家 **goody**：多轮对话 / 完全掌控本软件 / 完全掌控计算机 / 每日本日工作记录 / 瞬时+长期记忆 / 权限管理与 MCP（"把 ZCode 核心搬进来"）。关键对齐决策（用户拍板）：密码框自动打码；中文上屏用内置 OCR 补齐（LL 钩子拿不到 IME 上屏，见 P-034）；鼠标**不逐条落盘**、只做输入状态机；权限三模式（ask/smart/auto，类 ZCode）；调试模型 `deepseek-v4-flash`；**监控开启期钩子常驻（红线 2 豁免，用户批准）**；API Key 只进本机 PopTools.ini 不入仓库。
- **结果**（全部落地，ctest 17→40 用例全绿；渲染/交互按协作纪律留用户目测验收）：
  1. **监控层** `src/core/monitor/`（8 对文件）：`GlobalKeyboardHook`（WH_KEYBOARD_LL，回调只入队）→ `KeyboardTextComposer`（修饰键状态机 + ToUnicodeEx 按前台布局翻译、密码框 ● 占位、空闲 3s/满 512 字/切窗分批）+ `InputActivityMonitor`（切窗/空闲超时/同窗 60s 切片三段闭合的活跃度时间线）+ `ForegroundTracker`（EVENT_SYSTEM_FOREGROUND）+ `ActivityStore`（worker QThread JSONL 追加、按日轮转、保留期清理、meta.json 补账索引）。落盘 `%LOCALAPPDATA%\PopTools\monitor\<日期>\{timeline,keys,ocrtext}.jsonl`。
  2. **中文上屏 OCR 快照**：`ImeCommitDetector`（CJK 布局门 + 字母后空格/数字/回车启发）→ `SnapshotOcrPipeline`（防抖 800ms + 限流 + 密码框禁拍 → 截前台窗口 → RapidOCR → 同窗文本增量 diff → 只记新增、图即弃）。增量 diff 纯函数 `diffAdded` 单测覆盖。
  3. **goody 全栈** `src/core/assistant/`（13 对文件）+ `src/ui/goody/GoodyWindow` + `GoodyModule`：`LlmClient`（DeepSeek OpenAI 兼容 SSE 流式 + `LlmStreamParser` 纯逻辑解析、tool_calls 按 index 聚合、中止/错误统一 finished）→ 工具回合循环（GoodyModule，单消息上限 8 轮防失控）；`PermissionManager`（ask/smart/auto + "总是允许此类"持久化 + 审计 `goody/audit.jsonl`）；`ChatSession`（QSaveFile 持久化 + system 每轮重建 + 40 条截断）；人格提示词（专业温暖小管家，只做电脑整理/电脑问题/记录，越界温和拒绝）+ 每轮注入时间/未结任务/记忆索引。
  4. **工具系统**：`ToolRegistry`（OpenAI function-calling schema；`AppServices.toolRegistry` 共享槽，模块 init 里贡献）——内置 `time.now` / `fs.list/read/write/move/delete`（删除走回收站 SHFileOperation）/ `shell.run`（PowerShell QProcess 异步 + 超时看门狗 + UTF-8 输出）/ `monitor.query`（线程池读日志）/ `app.generate_daily_record`；模块贡献 `app.capture`（Capture）、`app.pin_clipboard`（Pin）。
  5. **每日工作记录**：`DailyAggregator`（JSONL → 合并时间线 + 分窗摘录，12K 字符预算）+ `Scheduler`（60s 心跳、每日到点一次）+ `DailyRecordJob`（启动 5s 补账历史日 + 每日 22:00 含今天 + 工具手动；四节式 MD 写 `goody/daily/<date>.md` 并置 meta `summarized:true`）。
  6. **记忆系统**：长期记忆引用层叠 `MemoryStore`（`memory/longterm/index.json` 只存引用+标签+命中统计，正文 `entries/*.md` 按需读；L1 事实/L2 项目/L3 外部引用不复制内容；检索即 touch）；瞬时工作记忆 `WorkingMemory`（`task.start` 标题相近即**缓存命中**续用；`task.complete` 归档 `memory/archive/` 并释放上下文；工作集每轮注入系统提示词）。
  7. **MCP**：`McpStdioClient`（JSON-RPC 2.0 over stdio、initialize 握手、请求 id 匹配 + 30s 看门狗）桥接 `goody/mcp.json` → 工具名 `mcp__<server>__<tool>`，外部能力一律走权限确认。无配置文件零开销。
  8. 设置两新页「活动监控」「Goody 管家」；托盘一键启停（活动监控）；`Ctrl+Alt+G` 呼出聊天窗（可配）。
- **关键改动**：`src/core/monitor/*`（8 对）、`src/core/assistant/*`（13 对）、`src/ui/goody/GoodyWindow.{h,cpp}`、`src/app/modules/{ActivityMonitorModule,GoodyModule}.{h,cpp}`、既有模块的工具贡献段、`Settings.{h,cpp}`（`monitor/*` 与 `assistant/*` 键）、`SettingsDialog.{h,cpp}`、`AppServices.h`（toolRegistry 槽）、`MainWindow.{h,cpp}`（ToolRegistry 装配 + 两模块注册）、CMake ×3、`tests/domain_tests.cpp`（+23 用例）、`README.md`
- **相关坑**：[P-032](#p-032-automoc-对-cpp-内-qobject-的-include-约定)、[P-033](#p-033-qthread-quit-before-exec-竞态吞掉排队任务)、[P-034](#p-034-ll-键盘钩子拿不到输入法上屏中文)
- **待用户**：真机验收——①设置开监控 → 打字/切窗/中文输入 → 查 monitor/<日期>/ JSONL 与密码打码；②填 DeepSeek Key → Ctrl+Alt+G 对话 + 工具确认卡片链路；③次日看 `goody/daily/` 自动生成；④可选自编 `goody/mcp.json` 验证 MCP 桥接。API Key（用户提供的测试 key）已写入本机 `build/Release/PopTools.ini`，不入仓库。

### T-041 OCR 多语言裁剪 + v0.4.0 版本发布
- **完成日期**：2026-11-02
- **执行智能体**：Agent_20261102000004
- **背景**：T-040 遗留三项决策，用户一次拍板：①裁剪 4 套多语言 rec 模型；②立即走 0.4.0 发版（T-034~T-040 全部改动随版发出）；③UI 现代化第二批批准（后置，先出范围提案）。用户明确**跳过真机回归**，以自动化基线代替冒烟。
- **前提核查**：RapidOcrService 启动参数固定简中 v3 配置（`src/core/ocr/RapidOcrService.cpp:147-150`），cht/japan/korean/cyrillic 四套 rec 从未被引擎加载，裁剪零功能损失；核查中发现 **ch v4 det/rec 模型对（约 15.5MB）同样未被引用**，未裁（待用户拍板）。
- **结果**：
  1. `scripts/install_rapidocr.py` 新增幂等裁剪步骤（TRIM_REC_MODELS 表，对已有安装同样生效）；`tools/rapidocr` 92 → 53MB。
  2. 全量重建后部署目录 121 → 83MB；**安装包 80.8 → 46.9MB（`PopTools-Setup-0.4.0-20261101_2359.exe`，49,158,586 字节，-42%）**。
  3. 版本三处同步 0.4.0（CMakeLists / PopTools.iss / 关于对话框；.iss 内两处 PaddleOCR 旧注释顺带更名 RapidOCR）；exe 二进制核验含 v0.4.0 无 0.3.2 残留。
  4. ctest 1/1 通过；基线：启动 72ms / 主进程 35.4MB / OCR 子进程懒启动 0MB（性能红线守住）。
  5. dist 删 0.3.2 包仅留最新；GitHub Release 直发（安装包资产）。
- **提交**：`a1236fc`（perf 裁剪）、docs 与版本翻转提交见 git log（tag `v0.4.0`）
- **相关坑**：①本会话 Git Bash PATH 无 cmake，实测改用 `D:\CMake\bin` 绝对路径（本机环境技能待更新，非项目问题）；②temp_scripts/gh-cli 在机器搬迁（F:\→E:\）中丢失、gh 登录态随之失效——已重装 gh 2.103.3 便携版至原路径，认证用 `git credential fill` 取 github.com 令牌注入 `GH_TOKEN`（T-033 的「认证走凭据管理器」记载已过时，此为当前有效做法）
- **待用户**：①ch v4 模型对去留（裁掉安装包约再省 14MB → ~33MB）；②UI 第二批范围提案（待下一会话产出）；③T-034~T-040 交互项未真机回归（用户明示跳过），日常使用发现问题按普通缺陷报。

> 更早：T-001~T-020 → [handover/archive/tasks-001-020.md](handover/archive/tasks-001-020.md) ｜ T-021~T-036 → [handover/archive/tasks-021-036.md](handover/archive/tasks-021-036.md) ｜ T-037~T-040 → [handover/archive/tasks-037-040.md](handover/archive/tasks-037-040.md)

## 当前任务

**T-054 桌面日历重写（照搬 E:\rlqd → Qt）+ 日历/收纳盒痕迹清零（进行中）**——需求单 requirements/20261116-rlqd-calendar-port.md：
- 清零阶段 ✅：旧日历（T-042/T-043/T-051/T-052 系列）、2026-11-15 YYNote 在途实现、收纳盒（T-044）、desktopband 体系的代码/设置键/设置页/测试/文档痕迹全部清除；交接文档全层级（主档/细档/归档）同步去痕。
- 移植阶段（进行中）：E:\rlqd\clone（DesktopCal 3.33 行为复刻，用户认定为已实现核心需求，**严禁改动原目录**）——Win32 注入 DLL（WH_CALLWNDPROC 注入 explorer、子类化 SHELLDLL_DefView/SysListView32、WM_ERASEBKGND AlphaBlend 共享内存位图）+ Qt 应用侧（QPainter 渲染 → 共享 DIB；日程编辑器/主菜单/设置/调整/搜索五窗）；数据落 `%LOCALAPPDATA%\PopTools\calendar\`（JSON 与 rlqd 同构）。
- 验收：日历在壁纸之上/图标之下，右键/框选/图标拖动透传，双击日期开编辑器，Explorer 重启自愈；ctest 全绿；用户真机目测。

**已定待开工：T-050 阶段三（管家总览 + 全历史检索）**——需求单 requirements/20261111-goody-ui.md 附录 A（体量 M）：
- HistorySearchTool：monitor 五流 + goody/daily + 聊天史 + memory 索引统一检索；域侧已就绪

**遗留候修（非阻塞，P3 级）**：R5-B3（PinWindow 数量上限）/ R5-B4（F3 剪贴板无图静默）；R6-C17/C18 缓议；SnapshotOcrPipeline 归属防护残余理论窗口；ClipboardListener 哈希口径；ChatSession 序列口径；流式光标"▍"

**移交备忘**：①API Key 在本机 build/Release/PopTools.ini，不入仓库、不许读取外传；②重建前杀 PopTools.exe + RapidOCR-json.exe；③崩溃自诊日志在 build/Release/crash/；④本机屏幕 150% DPI（物理=逻辑×1.5）；⑤跑 ctest 前 GetAsyncKeyState 自检无 Ctrl/Alt 残留（历史夜间实验曾泄漏修饰键卡下，见 P-046）

### 功能候选（未排期——下一会话可直接从表内挑活）

> 选型标准（T-037 头脑风暴与用户确认过）：**热键一按即用、全局生效、复用现有服务、单模块自包含**；
> 新模块按「新增功能模块 checklist」（项目快照后、T-036 后为 5 处编辑）接入。体量：S=半天内 / M=1~2 个会话段 / L=需立项。
> 已否决：批量图片 OCR（用户 2026-11-01 明确砍掉，勿再提）。

| # | 功能 | 说明与实现要点 | 复用 | 体量 |
|---|---|---|---|---|
| 1 | **窗口管理三件套**（置顶/半透明/移屏置中） | 热键作用于鼠标下窗口：`SetWindowPos` 置顶切换、`SetLayeredWindowAttributes` 透明度、跨屏居中（ScreenCapturer 多屏几何现成）。不依赖任何现有服务，是验证新模块接入 checklist 的最佳试验田 | GlobalHotKey + Win32 | S×3 |
| 2 | **全局取色** | 不进截图模式，热键直接吸色（放大镜 + Hex 进剪贴板并气泡提示）。screenColorAt + 取色卡片绘制已存在，独立小热键即可 | 取色管线 | S |
| 3 | **延时截图** | 托盘菜单「3 秒后截图」，截弹窗/悬停态；QTimer + startCapture 即可 | 截图管线 | S |
| 4 | **QR 生成** | 划词卡片/贴图右键「生成二维码」（悬浮展示 + 保存）。zxing-cpp 编码器已在包内，当前 CMake `BUILD_WRITERS=OFF`，打开并加 `ZXing::WriteBarcode` 调用即可 | zxing-cpp | S/M |
| 5 | **OCR 多语言切换** | 随包仅中/英模型（T-041 已裁 4 套多语言 rec，恢复见 install_rapidocr.py 的 TRIM_REC_MODELS），服务启动参数固定简中；切换需按语言重启子进程换 rec 模型（或每语言一实例），设置/工具栏加语言选择。注意与「简中默认行为对齐」决策的关系 | RapidOcrService | S/M |
| 6 | **剪贴板监听翻译** | QClipboard::dataChanged 检测外文自动弹卡（可开关、防打扰节流）。遵守性能红线第 2 条：监听按需启用 | TranslateCard 全套 | M |
| 7 | **截图历史面板** | 最近 N 张截图托盘面板，回看/重贴/复制；PinWindow 落盘 tempPopTools 已是天然素材 | PinWindow 落盘 | M |
| 8 | **剪贴板历史** | 文字+图片统一历史、搜索后粘贴（Win+V 增强）；独立新面板 + 全局热键，体量最大 | GlobalHotKey + 新 UI | M/L |

> 决策待用户：①UI 现代化第二批范围提案（已批准做，见 T-041）；其余已结算（ch v4 rec 已切、语言裁剪、0.4.0 发版见 T-041/T-047）。

> 备注 1：安装包瘦身以归档条目 T-027 为起点；其记录的两个后续可选项中「换 RapidOCR-json 引擎」已由 T-040 落地，剩余可选项为 opencv 最小化重建。
> 备注 2：T-034~T-050（阶段一二）已全部提交（最新 5a00f7e），工作区干净；夜间批次工程细节见 [handover/t047_048_050_night_batch.md](handover/t047_048_050_night_batch.md)。

## 踩坑记录

> 每条坑必须链接到产生它的历史任务条目。
> 维护智能体：Agent_20261116003308

### P-031 桌面层嵌窗两条弯路：SetParent 不被合成与点击激活横跳
- **关联任务**：T-043（已清档）｜ 记录智能体：Agent_20261108204000
- **现象**：实现「贴桌面小组件」时——①把窗口 SetParent 进 Progman/WorkerW（Wallpaper Engine / Rainmeter 经典技法）：窗口存在、可见标志为真、鼠标交互正常（右键能弹菜单），但**内容永远不被 DWM 合成**（完全透明不可见）；②改用顶层窗口 Z 序贴底后，又出现「盖住应用 ↔ 被应用盖住」每秒横跳
- **原因**：
  1. **本机（Win11 24H2+ 26300 + Wallpaper Engine）外来 GDI 子窗口在桌面层不被合成**。逐层排查：Progman 直接子级、WorkerW 子级、不透明化（摘 WA_TranslucentBackground）、摘 WS_EX_LAYERED、清 QWindow alpha surface format——全部交互正常但渲染失败。对照实验：WE 自己的壁纸窗口走 `WS_EX_NOREDIRECTIONBITMAP + DirectComposition`（独立合成路径，能显示），普通/分层 GDI 子窗口的重定向表面在该层均不出图。且 WE 会周期性重建/占用壁纸层窗口（网络资料证实），SetParent 路线与其结构性冲突。另注意：`setWindowFlags(原值)` 是空操作（flags 相同不重建窗口），强制重建需 `QWindow::destroy()` 后 show
  2. **Windows 默认点击激活**：鼠标点任何背景窗口都会把它提到前面（WM_MOUSEACTIVATE → MA_ACTIVATE），贴底窗口每被点一次就浮到应用之上，1s 守护又压回去——横跳
- **解决**：放弃 SetParent，改**顶层窗口 + Z 序贴底**（`glueToDesktop`：Z 序紧贴 Progman 正上方，一切普通窗口天然在其上；1s 守护归位、就位时零开销）+ 固定模式 `WM_MOUSEACTIVATE → MA_NOACTIVATE`（点击不抬窗）+ 打字场景显式 activateWindow 临时唤起。桌面层探针脚本（temp_scripts/ 下窗口层次/贴底检查类）在本机排查中价值极高，建议保留复用
- **教训**：桌面层嵌窗在 24H2 + 壁纸引擎环境下不可依赖 SetParent；「贴桌面」用顶层 + Z 序锚定 Progman 是稳定解。用户 2026-11-08 明确规矩：**AI 不做自主验收**——本坑前两轮修复都因 AI 自行截图判断"应该好了"而多绕一圈，最终靠用户目测一次定位

### P-029 匿名命名空间自由函数裸调 connect 编译失败
- **关联任务**：T-042（已清档）｜ 记录智能体：Agent_20261108204000
- **现象**：cpp 匿名命名空间里的自由函数（构造临时 QDialog 的 pickDate 助手）内写 `connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept)`，MSVC 报 C2660 "connect: 函数不接受 4 个参数"
- **原因**：非限定名 `connect` 在 QObject 派生类的**成员函数**里能命中基类静态成员，但在**自由函数**里只能靠 ADL——而 `QObject::connect` 是类静态成员而非自由函数，ADL 查不到，编译器只能找到别的重载集报参数不匹配。同类隐患：file-local 嵌套类（无 Q_OBJECT）里不能声明 `signals:`（moc 不处理，connect 运行期失败），回调要用 `std::function` 成员
- **解决**：自由函数中显式 `QObject::connect(...)`；嵌套 helper 类的对外通知一律 std::function 回调

### P-025 SmartRectDetector Worker 带 parent 致 moveToThread 静默失败
- **关联任务**：T-036
- **现象**：智能框选在浏览器/Electron 大页面上悬停高亮卡顿（预算 800ms 的 UIA 下行直接阻塞覆盖层）；代码里有完整的工作线程设计却形同虚设，且引出连环 COM 引用错误长期潜伏（元素双重 Release、批量下行漏 Release、cleanup 在 CoUninitialize 后释放 marshaled 代理属 UB）
- **原因**：`m_worker(new Worker(this))` 带 parent——Qt 规定 moveToThread 对有 parent 的对象**静默失败**（仅 qWarning），queued drain 全部落回 GUI 线程执行；线程化从未发生，也没有任何断言能暴露
- **解决**：Worker 改无 parent 构造，析构 quit/wait 后显式 delete；四处 COM 引用契约修复。教训：**moveToThread 失败无断言**，线程化代码要验证"确实在别的线程跑"（如 QThread::currentThreadId 打点），不能只看代码结构

### P-021 QToolButton 高分屏图标裁剪：DPR 元数据被绘制路径忽略
- **关联任务**：T-030
- **现象**：屏幕缩放≥150% 的机器上工具栏图标只剩左上角（横竖笔画）；100%/125% 正常。探针（`QT_SCALE_FACTOR` + 参数化 dpr 对比）定位：经 `setDevicePixelRatio` 打标的位图加入 QIcon 后，QToolButton 绘制路径按**物理像素**尺寸绘制（元数据被忽略），物理尺寸（size×dpr）超过按钮即裁剪；同一位图在 dpr=1.0/1.25 下因物理尺寸恰小于按钮而"侥幸正常"，极具迷惑性。
- **解决**：图标位图改 4x 超采样、**不设 DPR 元数据**，交给 QIcon 缩放到 iconSize（探针对比过的显式 iconSize、iconSize=物理尺寸等方案均无效，仅此方案稳妥）。教训：Qt 6.8.3 的 QToolButton 上，带 DPR 元数据的 QIcon 位图不可信；自绘图标一律超采样无元数据。

### P-022 QSS 类型选择器对命名空间类静默失效
- **关联任务**：T-030（第三次踩，首次见 [T-017] ToolBar.cpp 内注释）
- **现象**：`TranslateCard {...}` / `OcrPanel {...}` 样式从未生效，卡片底色/边框随系统调色板回落——暗色系统碰巧看不出问题，浅色系统才暴露。
- **原因**：项目所有类在 `poptools` 命名空间，`QMetaObject::className()` 返回 "poptools::TranslateCard"，QSS 类型选择器 `TranslateCard` 按类名匹配失败且**无任何警告**。
- **解决**：setStyleSheet 前给控件 setObjectName，样式表用 ID 选择器（`#translateCard {...}`）。教训：本项目凡对自定义类写 QSS，一律 ID 选择器；类型选择器只用于 QLabel/QPushButton 等非命名空间 Qt 类。

### P-023 浮窗关键颜色必须内联样式+调色板双通道，父级 QSS 继承不可信
- **关联任务**：T-030
- **现象**：划词卡片源文本正文颜色由卡片级样式表 `QTextEdit { color: ... }` 定义，开发机（Win11）渲染正确，故障机（Win10）渲染为灰；而同卡片内**控件内联样式**的错误文字（红）始终渲染正确。背景色等部分属性走父级样式表又能生效——失效是选择性的、按机器差异表现，极难凭本机现象推断。
- **解决**：关键颜色一律双通道——① 直接写在控件自身内联样式（`edit->setStyleSheet(主题规则)`），② 调色板 Text/Base 角色 Active/Inactive/Disabled 三组全钉（卡片是 `WA_ShowWithoutActivating` 不激活窗口，部分渲染路径走 Inactive 组）；③ 应用层强制 Fusion 风格消除 Win10/Win11 原生风格差异。父级 QSS 仅作布局类声明。
- **教训**：跨机器交付的 Qt 界面，"本机渲染正确"不是充分证据；颜色这类关键视觉属性要在**最贴近控件的层**声明。

### P-008 运行中的实例会锁住 build/Release 产物导致构建失败
- **关联任务**：T-013
- **现象**：重新构建时报错失败——不只是交接中已知的 PaddleOCR-json.exe 残留占锁，Snipaste.exe 本体（如上次调试/冒烟测试后未退出）也会锁住 `build/Release/Snipaste.exe` 及其加载的 Qt/OpenCV DLL，导致链接与 POST_BUILD 部署步骤失败
- **原因**：Windows 文件锁：运行中的进程以独占方式映射其 exe 与已加载 DLL
- **解决**：构建前 `tasklist | grep -i "Snipaste\|PaddleOCR"` 检查，有则 `taskkill /F` 全部结束再构建（本任务实操验证：结束 PID 后构建一次通过）

### P-032 AUTOMOC 对 .cpp 内 QObject 的 include 约定
- **关联任务**：T-045
- **记录智能体**：Agent_20261109024000
- **现象**：`ActivityStore.cpp`（worker 类定义在源文件内）构建报 `contains a "Q_OBJECT" macro, but does not include "ActivityStore.moc"`，AutoMoc 直接失败。
- **原因**：较新 CMake 的 AUTOMOC 对**源文件内**定义的 QObject 生成 `<basename>.moc`，约定 include 的是 `.moc` 后缀；旧项目/旧文档写的 `#include "moc_<basename>.cpp"` 已不被接受。
- **解决**：源文件末尾 `#include "ActivityStore.moc"`（与 `domain_tests.cpp` 末尾的 `domain_tests.moc` 同理）。

### P-033 QThread quit-before-exec 竞态吞掉排队任务
- **关联任务**：T-045
- **记录智能体**：Agent_20261109024000
- **现象**：`ActivityStore` 启动时以 QueuedConnection 排入 worker 的保留期清理，在测试中 `stop()` 紧随 `start()` 时被整体跳过，过期日目录未删。
- **原因**：worker 线程尚未进入 `exec()` 时调用 `quit()`，事件循环直接退出，**先排队的任务也不会执行**。
- **解决**：启动路径上"必须发生"的任务改 `BlockingQueuedConnection`（启动清理量极小，毫秒级阻塞可接受）；纯尽力而为的任务才允许 Queued。

### P-034 LL 键盘钩子拿不到输入法上屏中文
- **关联任务**：T-045
- **记录智能体**：Agent_20261109024000
- **现象**：WH_KEYBOARD_LL 只能看到原始键序（拼音字母、候选数字键），任何 IME 上屏后的中文都不存在于键盘事件流。
- **原因**：IME 组合与上屏发生在目标进程内部（composition 由 IMM 注入目标窗口），系统级钩子无旁路；跨线程 `ImmGetContext` 亦不可用。
- **解决**（用户拍板方案）：启发式检测疑似上屏（CJK 布局 + 近 3s 有字母 + 空格/数字/回车确认键，`ImeCommitDetector`）→ 防抖限流截前台窗口 → RapidOCR → 与同窗上次文本增量 diff → 只记新增文字（`SnapshotOcrPipeline`）；键入流里的拼音字母原样保留作证据，由日总结提示词过滤噪声。密码框聚焦时整链禁拍。

### P-035 F1 截图覆盖层糊屏（150% DPI 双重重采样）
- **关联任务**：T-049/R6-B1 ｜ 记录智能体：Agent_20261111234632
- **现象**：150% DPI 下按 F1，冻结屏幕背景显示模糊；截图输出图片不糊
- **原因**：`CaptureWindow.cpp:436-439` 物理分辨率截图被 scaled 到逻辑尺寸缓存，绘制时又被 1.5x DPR 放大回屏幕——双重重采样
- **解决**：修法已给（QPixmap 带 DPR 元数据 1:1 直绘，见细档 t049_r6_plan.md §1），待 R6 实施并用户目测。教训：**上屏路径禁止 scaled() 二次加工，物理像素图一律带 DPR 元数据直绘**（P-021 是图标类镜像教训：图标位图恰恰不能带 DPR 元数据）
- **结算**：✅ 已随 R6-B1 落地（2026-11-12 夜，另含 R6-B1a 混合 DPR 多屏回退），150% 一致性待用户目测

### P-038 移交文档"已实测全绿"记载与实测不符
- **关联任务**：T-049 ｜ 记录智能体：Agent_20261112003400
- **现象**：t049_r6_plan.md 记"R5 改动 ctest 全绿可直接提交"；夜间实测 chat_session_roundtrip 必红（R5-C2 过滤器把测试数据判孤儿 tool 丢弃）
- **原因**：上一会话的实测先于 C2 落码或未看用例级结果；文档结论未经当轮复验
- **解决**：独立构建+ctest 复验后再提交。教训：引用移交文档的"已验证"结论前先复跑（分钟级成本）

### P-039 QSettings IniFormat 未引号逗号值裂变 QStringList
- **关联任务**：T-050 ｜ 记录智能体：Agent_20261112003400
- **现象**：手改 ini 写 assistant/ballPos=777,777（无引号）→ 读回成双元素 QStringList，toString() 得空串，位置回默认
- **原因**：IniFormat 对含逗号的未引号标量按列表解析
- **解决**：getter 双形态兼容（双元素列表 join 还原）。教训：ini 键值含逗号/特殊字符时写入端带引号、读取端双形态容错

### P-046 键盘合成 ctest 对"物理修饰键卡下"敏感，SendInput 泄漏会假红
- **关联任务**：原夜间注入实验（任务已清档）｜ 记录智能体：Agent_20261114020000
- **现象**：夜间脚本注入键序泄漏 Ctrl/Alt 卡下（GetAsyncKeyState=0x8000），3 个 keyboard_composer 用例全红（键被"组合键不记录"守卫吞掉）；释放后即全绿
- **原因**：KeyboardTextComposer 构造时按系统实时状态初始化修饰键（设计如此）；测试进程忠实读到卡下的物理键
- **解决**：跑测试前先补发修饰键 KEYUP 或重启输入会话；教训：**注入键序后必查 GetAsyncKeyState 无残留**，否则 ctest 红不一定是代码红

> 冷坑索引：P-001~P-020、P-024、P-026~P-027 → [handover/archive/pits-001-028.md](handover/archive/pits-001-028.md)

## 修改台账

> 维护智能体：Agent_20261116003308

> 更早台账（2026-11-01 ~ 11-08）→ [handover/archive/ledger-arch.md](handover/archive/ledger-arch.md)

| 时间 | 智能体 | 板块 | 操作 | 摘要 |
|---|---|---|---|---|
| 2026-11-09 02:50 | Agent_20261109024000 | 历史任务 / 项目快照 / 当前任务 / 踩坑记录 / README | 新增 T-045；红线 2 加豁免条款；快照刷新（一句话描述、core/ui 目录、ctest 40 用例）；当前任务写移交；新增 P-032、P-033、P-034 | 活动监控 + Goody 管家全栈（监控/OCR 快照/LLM 工具权限/每日记录/记忆/MCP），ctest 17→40 用例 |
| 2026-11-11 22:10 | Agent_20261111220402 | 全文（归档） | 存量迁移：任务 T-001~T-020、T-021~T-036 原文剪切至 archive/tasks-001-020.md、archive/tasks-021-036.md；冷坑 P-001~P-013、P-015、P-017~P-019、P-024、P-028 → archive/pits-001-028.md；主档留索引；失效锚点引用去链为纯编号；T-041 重复标题行清除 | 主档 823 行回归 ≤500 行预算 |
| 2026-11-11 22:10 | Agent_20261111220402 | 历史任务 / 当前任务 / 项目快照 / 各板块头 / 修改台账 | 新增 T-046（R1~R4 入账，git 痕迹回填）；当前任务改写为 R5 在途移交；快照与目录标注刷新；各板块补维护智能体行 | 补记 2026-11-11 R 系列会话成果，交接对齐当下 |
| 2026-11-11 23:46 | Agent_20261111234632 | 当前任务 / 修改台账 / 头部 | 当前任务新增「已定待开工 T-047 输入监控升级」块（UIA 取文+剪贴板流+OCR v4，方案定案）；当前任务维护智能体与头部更新 | T-045 真机验证（监控四管线落盘正常）驱动输入监控升级定案，入账待开工 |
| 2026-11-12 01:00 | Agent_20261111234632 | 当前任务 / 踩坑记录 / 细档 | 新增 T-049/R6 批量修复轮计划（细档 t049_r6_plan.md：P-035 修法 + 探查工作单 A~E + 执行顺序）；新增 P-035；头部时间更新 | 用户报 F1 糊屏 bug 诊断定案；修复执行交后续 AI，本会话只出计划 |
| 2026-11-12 07:45 | Agent_20261112003400 | 全文（结算） | 夜间批次结算：T-046 补 R5 结算注记；新增 T-049/T-047/T-050 条目；当前任务重写（T-050 阶段三待开工 + 晨起验收清单 + 遗留候修）；新增 P-038、P-039；冷坑 5 条与 T-037~T-040 归档；快照/红线/目录/命令刷新 | 夜间全自动 7 提交（ead3339..5a00f7e），ctest 40→108 绿，R5/R6/T-047/T-050 阶段一二全部收口 |
| 2026-11-16 00:33 | Agent_20261116003308 | 全文（破坏性清理，用户指令） | T-054 清零阶段：删除旧日历/YYNote 在途实现/收纳盒/desktopband 全部代码、设置键、测试与文档痕迹；删除 T-042/T-043/T-044/T-048/T-053 条目与 P-036/P-037 坑（编号不复用）；细档去 T-048 节、归档去 P-028 与相关台账行；快照/当前任务/红线/命令刷新 | 日历+收纳盒全量清零（代码+文档），仓库回无痕迹基线；移植阶段进行中 |
