# 项目交接文档

> 本文档由 AI 维护，用于任务交接。主档 ≤ 500 行，旧条目见 docs/handover/archive/（按编号按需检索，不通读）。
> 最后更新：2026-11-11 23:46（T-047 输入监控升级定案，入账待开工） · Agent_20261111234632

## 项目快照

> 维护智能体：Agent_20261111220402

- **项目名称**：PopTools（前身为 Snipaste，2026-09-02 更名；更早为 MySnipaste）
- **一句话描述**：Windows 全局实用小工具箱（常驻托盘、热键唤起、功能模块可拔插）——当前内置区域截图 + 本地离线 OCR（RapidOCR）+ 大模型翻译（OpenAI 兼容 / Anthropic 双协议）+ 贴图 + 桌面日历（月历农历/日程/待办）+ 活动监控（键入/时间线/OCR 快照本地日志）+ Goody 大模型管家（T-045），持续新增小工具
- **开源协议**：GPL-3.0（2026-09-02 由 Apache-2.0 变更；衍生作品必须同协议开源，闭源商用不被允许）
- **当前版本**：0.4.0。版本号在**三处**需同步改：`CMakeLists.txt` 的 `project(... VERSION ...)`、`packaging/PopTools.iss` 的 `#define MyAppVersion`、`src/app/MainWindow.cpp` 托盘「关于」对话框的硬编码文案（v0.2.0 起记入第三处）
- **技术栈**：
  - C++17，代码统一在 `poptools` 命名空间
  - Qt 6.8.3 LTS（LGPL 动态链接，本地免安装套件 `tools/qt/6.8.3/msvc2022_64`，不依赖全局 Qt）
  - CMake + MSVC（VS 2022，VS 2026 亦可，见坑 P-001）
  - OCR：RapidOCR-json v1.1.0（onnxruntime 静态单文件 16.8MB，hiroi-sora；QProcess 常驻子进程管道模式：stdin 发图片路径 JSON，stdout 读结果；协议与原 PaddleOCR-json 同构；简中 v3 配置启动（det/cls/rec v3 + dict_chinese）；中/英模型随包，cht/japan/korean/cyrillic 四套未使用的 rec 已由 install_rapidocr.py 幂等裁剪（T-041，恢复 = 移除 TRIM_REC_MODELS 表重跑）；引擎 53MB 见 tools/rapidocr，空闲 3 分钟自动回收）
  - 翻译：大模型 API 双协议（设置 `translate/apiFormat`）——OpenAI 兼容 `/chat/completions` + Bearer（默认 DeepSeek，可换 Ollama 等）与 Anthropic `/v1/messages` + `x-api-key`（DeepSeek 兼容端点 / 官方 Claude）；`QNetworkAccessManager` 实现；请求显式关闭思考模式（`thinking` 字段，端点不支持时 400/422 降级重发），响应做 `<think>` 兜底清洗；提示词注入用户配置的翻译领域与专有名词术语表
  - 窗口/控件识别：Win32 窗口枚举（user32 + dwmapi）+ Windows UI Automation（UIA 控件树，系统组件无第三方依赖），实现见 `core/capture/SmartRectDetector`
  - 热键：Win32 `RegisterHotKey`（user32），经 `core/hotkey/GlobalHotKey` 封装（nativeEventFilter）
  - 设置持久化：QSettings 写 exe 同目录 `PopTools.ini`，单例 `core/settings/Settings`
- **架构概览**（分层 + 功能模块化：core 与 ui 各为静态库（ui 内按功能分库），依赖方向 ui → core；app 可执行承载组合根与功能模块）：
  - **功能模块**（`app/IFeatureModule` + `FeatureRegistry` + `AppServices`）：截图 CaptureModule / 贴图 PinModule / 划词翻译 SelectionTranslateModule / 桌面日历 CalendarModule 四个自包含模块（`app/modules/`），各自注册热键、贡献托盘菜单、管理自己的窗口/钩子/事件过滤器；MainWindow 是组合根（托盘 + 服务装配 + 模块注册 + 设置/关于/退出）。模块间互不 include，跨模块交互经 AppServices 能力槽（`captureActive`/`captureIntercept`/`lastCaptureRect`）与共享服务槽（`ocr`/`calendarStore`）；设置保存后组合根经 `AppServices::settingsChanged` 信号广播，有缓存外观的模块各自重应用。**拔插功能 = 组合根删一处 add() + CMake 删一个目标**
  - `app/MainWindow`（QObject，非窗体）＝应用控制器：系统托盘 + 模块装配
  - 截图主流程：`CaptureWindow`（全屏画布式编辑，内部按控制器拆分：SelectionController / AnnotationEditor / ToolDrawingController / TextEditingController / OcrController / OutputController）→ OutputController 产出到剪贴板 → PinWindow 贴图（构造注入应用级共享 `PaddleOcrService`，OCR 子进程全局唯一；OCR 面板/动效经 `OcrHostCallbacks` 窄接口注入 OcrController，CaptureContext 保持纯画布/选区共享态）
  - OCR 流程：OcrController → `PaddleOcrService`（子进程）→ 识别文本以文本框覆盖画布，或打开 `OcrPanel` 文字栏
  - 翻译流程：`Translator`（大模型 API，提示词=基础指令+领域/术语表；CaptureWindow 内嵌一例服务截图翻译，SelectionTranslateModule 内一例服务划词卡片，各自单消费者）→ OcrPanel（截图翻译）或 `TranslateCard`（划词翻译卡片，全局仅一张）；`SelectionTranslator` 负责模拟 Ctrl+C 抓取选中文本；目标语言在两处入口共享并持久化（`translate/targetLang`），切换即记忆；源语言不记忆，每次新任务重置为自动检测；语言显示名↔代码映射统一在 `core/translation/Languages.h`
  - 桌面日历流程（T-042/T-043）：`CalendarStore`（core/calendar，JSON 持久化 `%LOCALAPPDATA%/PopTools/PopTools/calendar.json`，QSaveFile 原子写，改动即存；日程=待办单列表）→ `CalendarWindow`（顶层无边框窗口，**Z 序贴底**：desktoppin::glueToDesktop 紧贴 Progman 正上方 + 1s 守护，固定模式吞 WM_MOUSEACTIVATE 防抬窗、右键空白透传桌面菜单；编辑模式 ✏️ 置顶浮动拖缩、✅ 贴回；左 MonthView 自绘月历（公历+农历小字+节日色+事项圆点，双击格子原位内联添加）| 右单一日程列表（ItemDelegate：勾选圈=分类色/★加粗/删除线，拖拽重排回写））；显隐由设置 `calendar/enabled` 驱动（无托盘菜单项）；收起右上角 `CalendarMiniWidget`（今日未完成，60s 定时跨天刷新，同样贴底）；透明度/背景色/强调色经 settingsChanged 重应用；农历换算 `core/calendar/LunarCalendar`（1900-2100 压缩表算法 + 农历/公历节日）
  - 标注数据模型与渲染在 `core/annotation`（AnnotationCanvas / AnnotationItem / AnnotationPainter），UI 工具在 `ui/capture/tools`（ToolBase + ToolFactory + 各工具类）
- **目录含义**：
  - `src/main.cpp` — 程序入口
  - `src/app/` — 应用外壳（组合根）：MainWindow（托盘 + 模块装配）+ `IFeatureModule`/`FeatureRegistry`/`AppServices` 基础设施；CMake 目标：`poptools_app_infra`（基础设施，可执行与测试共链）+ `poptools_module_<id>`（每功能模块一个静态库，含其界面库）+ 聚合 `poptools_modules`（T-036 起「拔插=删一个目标」字面成立）
  - `src/app/modules/` — 功能模块（capture 截图 / pin 贴图 / selectionTranslate 划词翻译 / calendar 桌面日历），各自自包含
  - `src/core/` — 核心域静态库 `poptools_core`（annotation 标注模型与渲染、calendar 农历换算与日历数据仓库、capture 多屏捕获与窗口检测、hotkey 全局热键与全局鼠标钩子、monitor 活动监控（键盘钩子/文本组装/输入状态机/前台跟踪/OCR 快照管线/JSONL 存储，T-045）、ocr OCR 服务、settings 设置、translation 翻译与划词翻译、assistant Goody 助手域（LLM 流式客户端/工具注册表/权限/会话/每日记录/记忆/MCP，T-045）、box 桌面收纳盒数据仓库（T-044））
  - `src/ui/` — 界面层按功能分库：`poptools_ui_capture`（截图画布+工具栏+OCR 面板）/ `poptools_ui_pin`（贴图窗口）/ `poptools_ui_translate`（划词翻译卡片）/ `poptools_ui_calendar`（日历主窗口+月历+迷你条）/ `poptools_ui_settings`（设置对话框：左侧导航分页 + 分类管理，跟随系统明暗主题），`poptools_ui_goody`（Goody 管家聊天窗，T-045）/ `poptools_ui_box`（收纳盒窗口，T-044），聚合为 INTERFACE 目标 `poptools_ui`
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
  - 构建：`cmake --build build --config Release`（POST_BUILD 自动 windeployqt / 剔 D3D 三件 / 部署 VC 运行库 / 拷 paddleocr/。**构建前先结束 PopTools.exe 与 PaddleOCR-json.exe 两个进程**，见坑 P-008）
  - 测试：`ctest -C Release --test-dir build --output-on-failure`（40 用例：标注画板 / 语言映射 / 热键名 / 主题色 / API 辅助与请求体 / 注册表 / 设置回退 / 二维码夹具 / 农历换算与标签 / 日历数据仓库往返 / 收纳盒仓库与移动 / 活动存储轮转与清理 / 键入组装与打码 / 输入状态机 / 上屏检测与增量 diff / SSE 解析 / 权限决策 / 会话往返 / 工具注册表 / 每日摘要 / 调度器 / 长期记忆 / 工作记忆 / MCP 配置）
  - 运行：`build/Release/PopTools.exe`
  - 性能基线：`python scripts/perf_baseline.py`（启动耗时+内存采样，追加 temp_scripts/out/perf_baseline.jsonl；发版前跑一次对照历史）
  - 打包：`"D:\Inno Setup 6\ISCC.exe" packaging\PopTools.iss`（ISCC 6.7.3；输出 `dist/PopTools-Setup-<版本>.exe`）
  - Python 环境：项目根 `.venv`（Python 3.12，uv 管理；本机 Git Bash 无全局 python/cmake/uv，需用绝对路径 `D:\CMake\bin`、`D:\uv` 或临时 PATH）
  - UI 实拍冒烟脚本：`temp_scripts/script_ui_review_20261101.py`（模拟 F1 + 拖选区 + ESC，存 out/*.png）
- **当前状态**：**v0.4.0 已发布**（2026-11-02，GitHub Release 直发）；**v0.4.0 之后未发版改动**（均已提交至 480877d）：T-042/T-043 桌面日历两轮 + T-044 桌面收纳盒 + T-045 活动监控与 Goody 管家 + T-046 R1~R4 审查修复 63 项；**R5 审查修复在途未提交（见当前任务）**；下次发版需版本判定（新功能建议 MINOR）；无 CI；OCR 引擎缺失时不阻断构建（运行时提示不可用）

## 历史任务

> 按任务组织（不是按对话），最新的在最上面。编号从 T-001 递增，永不复用。
> 注：本节由 git 提交历史（2026-08-06 ~ 2026-08-24，共 19 个提交）在交接文档首次创建时整理而成。
> 维护智能体：Agent_20261111220402

### T-046 代码审查修复四连 R1~R4（T-045 代码全量复审，共修复 63 项）
- **执行智能体**：未登记（2026-11-11 凌晨会话，成果仅存于 git；本条目为交接文档会话依提交痕迹回填）
- **结果**：✅ 四轮「审查→修复→复审」闭环共修复 63 项（R1 全部 P0/P1×14 → R2 复审新发现 P1×2+P2×14+P3×6 → R3 旧代码区 P1×1+P2×5+P3×16 → R4 复审 P0×1+P2×2+P3×2），波及 monitor/assistant/box/goody/ocr/pin/annotation/settings/memory；提交 5b1de29 → 42d5c58 → a8b84b2 → 480877d
- **相关坑**：无新增 ｜ 细档：缺（细节以四个提交的 diff 为准；其后续复审批次 R5 在途未提交，见当前任务）

### T-045 活动监控 + Goody 大模型管家（监控层 / LLM 全栈 / 每日记录 / 记忆 / MCP）
- **完成日期**：2026-11-09（用户夜间授权全自动开发， Morning 睡醒验收）
- **执行智能体**：Agent_20261109024000
- **背景**：用户三大需求——①全局键盘监听原样落盘（知道"写了什么"，便于 AI 做日志）；②全局鼠标监听；③内置大模型管家 **goody**：多轮对话 / 完全掌控本软件 / 完全掌控计算机 / 每日本日工作记录 / 瞬时+长期记忆 / 权限管理与 MCP（"把 ZCode 核心搬进来"）。关键对齐决策（用户拍板）：密码框自动打码；中文上屏用内置 OCR 补齐（LL 钩子拿不到 IME 上屏，见 P-034）；鼠标**不逐条落盘**、只做输入状态机；权限三模式（ask/smart/auto，类 ZCode）；调试模型 `deepseek-v4-flash`；**监控开启期钩子常驻（红线 2 豁免，用户批准）**；API Key 只进本机 PopTools.ini 不入仓库。
- **结果**（全部落地，ctest 17→40 用例全绿；渲染/交互按协作纪律留用户目测验收）：
  1. **监控层** `src/core/monitor/`（8 对文件）：`GlobalKeyboardHook`（WH_KEYBOARD_LL，回调只入队）→ `KeyboardTextComposer`（修饰键状态机 + ToUnicodeEx 按前台布局翻译、密码框 ● 占位、空闲 3s/满 512 字/切窗分批）+ `InputActivityMonitor`（切窗/空闲超时/同窗 60s 切片三段闭合的活跃度时间线）+ `ForegroundTracker`（EVENT_SYSTEM_FOREGROUND）+ `ActivityStore`（worker QThread JSONL 追加、按日轮转、保留期清理、meta.json 补账索引）。落盘 `%LOCALAPPDATA%\PopTools\monitor\<日期>\{timeline,keys,ocrtext}.jsonl`。
  2. **中文上屏 OCR 快照**：`ImeCommitDetector`（CJK 布局门 + 字母后空格/数字/回车启发）→ `SnapshotOcrPipeline`（防抖 800ms + 限流 + 密码框禁拍 → 截前台窗口 → RapidOCR → 同窗文本增量 diff → 只记新增、图即弃）。增量 diff 纯函数 `diffAdded` 单测覆盖。
  3. **goody 全栈** `src/core/assistant/`（13 对文件）+ `src/ui/goody/GoodyWindow` + `GoodyModule`：`LlmClient`（DeepSeek OpenAI 兼容 SSE 流式 + `LlmStreamParser` 纯逻辑解析、tool_calls 按 index 聚合、中止/错误统一 finished）→ 工具回合循环（GoodyModule，单消息上限 8 轮防失控）；`PermissionManager`（ask/smart/auto + "总是允许此类"持久化 + 审计 `goody/audit.jsonl`）；`ChatSession`（QSaveFile 持久化 + system 每轮重建 + 40 条截断）；人格提示词（专业温暖小管家，只做电脑整理/电脑问题/记录，越界温和拒绝）+ 每轮注入时间/未结任务/记忆索引。
  4. **工具系统**：`ToolRegistry`（OpenAI function-calling schema；`AppServices.toolRegistry` 共享槽，模块 init 里贡献）——内置 `time.now` / `fs.list/read/write/move/delete`（删除走回收站 SHFileOperation）/ `shell.run`（PowerShell QProcess 异步 + 超时看门狗 + UTF-8 输出）/ `monitor.query`（线程池读日志）/ `app.generate_daily_record`；模块贡献 `app.capture`（Capture）、`app.pin_clipboard`（Pin）、`app.calendar_show`（Calendar）、`app.box_create`（Box，写类走权限）。
  5. **每日工作记录**：`DailyAggregator`（JSONL → 合并时间线 + 分窗摘录，12K 字符预算）+ `Scheduler`（60s 心跳、每日到点一次）+ `DailyRecordJob`（启动 5s 补账历史日 + 每日 22:00 含今天 + 工具手动；四节式 MD 写 `goody/daily/<date>.md` 并置 meta `summarized:true`）。
  6. **记忆系统**：长期记忆引用层叠 `MemoryStore`（`memory/longterm/index.json` 只存引用+标签+命中统计，正文 `entries/*.md` 按需读；L1 事实/L2 项目/L3 外部引用不复制内容；检索即 touch）；瞬时工作记忆 `WorkingMemory`（`task.start` 标题相近即**缓存命中**续用；`task.complete` 归档 `memory/archive/` 并释放上下文；工作集每轮注入系统提示词）。
  7. **MCP**：`McpStdioClient`（JSON-RPC 2.0 over stdio、initialize 握手、请求 id 匹配 + 30s 看门狗）桥接 `goody/mcp.json` → 工具名 `mcp__<server>__<tool>`，外部能力一律走权限确认。无配置文件零开销。
  8. 设置两新页「活动监控」「Goody 管家」；托盘一键启停（活动监控）；`Ctrl+Alt+G` 呼出聊天窗（可配）。
- **关键改动**：`src/core/monitor/*`（8 对）、`src/core/assistant/*`（13 对）、`src/ui/goody/GoodyWindow.{h,cpp}`、`src/app/modules/{ActivityMonitorModule,GoodyModule}.{h,cpp}`、Capture/Pin/Calendar/Box 四模块的工具贡献段、`Settings.{h,cpp}`（`monitor/*` 与 `assistant/*` 键）、`SettingsDialog.{h,cpp}`、`AppServices.h`（toolRegistry 槽）、`MainWindow.{h,cpp}`（ToolRegistry 装配 + 两模块注册）、CMake ×3、`tests/domain_tests.cpp`（+23 用例）、`README.md`
- **相关坑**：[P-032](#p-032-automoc-对-cpp-内-qobject-的-include-约定)、[P-033](#p-033-qthread-quit-before-exec-竞态吞掉排队任务)、[P-034](#p-034-ll-键盘钩子拿不到输入法上屏中文)
- **待用户**：真机验收——①设置开监控 → 打字/切窗/中文输入 → 查 monitor/<日期>/ JSONL 与密码打码；②填 DeepSeek Key → Ctrl+Alt+G 对话 + 工具确认卡片链路；③次日看 `goody/daily/` 自动生成；④可选自编 `goody/mcp.json` 验证 MCP 桥接。API Key（用户提供的测试 key）已写入本机 `build/Release/PopTools.ini`，不入仓库。

### T-044 桌面收纳盒（BoxStore + IconExtractor + BoxWindow + BoxModule）
- **执行智能体**：未登记（原会话未入账；本条目为交接文档会话依 git 痕迹回填）
- **结果**：✅ 模块已落地并随 T-045 会话一并提交（7cf94e6「含 T-044 未提交工作」）：core/box 仓库（BoxStore JSON 持久化 + IconExtractor 图标提取）+ ui/box/BoxWindow + BoxModule；ctest 覆盖收纳盒仓库与移动用例；后续 R 系列审查修复覆盖该模块
- **相关坑**：无记录 ｜ 细档：缺（原会话上下文已失；功能细节待触及该模块时依代码与提交补写）

### T-043 桌面日历体验重构：真贴桌面 + 双模式 + 日程待办合并 + 设置驱动
- **完成日期**：2026-11-08
- **执行智能体**：Agent_20261108204000
- **背景**：T-042 交付后用户真机验收提出五项要求：①日历必须贴在桌面、任何时候不能遮挡应用窗口；②显示开关移到设置页（撤托盘菜单项）；③编辑/固定双模式（编辑可拖缩，固定锁死如贴纸）；④双击月历格子原位添加日程；⑤日程=待办合并为单列表。
- **结果**（全部落地，用户验收通过）：
  1. **真贴桌面（三轮方案迭代，见 P-031）**：最终方案 = 顶层窗口 + Z 序贴底（`desktoppin::glueToDesktop`：紧贴 Progman 正上方，1s 守护定时器在 Z 序被搅动时归位、就位时零开销）+ 固定模式吞掉点击激活（`WM_MOUSEACTIVATE → MA_NOACTIVATE`，点击不再抬窗）+ 需要打字时（双击格子/点输入行）显式 `activateWindow()` 临时唤起、输入完自动归位；模块层全部 `raise()` 移除。
  2. **双模式**：固定（默认，锁死、右键空白处透传系统桌面菜单——向 DefView 转发 `WM_CONTEXTMENU`）/ 编辑（✏️ 置顶浮动 + 虚线框 + 拖动缩放，✅ 贴回）。置顶/贴边收起功能随双模式移除（EdgeTab 删除）。
  3. **日程=待办合并**：`CalendarStore::addItem` 单一列表（去 todo 字段，旧 JSON 读时忽略）；UI 单列表 + 全员勾选圈（圈色=分类色）；迷你条显示今日未完成。
  4. **设置驱动显隐**：`calendar/enabled` 单开关（显示=启动自动出现）；托盘「桌面日历」菜单项删除；日历 ✕ 按钮等同关闭该开关。设置页桌面日历页 = 显示开关 + 外观（透明度/背景色/强调色）+ 分类管理。
  5. **双击格子原位添加**：`MonthView::cellRectFor` 定位格子，窗口内弹内联输入框（Enter 添加/Esc 取消/失焦关闭）。
  6. 测试更新合并语义（17 用例保持通过）；README 重写桌面日历章节。
- **关键改动**：`src/ui/calendar/DesktopPin.h`（重写为 Z 序贴底）、`CalendarWindow.{h,cpp}`、`CalendarMiniWidget.{h,cpp}`、`ItemDelegate.{h,cpp}`、`ItemEditDialog.{h,cpp}`、`MonthView.{h,cpp}`、`src/core/calendar/CalendarStore.{h,cpp}`、`src/core/settings/Settings.{h,cpp}`、`src/app/modules/CalendarModule.{h,cpp}`、`src/ui/settings/SettingsDialog.{h,cpp}`、`tests/domain_tests.cpp`、`README.md`
- **相关坑**：[P-031](#p-031-桌面层嵌窗两条弯路setparent-不被合成与点击激活横跳)
- **待用户**：日常使用观察（多显示器场景贴底 Z 序行为未验证；Win+D 对贴底窗口的影响未验证）。

### T-042 桌面日历功能模块 + 设置菜单分页重构
- **完成日期**：2026-11-08
- **执行智能体**：Agent_20261108204000
- **背景**：用户给出一篇小红书笔记（「howto｜用AI手搓桌面日程小组件」，作者用 Codex 做的 Windows 桌面日程工具，功能清单齐全），要求：①用本项目框架把该日历小组件实现为软件的一个功能；②设置菜单改为规范化的大型软件设置（通用 + 各功能分页）；③先实现、不搞太复杂，但笔记的功能要能实现。笔记功能清单：月历自带中文农历 / 日程待办增删改与调整顺序 / 待办一键安排到不同日期 / 分类+自定义颜色+重要高亮加粗 / 完成自动删除线 / 窗口及各栏尺寸拖动调整 / 背景色强调色透明度 / 置顶+贴边自动收起+系统托盘 / 最小化收进右上角只显示待办 / 开机启动 / 数据纯本地不联网。
- **结果**：
  1. **第 4 个功能模块 calendar**（照模块化 checklist 接入：core 域 + ui 分库 + app 模块 + 三处 CMake + 组合根注册）：托盘菜单「桌面日历」开关窗口；窗口懒创建（模块 init 只装配）。
  2. **core/calendar**：`LunarCalendar`（1900-2100 经典压缩表农历换算 + 节日标注：春节/元宵/端午/七夕/中秋/重阳/腊八/除夕 + 元旦/五一/国庆；初一显示月名）+ `CalendarStore`（条目=日程/待办/分类，数组顺序即同组显示顺序，无显式 order 字段；JSON 经 QSaveFile 原子写，改动即存；测试可注入路径构造）。
  3. **ui/calendar**：`CalendarWindow`（无边框：HTCAPTION 标题栏拖动 + NCHITTEST 六向边缘缩放；水平分栏 月历|列表、垂直分栏 日程|待办，双 QSplitter 状态持久化；📌置顶 / —收起 / ✕关闭；贴边收起：moveEvent 距屏幕边 18px 内 → `EdgeTab` 贴边小条，悬停 280ms 或点击展开，展开后 1.5s 冷却防回吸）+ `MonthView`（自绘月历：选中强调色实底、今天描边圈、农历小字节日色、事项圆点≤3）+ `CalendarMiniWidget`（右上角今日待办条：未完成在前完成垫底、点击勾选、双击展开）+ `ItemDelegate`（分类圆点 / 勾选圈 / ★加粗 / 完成删除线）+ `ItemEditDialog`（增改+删除）；拖拽重排经 ReorderList::dropEvent 回写 store。
  4. **设置对话框重构**：左侧导航（通用/截图与贴图/翻译/桌面日历）+ QStackedWidget 四页，全部原有配置项迁入对应页；桌面日历页 = 置顶/贴边/开机显示三开关 + 透明度滑条 + 背景/强调色拾色器 + 「管理分类…」（`CategoryManagerDialog`，改动即时写 store）；`AppServices` 新增 `calendarStore` 共享槽与 `settingsChanged` 信号（组合根在设置 OK 后广播）。
  5. **数据纯本地**：`%LOCALAPPDATA%/PopTools/PopTools/calendar.json`（标准可写目录——Program Files 安装形态 exe 旁不可写，刻意不走 PopTools.ini 的 exe 旁约定）；开机启动复用应用级注册表项，新增 `calendar/showOnStartup` 配合。
  6. **测试 14 → 17 用例**：农历锚点（2024/2025/2026 春节、2025/2026 中秋、2023 闰二月初一）、dayLabel（节日/除夕/朔日月名/平日）、CalendarStore 往返（增删改/上下移/安排到/重排/分类删除重挂/JSON 重载）。
  7. **冒烟**：临时 ini 开 showOnStartup → 启动自动弹日历 → 截图验证月历农历（11-08=九月三十、11-09=十月初一，与推算一致）、列表渲染（分类圆点/★加粗/完成划线全部正确）；冒烟痕迹（ini/测试 json/临时脚本）已清理。
- **关键改动**：`src/core/calendar/`（新增 5 文件）、`src/ui/calendar/`（新增 10 文件）、`src/app/modules/CalendarModule.{h,cpp}`（新增）、`src/ui/settings/SettingsDialog.{h,cpp}`（重写为分页）+ `CategoryManagerDialog.{h,cpp}`（新增）、`src/app/AppServices.{h,cpp}`、`src/app/MainWindow.{h,cpp}`、`src/core/settings/Settings.{h,cpp}`（calendar/* 键）、CMake 三处、`tests/domain_tests.cpp`（+3 用例）、`README.md`
- **相关坑**：P-028、[P-029](#p-029-匿名命名空间自由函数裸调-connect-编译失败)
- **待用户**：①真机回归交互项（增删改/拖拽排序/右键菜单/安排到/贴边收起与展开/迷你条/设置页外观即时生效/分类管理）；②显示窗口时主进程内存 ~104MB（基线 35MB，窗口栈+双缓冲所致，隐藏后回落，未量化——关注是否需优化）；③发版决策（新功能建议 MINOR，可与 UI 第二批一起发）。

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

### T-040 OCR 引擎切换 RapidOCR-json（安装包 105.7 → 80.8MB）
- **完成日期**：2026-11-01
- **执行智能体**：Agent_20261101184000
- **背景**：用户要求安装包更小。部署 327MB 中 paddleocr/ 占 297MB（91%）。原定方案（T-027 遗留）为 RapidOCR-json，先做可行性终审再动服务层。
- **可行性终审（实测）**：下载 RapidOCR-json v1.1.0（hiroi-sora，Umi-OCR 引擎）——静态单文件 16.8MB 零 DLL 依赖；6 语言 onnx 模型齐全（ch v3/v4、en、cht、japan、korean、cyrillic + 字典）；管道协议与 PaddleOCR-json **完全同构**（stdin `{"image_path"}` → `{code:100,data:[{box,score,text}]}`，101=无文字，中英文实测识别正确；204=JSON 解析失败、299=未知错误）。
- **重要发现**：原 `PaddleOcrService` 空参数启动 PaddleOCR-json，**只加载简中模型——6 语言模型从未被引擎使用过**，换引擎无任何功能损失；「多语言保留」约束实际保护的是闲置文件。
- **结果**：
  1. `scripts/install_rapidocr.py`：下载 v1.1.0 发布包（70MB 7z）→ 7-Zip 便携版解压 → `tools/rapidocr`（92MB，green 套件不入库）。
  2. 服务更名 `PaddleOcrService` → `RapidOcrService`（诚实命名），引擎目录 `rapidocr/`，启动参数固定简中配置（det v3/cls/rec v3 + dict_chinese，对齐原默认行为）；管道/看门狗/空闲回收逻辑不变。
  3. POST_BUILD 部署 `rapidocr/` 并**清理旧 `paddleocr/` 残留**（防 297MB 混入安装包）。
  4. 实测（ISCC 实装）：**部署 327 → 121MB，安装包 105.7 → 80.8MB（-24%）**。onnx 权重近不可压缩（T-027 已测 11%），进一步瘦身需裁语言模型——砍 cht/japan/korean/cyrillic 四套 rec 约再省 38MB（安装包 → ~43MB），**待用户拍板**（涉及多语言约束）。
  5. 顺带：引擎切换后 stderr 启动零警告；OCR 回归（简中+英）通过真机冒烟。
- **提交**：`2def450`
- **待用户**：①真机 OCR 回归（截图扫描/译、贴图 OCR，简中与中英混排）；②语言裁剪决策（保留 6 语言 80.8MB vs 砍 4 语言 ~43MB）；③测速对比（onnxruntime 通常快于 Paddle CPU，未量化）。
- **相关坑**：安装脚本勿用 `text=True` 读 tasklist 等中文 GBK 输出（P-020）；本任务无新坑。

### T-039 性能治理：OCR 空闲自动回收、基线脚本与性能红线
- **完成日期**：2026-11-01
- **执行智能体**：Agent_20261101184000
- **背景**：用户问「功能变多会不会变卡」。结论：功能数量不是主因，踩主线程红线才是（反例：UIA 线程静默失效）。给出基线实测与三类手段，用户批准全部落地。
- **结果**：
  1. **OCR 子进程空闲自动回收**：PaddleOcrService 识别完成 3 分钟无新请求自动结束子进程（常驻约 775MB 是全项目最大单一开销；冷启动约 0.6s）。收尾开表 / 新请求停表；与 20s 超时看门狗、CrashExit 处理器互不误报。
  2. **基线脚本** `scripts/perf_baseline.py`（--help 可查）：杀旧实例冷启动 → WaitForInputIdle 计启动耗时 → 采样主进程/OCR 子进程工作集 → JSONL 追加 `temp_scripts/out/perf_baseline.jsonl`。实测：**启动 54~66ms、主进程 34.5~35.4MB、exe 1.53MB**。tasklist 输出 GBK 解码（P-020 同款坑，当场修复）。
  3. **性能红线清单**写入项目快照（六条：GUI 线程红线/钩子按需/回包归属/脏矩形/资源释放/init 轻装配），作为后续所有新功能的自查清单。
- **提交**：`92e6d3c`
- **待用户**：OCR 用一次后等 3 分钟，任务管理器观察 PaddleOCR-json.exe 消失（回收生效）；下次用 OCR 冷启动约 0.6s 正常。

### T-038 二维码识别（zxing-cpp 可选套件）
- **完成日期**：2026-11-01
- **执行智能体**：Agent_20261101184000
- **背景**：A 组功能头脑风暴，用户砍掉批量图片 OCR、选定二维码识别并授权「依赖怎么方便怎么来」。选型 zxing-cpp v2.2.1（Apache-2.0、CMake 原生、解码最稳），按 green 套件惯例落 `tools/zxing`（脚本下载、不入库）。
- **结果**：
  1. `scripts/install_zxing.py`：经代理下载源码 tar.gz（0.9MB）→ 标准库 tarfile 解压 → `tools/zxing`。
  2. CMake 检测 `tools/zxing/CMakeLists.txt` 存在才静态编入（EXCLUDE_FROM_ALL），定义 `POPTOOLS_HAVE_ZXING`；**缺失不阻断构建**，运行时按钮提示安装脚本。坑：开关名是 `BUILD_*`（`ZXING_*` 无效），`BUILD_EXAMPLES` 默认 ON 会经 FetchContent 联网拉 stb，必须关闭以保持离线可构建；2.2.1 公共头文件平铺在 core/src（无 ZXing/ 子目录）。
  3. `core/capture/QrScanner.{h,cpp}`：`qr::available()/scan()`，ImageView 按 BGRX 读 QImage ARGB32；一图多码。
  4. 工具栏新增【码】按钮（qrcode.svg 手绘线性图标）→ `CaptureWindow::onQrRequested`：选区物理像素裁剪 → 识别 → 内容弹窗展示 + 复制剪贴板（多码逐行）。
  5. 回归测试：`tests/fixtures/qr_test.png` 夹具（qrcode 库一次性生成、测试只依赖解码），`qr_scan_fixture` 断言解码内容与无码图返回空；**测试 13 → 14 用例**。
- **提交**：`221b966`（T-034~T-037 综合检查点）、`bbd9e83`（本任务）
- **待用户**：真机验证——截图 → 框住一个二维码 → 点【码】按钮 → 弹窗内容正确且剪贴板已有；无码选区提示未识别到。

### T-037 A 组小工具第一批：贴图 OCR/翻译 + 划词 LLM 动作
- **完成日期**：2026-11-01
- **执行智能体**：Agent_20261101184000
- **背景**：用户要求头脑风暴新功能；按「热键即用/全局/复用现有服务/单模块」标准给出候选清单并分组，用户选定 A 组（复用 OCR+翻译服务线）。第一批落两个 S 级；批量图片 OCR（M）与二维码识别（M，需选解码依赖如 ZXing）留待后续。
- **结果**：
  1. **贴图 OCR / 翻译**：PinWindow 右键菜单加「识别文字 / 翻译」（携带贴图内容的信号），PinModule 经共享 OCR 服务识别 → 行序重排拼接 → 文本/译文进剪贴板 + 托盘气泡反馈（不弹交互窗）。配套三件：① AppServices 新增 `notify` 贡献点（组合根注入 tray->showMessage）；② **OcrController 加回包归属防护**（`m_ocrPending`——共享 OCR 服务接第二个消费者后，无防护时贴图的回包会串入截图画布弹 QMessageBox，属修复的潜在串扰缺陷）；③ PinModule 升级为 QObject 双继承（照 SelectionTranslateModule 先例，connect/parent 可用）。
  2. **划词 LLM 动作**：Translator 新增 `LlmAction`（Translate/Explain/Summarize/Rewrite）+ 提示词纯函数 `translateActionPrompt`（langName 一并挪入头文件，可测）；领域/术语表仅对翻译动作注入（术语映射指令对其他动作是干扰）；TranslateCard 语言行加动作下拉（翻译/解释/总结/改写），动作按钮文案与结果区小节标题随动作同步，动作记忆持久化（`translate/action`）；划词请求信号带动作参数。解释/总结按目标语言输出，改写保持原文语言。
- **关键改动**：`src/core/translation/Translator.{h,cpp}`、`src/core/settings/Settings.{h,cpp}`、`src/ui/translate/TranslateCard.{h,cpp}`、`src/ui/pin/PinWindow.{h,cpp}`、`src/app/AppServices.h`、`src/app/MainWindow.cpp`、`src/app/modules/PinModule.{h,cpp}`、`src/app/modules/SelectionTranslateModule.cpp`、`src/ui/capture/OcrController.{h,cpp}`、`tests/domain_tests.cpp`（13 用例）、`README.md`
- **待用户**：真机回归——①贴图右键两项 + 气泡反馈 + 剪贴板内容；②划词卡片动作切换与记忆；③截图 OCR 不受影响（归属防护）
- **遗留**：A 组剩余两项——批量图片 OCR（M）、二维码识别（M，需先定解码库引入方式：green 套件 tools/ 或源码合入）

> 更早：T-001~T-020 → [handover/archive/tasks-001-020.md](handover/archive/tasks-001-020.md) ｜ T-021~T-036 → [handover/archive/tasks-021-036.md](handover/archive/tasks-021-036.md)

## 当前任务

> 维护智能体：Agent_20261111234632

<!-- 当前任务完成后：整理为历史任务条目移入上方，然后恢复此占位 -->

**在途：R5 审查修复轮（未提交；工作区 9 文件 +169/-29）。** R1~R4 复审延续轮，已落码 7 项（代码内均有 R5-* 标记注释）：

- **R5-B1** OCR 面板跨会话复活：退出会话后迟到的【译】回包置位面板、重进截图浮出旧内容（OcrController 过期回包门卫 + releaseSessionResources 复位 m_ocrPending + CaptureWindow 无条件 hide 兜底 / 重选时隐藏）
- **R5-B2** 拖拽绘制中右键 = 取消当前笔画（与 Esc 同语义）
- **R5-B5** 桌面日历跨午夜刷新：分钟 tick 检测日期变化触发 storeChanged（「今天」圈选与标题滞留昨天）
- **R5-C1** 会话文件非 JSON/版本不识别时先备份 .bad 再空载（防必发 save() 覆盖丢失聊天历史）
- **R5-C2** 装载期消息序列合法性过滤（孤儿 tool、断裂 tool_calls 整组丢弃，防网关 400 且 trim 无法自愈）
- **R5-D1** perf_baseline.py 补杀 RapidOCR-json.exe 并修正子进程内存观测（旧名遗漏 → ~770MB 孤儿子进程跨轮累积 + 观测恒 0）
- **R5-D2** Goody 呼出热键进程内重复组合检测（提示「与 PopTools 内其他快捷键冲突」）+ 托盘快捷键文案按设置动态生成 + 注册失败标注

**本任务收尾待办**：①核对 R5 是否还有未落码编号（B3/B4 缺号，向上一会话意图对齐）；②构建 + `ctest -C Release --test-dir build` 全绿；③提交（建议 `fix(ocr,calendar,assistant,hotkey)` 前缀）；④完成后本条目并入 T-046 或新立 T-047。

**上一任务移交（T-045 会话，Agent_20261109024000）**：

- T-045 真机验收清单未走（见 T-045「待用户」）；协作纪律：渲染/交互一律用户目测验收，AI 不做自主验收（P-031 教训）
- `build/Release/PopTools.ini` 内测试 API Key 属用户本机配置，发布包不带；发版版本判定见项目快照「当前状态」
- T-034~T-040 真机回归被用户明示跳过（以 perf_baseline 自动化冒烟代替）；日常使用发现问题按普通缺陷处理
- 重建前杀 PopTools.exe + RapidOCR-json.exe（坑 P-008；perf_baseline.py 自 R5-D1 起已自处理）；gh CLI 便携版 `temp_scripts/gh-cli/bin/gh.exe`，认证用 `git credential fill` 取令牌注入 `GH_TOKEN`（T-041 实操可复制）

**已定待开工：T-047 深度输入监控（UIA 取文 + 剪贴板流 + OCR 兜底增强）**——2026-11-11 深夜定案，方案已经用户确认，下一会话可直接开工：

- **背景**：T-045 监控开启后真机验证——四管线落盘格式正常（keys/timeline/ocrtext/meta），但 keys 流只有拼音原样、OCR 快照有错字（P-034 根因坐实），用户拍板升级「取文」。注意实际落盘路径为 `%LOCALAPPDATA%\PopTools\PopTools\monitor\`（T-045 条目所记路径少一层；旧条目不改，以 ActivityStore.cpp:140 代码为准）
- **① UIA-first 取文**（主菜）：SnapshotOcrPipeline 触发/防抖/增量 diff 架构不动，「取文」一步升级两级——新增 `UiaTextReader`（GetFocusedElement → TextPattern 取光标附近限长文本 → ValuePattern 兜底）拿精确文本记 `source:"uia"`，取不到回落现有截图+RapidOCR 记 `source:"ocr"`；密码框改用 UIA_IsPasswordPropertyId 精确判定；UIA COM 调用禁入 GUI 线程（P-025 前车之鉴）。参考：Text-Grab「Grab Text from Window」纯 UIA 先例；Chromium 2025-08 起默认开启 UIA（浏览器/Electron/Qt/WPF/Win32 Edit 均精确可读）
- **② 剪贴板流**：`AddClipboardFormatListener`（窗口消息监听，非系统钩子，不踩红线 2）→ 新流 `clipboard.jsonl`；只记元数据——文本记 chars+头尾各 200 字符预览、图片记分辨率+字节、CF_HDROP 记路径≤5 条；内容哈希去重 repeat 计数、高熵串（密钥类）打码 `[masked]`、排除 PopTools 自身写入、200ms 防抖；monitor_query/DailyRecordJob 自动获得新流
- **③ OCR 兜底增强**：RapidOcrService 试切 ch v4 rec 模型对（T-041 记录的闲置 15.5MB）+ 小字号 2x 放大，与 v3 A/B 对比后定去留
- **体量** M（1~2 会话段）；验收：中文对话后 uia/clipboard 两流拿到准确内容、OCR 仅对不支持 UIA 的应用兜底；密码框三流全静默

**已定待开工：T-048 桌面件层级升级（日历+收纳盒「低于图标」路线）**——2026-11-12 凌晨定案：

- **背景**：用户目标形态「高于壁纸、低于桌面图标、不遮挡应用窗口」；现状贴底窗口（T-043 方案）满足「不遮挡」但高于图标。逆向 xDiarys 定性：其手法 = SetWindowsHookEx 注入 explorer.exe + 子类化 SHELLDLL_DefView + 画进桌面绘制流（Fences 同款、非窗口方案）；活体已验证 explorer.exe 加载 dkdockexplorerx64.dll、日历无独立顶层窗口。逆向产物：`temp_scripts/xdiarys_probe/`（NSIS 解包 extract/ + 静态分析 script_20261112_xdiarys_static.py + 活体探针 script_20261112_xdiarys_probe.py）
- **定案路线：B 先实验**——0x052C 触发 Progman 分裂出壁纸带 WorkerW，DComp（WS_EX_NOREDIRECTIONBITMAP）窗口驻入该带自绘；本机 WE 在跑且当前无 WorkerW 分裂（DefView 直宿 Progman，探针已记录），实验直接验真伪。成 → 干净窗口路线落地日历+收纳盒（体量 M）；败 → 再议注入路线（体量 L+杀软误报税）或维持现状
- **用户活体目测 xDiarys 表现（2026-11-12）**：✅ 用户确认与 Wallpaper Engine **无冲突**（原话「没有打架」），层级与交互符合预期；**用户拍板：桌面收纳盒和日历都要改成此形态**
- **验收**：日历与收纳盒驻入桌面带、图标之下、应用窗口盖于其上、拖动交互正常、WE 共存无横跳（对照 P-031 教训）

**在途：T-049 / R6 批量修复轮计划（2026-11-12 整理，交后续 AI 执行）**——计划全文细档：[handover/t049_r6_plan.md](handover/t049_r6_plan.md)

- 新确认 BUG：P-035 F1 覆盖层糊屏（150% DPI 双重重采样，根因 CaptureWindow.cpp:436-439，修法已给）→ R6-B1
- R5 未提交改动 ctest 已实测全绿 → 可直接提交（R6-C1 核对 B3/B4 缺号后提交）
- 探查工作单 A~E（UI DPI 扫描 / QSS 主题 / R5 完整性 / 性能红线 / 测试缺口）产出 R6 候修清单后一轮全修
- 执行顺序：R5 提交 → R6 探查 → R6 修复 → T-047 → T-048 实验 → Goody UI 三阶段

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

> 决策待用户：①UI 现代化第二批范围提案（已批准做，见 T-041）；②ch v4 模型对去留（见 T-041）；其余已结算（语言裁剪、0.4.0 发版见 T-041）。

> 备注 1：安装包瘦身以归档条目 T-027 为起点；其记录的两个后续可选项中「换 RapidOCR-json 引擎」已由 T-040 落地，剩余可选项为 opencv 最小化重建。
> 备注 2：T-034~T-046 已全部提交（最新 480877d）；工作区仅剩 R5 在途改动（见上）。

## 踩坑记录

> 每条坑必须链接到产生它的历史任务条目。
> 维护智能体：Agent_20261111220402

### P-031 桌面层嵌窗两条弯路：SetParent 不被合成与点击激活横跳
- **关联任务**：[T-043](#t-043-桌面日历体验重构真贴桌面--双模式--日程待办合并--设置驱动)
- **现象**：实现「贴桌面小组件」时——①把窗口 SetParent 进 Progman/WorkerW（Wallpaper Engine / Rainmeter 经典技法）：窗口存在、可见标志为真、鼠标交互正常（右键能弹菜单），但**内容永远不被 DWM 合成**（完全透明不可见）；②改用顶层窗口 Z 序贴底后，又出现「盖住应用 ↔ 被应用盖住」每秒横跳
- **原因**：
  1. **本机（Win11 24H2+ 26300 + Wallpaper Engine）外来 GDI 子窗口在桌面层不被合成**。逐层排查：Progman 直接子级、WorkerW 子级、不透明化（摘 WA_TranslucentBackground）、摘 WS_EX_LAYERED、清 QWindow alpha surface format——全部交互正常但渲染失败。对照实验：WE 自己的壁纸窗口走 `WS_EX_NOREDIRECTIONBITMAP + DirectComposition`（独立合成路径，能显示），普通/分层 GDI 子窗口的重定向表面在该层均不出图。且 WE 会周期性重建/占用壁纸层窗口（网络资料证实），SetParent 路线与其结构性冲突。另注意：`setWindowFlags(原值)` 是空操作（flags 相同不重建窗口），强制重建需 `QWindow::destroy()` 后 show
  2. **Windows 默认点击激活**：鼠标点任何背景窗口都会把它提到前面（WM_MOUSEACTIVATE → MA_ACTIVATE），贴底窗口每被点一次就浮到应用之上，1s 守护又压回去——横跳
- **解决**：放弃 SetParent，改**顶层窗口 + Z 序贴底**（`glueToDesktop`：Z 序紧贴 Progman 正上方，一切普通窗口天然在其上；1s 守护归位、就位时零开销）+ 固定模式 `WM_MOUSEACTIVATE → MA_NOACTIVATE`（点击不抬窗）+ 打字场景显式 activateWindow 临时唤起。探针脚本（`temp_scripts/probe_desktop_windows.py`、`check_layer_owner.py`、`check_calendar_pin.py`）在本机排查中价值极高，建议保留复用
- **教训**：桌面层嵌窗在 24H2 + 壁纸引擎环境下不可依赖 SetParent；「贴桌面」用顶层 + Z 序锚定 Progman 是稳定解。用户 2026-11-08 明确规矩：**AI 不做自主验收**——本坑前两轮修复都因 AI 自行截图判断"应该好了"而多绕一圈，最终靠用户目测一次定位

### P-029 匿名命名空间自由函数裸调 connect 编译失败
- **关联任务**：[T-042](#t-042-桌面日历功能模块--设置菜单分页重构)
- **现象**：cpp 匿名命名空间里的自由函数（构造临时 QDialog 的 pickDate 助手）内写 `connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept)`，MSVC 报 C2660 "connect: 函数不接受 4 个参数"
- **原因**：非限定名 `connect` 在 QObject 派生类的**成员函数**里能命中基类静态成员，但在**自由函数**里只能靠 ADL——而 `QObject::connect` 是类静态成员而非自由函数，ADL 查不到，编译器只能找到别的重载集报参数不匹配。同类隐患：file-local 嵌套类（无 Q_OBJECT）里不能声明 `signals:`（moc 不处理，connect 运行期失败），回调要用 `std::function` 成员
- **解决**：自由函数中显式 `QObject::connect(...)`；嵌套 helper 类的对外通知一律 std::function 回调（先例：CalendarWindow::EdgeTab）

### P-025 SmartRectDetector Worker 带 parent 致 moveToThread 静默失败
- **关联任务**：T-036
- **现象**：智能框选在浏览器/Electron 大页面上悬停高亮卡顿（预算 800ms 的 UIA 下行直接阻塞覆盖层）；代码里有完整的工作线程设计却形同虚设，且引出连环 COM 引用错误长期潜伏（元素双重 Release、批量下行漏 Release、cleanup 在 CoUninitialize 后释放 marshaled 代理属 UB）
- **原因**：`m_worker(new Worker(this))` 带 parent——Qt 规定 moveToThread 对有 parent 的对象**静默失败**（仅 qWarning），queued drain 全部落回 GUI 线程执行；线程化从未发生，也没有任何断言能暴露
- **解决**：Worker 改无 parent 构造，析构 quit/wait 后显式 delete；四处 COM 引用契约修复。教训：**moveToThread 失败无断言**，线程化代码要验证"确实在别的线程跑"（如 QThread::currentThreadId 打点），不能只看代码结构

### P-026 批量替换波及新函数自身（syncChecked 无限递归）
- **关联任务**：T-036
- **现象**：新增 `Settings::syncChecked()`（包装 `m_s.sync()` + 可写性告警）后 ctest 段错误，单跑该用例崩在函数入口后
- **原因**：先写入含 `m_s.sync();` 的函数体，再执行「`m_s.sync();` → `syncChecked();`」的全文批量替换——把 syncChecked 自己体内的调用也替换了，无限递归栈溢出。批量替换与新代码交错的事故
- **解决**：syncChecked 体内恢复 `m_s.sync()`；测试单用例隔离定位（QtTest 传用例名参数）。教训：批量替换前新落定的代码先盘点；替换后必须全量 review diff；用例级测试是定位段错误的最快路径

### P-027 QMenu 父对象只能是 Widget，纯 QObject 组合根不能 parenting
- **关联任务**：T-036
- **现象**：修托盘菜单所有权时写 `new QMenu(this)`（this = 纯 QObject 的 MainWindow），编译错 C2665 无匹配重载
- **原因**：QWidget 构造只接受 QWidget* 父对象，QObject 级父子所有权对 QWidget 类不适用；组合根 MainWindow 是纯 QObject（托盘应用无主窗）
- **解决**：托盘菜单保持无父对象，`~MainWindow` 中 `shutdownAll()` 后显式 delete（action 随之销毁）。教训：纯 QObject 组合根的 QWidget 成员所有权必须显式管理，"加个 parent"不是万能解

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

### P-020 Git Bash 链路里 PowerShell `$_` 与路径反斜杠都会被吞
- **关联任务**：T-029
- **现象**：真机冒烟中三个连环坑——① `powershell.exe -Command "...$_.Name..."` 里的 PowerShell 变量 `$_` 被 Git Bash 先行展开为空，整条命令解析报错且**一条都没执行**；② Python 脚本收到的 `E:\\poptools\\...` 双反斜杠路径被 shell 逐层吃掉，存出 `poptoolstemp_scriptssmoke_1.png` 这种连体文件名；③ `subprocess.run(["tasklist"], text=True)` 在中文 Windows 上按 UTF-8 解码 GBK 输出直接抛 `UnicodeDecodeError`。
- **解决**：进程清理等带 `$`/复杂引号的 PowerShell 一律写成 Python 临时脚本、经 `subprocess` 列表参数调用（完全绕开 MSYS 转换）；给 Python 的文件路径用**正斜杠**；`subprocess` 捕获输出用 `bytes` + `.decode("gbk", errors="replace")`。教训：`$` 开头的任何内容在 Git Bash 双引号/裸传场景都不可靠，脚本化优先。

### P-016 Git Bash 会把 taskkill 的斜杠参数误转换为路径
- **关联任务**：T-025
- **现象**：Git Bash 中 `taskkill /F /IM PopTools.exe /T` 静默失败（进程原样存活），报错重定向到 /dev/null 时更难察觉；冒烟测试后残留两个实例
- **原因**：MSYS 参数转换把 `/F`、`/IM` 当作 Unix 路径转写成 Windows 路径（如 `F:\`），taskkill 收到无效参数
- **解决**：改用 PowerShell `Stop-Process -Name PopTools -Force`（本机已验证）；若坚持 taskkill，需双斜杠 `taskkill //F //IM` 或设 `MSYS_NO_PATHCONV=1`。教训：进程清理后必须再次 `tasklist` 复核为空

### P-014 会话 shell 工作目录占用 build 目录导致无法整目录删除
- **关联任务**：T-023
- **现象**：`rm -rf build` 报 "Device or resource busy"，且已 `cd` 到其他目录仍失败；但 `rm -rf build/*`（清空内容）成功
- **原因**：AI 会话的终端工作目录在两次调用之间持久——此前某条命令 `cd` 进了 `build/` 跑 ctest，持有该目录句柄的 shell 进程一直存在；Windows 不允许删除任何进程的当前工作目录
- **解决**：`rm -rf build/*` 清空内容 + 原地重新 `cmake -S . -B build` 配置，等效于整目录清理；预防：构建/测试命令避免 `cd` 进 build（用 `ctest --test-dir build` 等绝对/相对路径形式）

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

> 冷坑索引：P-001~P-013、P-015、P-017~P-019、P-024、P-028 → [handover/archive/pits-001-028.md](handover/archive/pits-001-028.md)

## 修改台账

> 维护智能体：Agent_20261111220402

| 时间 | 智能体 | 板块 | 操作 | 摘要 |
|---|---|---|---|---|
| 2026-11-01 22:20 | Agent_20261101184000 | 历史任务 / 踩坑记录 / 项目快照 / 当前任务 | 新增 T-034、T-035、T-036；新增 P-025、P-026、P-027；快照刷新（模块化 CMake 目标名、常用命令、版本备注） | ESC 策略重构 + UI 现代化第一批 + 五批整体优化结算；真机回归待用户 |
| 2026-11-01 22:50 | Agent_20261101184000 | 历史任务 | 新增 T-037 | A 组小工具第一批（贴图 OCR/翻译 + 划词 LLM 动作）结算；批量 OCR 与二维码遗留 |
| 2026-11-01 23:15 | Agent_20261101184000 | 历史任务 / 当前任务 | 新增 T-038；备注 2 更新（A 组遗留清空、提交号） | 二维码识别结算（zxing-cpp 套件）；T-034~T-038 已提交 |
| 2026-11-01 23:30 | Agent_20261101184000 | 历史任务 / 项目快照 / 当前任务 | 新增 T-039；快照加性能红线小节与基线数字、常用命令补基线脚本；当前任务写移交说明 | 性能治理结算；会话收尾移交下一会话 |
| 2026-11-01 23:50 | Agent_20261101184000 | 历史任务 / 项目快照 | 新增 T-040；技术栈/目录/常用命令刷新为 RapidOCR | 引擎切换结算：安装包 105.7→80.8MB（提交 2def450）；语言裁剪待用户拍板 |
| 2026-11-01 23:55 | Agent_20261101184000 | 当前任务 | 新增「功能候选（未排期）」小节（8 项：体量/复用/要点/否决项） | 候选清单入库，移交下一 AI |
| 2026-11-02 00:05 | Agent_20261102000004 | 历史任务 / 项目快照 / 当前任务 / 功能候选 | 新增 T-041；快照刷新（版本 0.4.0、OCR 中英模型 53MB、dist、ctest 14 用例、当前状态）；当前任务重写移交 | OCR 多语言裁剪（安装包 80.8→46.9MB）+ v0.4.0 发布（tag + GitHub Release） |
| 2026-11-08 21:00 | Agent_20261108204000 | 历史任务 / 项目快照 / 当前任务 / 踩坑记录 | 新增 T-042；快照刷新（一句话描述、架构概览加日历流程与 settingsChanged、目录含义、ctest 17 用例、当前状态）；当前任务重写移交；新增 P-028、P-029 | 桌面日历功能模块 + 设置菜单分页重构（ctest 14→17 用例，冒烟通过） |
| 2026-11-08 22:40 | Agent_20261108204000 | 历史任务 / 项目快照 / 当前任务 / 踩坑记录 / README | 新增 T-043；快照日历流程改贴底方案；当前任务重写移交（含 AI 不自主验收纪律）；新增 P-031 | 贴桌面重构（Z 序贴底+双模式+列表合并+设置驱动），用户验收通过 |
| 2026-11-09 02:50 | Agent_20261109024000 | 历史任务 / 项目快照 / 当前任务 / 踩坑记录 / README | 新增 T-045（T-044 留给收纳盒会话）；红线 2 加豁免条款；快照刷新（一句话描述、core/ui 目录、ctest 40 用例）；当前任务写移交；新增 P-032、P-033、P-034 | 活动监控 + Goody 管家全栈（监控/OCR 快照/LLM 工具权限/每日记录/记忆/MCP），ctest 17→40 用例 |
| 2026-11-11 22:10 | Agent_20261111220402 | 全文（归档） | 存量迁移：任务 T-001~T-020、T-021~T-036 原文剪切至 archive/tasks-001-020.md、archive/tasks-021-036.md；冷坑 P-001~P-013、P-015、P-017~P-019、P-024、P-028 → archive/pits-001-028.md；主档留索引；失效锚点引用去链为纯编号；T-041 重复标题行清除 | 主档 823 行回归 ≤500 行预算 |
| 2026-11-11 22:10 | Agent_20261111220402 | 历史任务 / 当前任务 / 项目快照 / 各板块头 / 修改台账 | 新增 T-046（R1~R4 入账，git 痕迹回填）、T-044（收纳盒回填）；当前任务改写为 R5 在途移交；快照「当前状态」与 box 模块标注刷新；各板块补维护智能体行 | 补记 2026-11-11 R 系列会话成果，交接对齐当下 |
| 2026-11-11 23:46 | Agent_20261111234632 | 当前任务 / 修改台账 / 头部 | 当前任务新增「已定待开工 T-047 输入监控升级」块（UIA 取文+剪贴板流+OCR v4，方案定案）；当前任务维护智能体与头部更新 | T-045 真机验证（监控四管线落盘正常）驱动输入监控升级定案，入账待开工 |
| 2026-11-12 00:40 | Agent_20261111234632 | 当前任务 / 修改台账 | 当前任务新增「已定待开工 T-048 桌面件层级升级」块（路线 B=DComp+WorkerW 先实验）；逆向 xDiarys（注入子类化手法定性）与活体验证结论入账 | xDiarys 逆向 + 活体验证完成，桌面件层级路线定案入账 |
| 2026-11-12 00:50 | Agent_20261111234632 | 当前任务 | T-048 块补记用户活体目测结论：xDiarys 与 WE 共存无冲突，用户拍板日历+收纳盒均改成此形态 | 用户验收通过，T-048 目标形态锁定 |
| 2026-11-12 01:00 | Agent_20261111234632 | 当前任务 / 踩坑记录 / 细档 | 新增 T-049/R6 批量修复轮计划（细档 t049_r6_plan.md：P-035 修法 + 探查工作单 A~E + 执行顺序）；新增 P-035；头部时间更新 | 用户报 F1 糊屏 bug 诊断定案；修复执行交后续 AI，本会话只出计划 |
