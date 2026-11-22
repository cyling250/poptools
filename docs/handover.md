# 项目交接文档

> 本文档由 AI 维护，用于任务交接。主档 ≤ 500 行，旧条目见 docs/handover/archive/（按编号按需检索，不通读）。
> 最后更新：2026-11-22 02:20（T-075 摘要入口改道日程编辑器：格内链接撤除+编辑器标题行可点直弹记事本，3315cfe 提交构建全绿待目测；另代办卸载「日历清单」原版 DesktopCal（桌面窗口冲突消除）+ 推送 13 提交） · Agent_20261122011419
> 最后更新：2026-11-22 01:15（T-074 收口冲刺：提交态复验全绿 + MCP 25 件冒烟 13/13 + 截图历史层自证 6/6 + 安装器对齐走查无缺口；双缺陷修复 a6e2a59（ocr_image 漏注册补齐 + box_catalog_get 悬垂 UAF 根修，P-074/P-075 入账）；验收手册 docs/20261122-t074-m3-acceptance.md——7 项真机验收待用户） · Agent_20261122011419
> 最后更新：2026-11-22 01:55（T-074 M3 融合收口：安装器联网装 dsh（iss [Code]+pack_dsh_runtime.py，ISCC 编译验证）+ 截图历史层（PinWindow 副本/CaptureHistoryJob OCR 标注 30 天清理/MCP 两工具）+ 收纳盒实时重载（BoxModule 转 QObject），03ab228 提交；T-074 全批次落码完成待用户验收） · Agent_20261121184000
> 最后更新：2026-11-22 01:05（T-074 M2.5 全功能交底收口：域工具 16 件并入 MCP（22 工具清单），跨进程 dirty 协议 + 心跳呼吸 + 单实例守卫 + 调试日志撤离，595aa07 提交；AGENTS.md 全功能指引与上下文纪律更新；打包形态决策点待用户） · Agent_20261121184000
> 最后更新：2026-11-22 00:15（T-074 M2 能力桥收口：poptools_mcp_server 挂进 dsh 端到端实证（node→mcp-manager→子进程存活），六工具=活动数据/日报月报落盘/截图/OCR，ca05a55 提交；P-073 dsh-home 双层路径坑入账，孤儿测试 home 已清） · Agent_20261121184000
> 最后更新：2026-11-21 22:45（T-074 删除批收口：旧 Goody 域全量退役（assistant 15 类/UI 六件/ToolRegistry 链/Settings 废弃键），domain_tests 3688→1770 行 52 用例全绿；旧记忆空零损失，dsh-home/AGENTS.md 人格基线写入；bc9abb9 融合批+8270ab3 删除批已提交） · Agent_20261121184000
> 最后更新：2026-11-21 21:40（T-074 M1 融合批落码：DshWindow 常驻窗+sidecar 开机即拉+悬浮球/热键直达 dsh+设置页收窄+API Key 启动环境注入，构建 0 编译警告+ctest 99 绿，node 常驻实证，未提交待目测；存量验收块 T-067~T-072 无损剪切 archive/current-pending-acceptance-1118-1121.md） · Agent_20261121184000
> 最后更新：2026-11-21 21:10（T-074 M1 落码：WebView2 嵌入+dsh sidecar PoC 全链就绪——安装脚本两件/Node 24.22.3/dsh 0.2.2-rc.5/侧车冒烟出 URL 成功/dsh_poc.exe 构建 0 警告已拉起待目测） · Agent_20261121184000
> 最后更新：2026-11-21 18:50（T-073 界面美化精修落码：设置导航指示条/标题层次/留白 + Goody IME Enter 守卫/空态欢迎卡/行高/间距/滚动条/过程行光环，构建 0 编译警告+ctest 99 绿，未提交待目测） · Agent_20261121184000
> 最后更新：2026-11-20 02:40（T-071 夜间批次落码完成：P0 修复 T-070 提交态编译损坏 + ChatSession 热路径四项/分散效率批/UI 收编批/日历悬停脏矩形，6 提交 5887ee9..78abd32 每步独立构建 0 警告+ctest 99 绿，待真机验收；P-070/P-071 入账；陈旧验收块 T-059/T-060/T-064/T-062 无损搬移 archive/current-superseded-1117-1118.md） · Agent_20261120005535
> 最后更新：2026-11-19 19:25（T-070 落码完成：Goody 按 deepseek-harness 形式重构——后端 ChatSession v3 会话事件日志/块化消息/审批审计对/投影派生模型历史（v1/v2 迁移），前端 dsh WebUI 形态（文档流气泡/ProcessRow 过程行/composer 大圆角卡/侧栏 280），提交 4e153c6，ctest 99 绿，未真机验收；dsh 源码研读存 temp_scripts/dl_dsh/） · Agent_20261119172053
> 最后更新：2026-11-19 17:20（T-069 落码完成：UI 现代化改版 M0-M3 全流程——Fluent token 底座 theming 库 + 设置界面 SettingCard/ToggleSwitch/即时生效 + Goody 头像行/卡片描边/自适应输入 + 全局配色统一 Fluent token（ThemeColors 表退役），构建+ctest 绿，未提交待用户目测验收；方案 docs/design/20261119-ui-fluent-restyle-proposal.md） · Agent_20261119172053
> 最后更新：2026-11-18 16:00（T-067 落码完成：Goody 起居注式日摘要（静默自主）+ 月度滚动月报（开机重写【月报更新】）+ 桌面日历摘要链接展示（双击面板后可点/notepad），textlog 接入日总结（T-066b 收口），ctest 97 绿，未提交待真机验收） · Agent_20261118130500
> 最后更新：2026-11-18 00:30（合并并行会话：本地「桌面日历单屏化+多屏花屏根修+拖动体验闭环」重编号 T-063（原本地草稿编号 T-059，与远端收纳盒 T-059 冲突）、「启动更新自查」T-064（原 T-060）、P-066（原 P-063）；特性已移植到远端槽位协议 CLK4 架构（虚拟桌面并集画布+双缓冲帧翻转+覆盖层预览拖动）） · Agent_20261117164000
> 最后更新：2026-11-17 16:45（T-059：桌面日历单屏化+多屏花屏/复制根修——协议 v2 增所在屏矩形、DLL 擦除两遍法限定日历屏、宿主坐标体系升级虚拟桌面、跨屏拖动；用户实拍副屏初验通过） · Agent_20261117164000

## 项目快照

> 维护智能体：Agent_20261116165900

- **项目名称**：PopTools（前身为 Snipaste，2026-09-02 更名；更早为 MySnipaste）
- **一句话描述**：Windows 全局实用小工具箱（常驻托盘、热键唤起、功能模块可拔插）——当前内置区域截图 + 本地离线 OCR（RapidOCR）+ 大模型翻译（OpenAI 兼容 / Anthropic 双协议）+ 贴图 + 活动监控（键入/时间线/OCR 快照本地日志）+ Goody 大模型管家（T-045）+ 桌面日历（T-054 起，照搬 E:\rlqd 注入机制），持续新增小工具
- **开源协议**：GPL-3.0（2026-09-02 由 Apache-2.0 变更；衍生作品必须同协议开源，闭源商用不被允许）
- **当前版本**：0.5.1。版本号在**三处**需同步改：`CMakeLists.txt` 的 `project(... VERSION ...)`、`packaging/PopTools.iss` 的 `#define MyAppVersion`、`src/app/MainWindow.cpp` 托盘「关于」对话框的硬编码文案（v0.2.0 起记入第三处）
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
  - `src/core/` — 核心域静态库 `poptools_core`（annotation 标注模型与渲染、calendar 桌面日历域（农历/节气/节日、布局命中、任务与设置 JSON 存储，T-054）、capture 多屏捕获与窗口检测、hotkey 全局热键与全局鼠标钩子、monitor 活动监控（键盘钩子/文本镜像/输入状态机/前台跟踪/OCR 快照管线/UIA 取文与剪贴板流/JSONL 存储，T-045/T-047/T-066）、ocr OCR 服务、settings 设置、translation 翻译与划词翻译、assistant Goody 助手域（LLM 流式客户端/工具注册表/权限/会话/每日记录/记忆/MCP，T-045））
  - `src/ui/` — 界面层按功能分库：`poptools_ui_theming`（T-069 Fluent token 底座：FluentTokens/fluentTheme 明暗双套+通用 QSS 生成器+applyFluentWindowChrome，**全部 UI 配色唯一来源**，新窗口接法见 FluentTheme.h 头注释）/ `poptools_ui_capture`（截图画布+工具栏+OCR 面板）/ `poptools_ui_pin`（贴图窗口）/ `poptools_ui_translate`（划词翻译卡片）/ `poptools_ui_settings`（设置对话框：T-069 起左导航+SettingCard 分组卡片+ToggleSwitch，即时生效），`poptools_ui_goody`（悬浮球+卡片聊天窗，T-045/T-050/T-069 头像行）/ `poptools_ui_calendar`（桌面日历：共享内存画布/渲染器/宿主控制器/五窗，T-054）/ `poptools_ui_shared`（DesktopStack 贴桌 Z 序协调器），聚合为 INTERFACE 目标 `poptools_ui`
  - `src/hook/` — 桌面日历 Explorer 注入 DLL 源码 `calendar_hook.cpp`（纯 Win32，CMake 目标 `poptools_calendar_hook` → `PopToolsCalendarHook.dll` 与 exe 同目录，T-054）
  - `tests/` — QtTest 域逻辑单元测试（ctest 运行，`SNIPASTE_BUILD_TESTS` 默认 ON）
  - `src/resources/` — qrc 资源与图标：`app_icon.svg` 为应用图标源文件（「破框而出」设计，托盘/窗口图标直接引用，见 T-026）；`icons/` 为 24×24 工具条线性图标（svg）
  - `scripts/` — 第三方依赖一键下载脚本（install_qt.py / install_rapidocr.py / install_zxing.py；install_paddleocr.py 为旧引擎遗留备查），走本机代理 `127.0.0.1:17890`
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
  - 安装依赖（新机器必做，需代理）：`python scripts/install_qt.py`、`python scripts/install_rapidocr.py`、`python scripts/install_zxing.py`（**代理端口实测已变：现监听 127.0.0.1:17890**（2026-11-16 实测，GitHub API 200；AGENTS.md 记的 7897 已失效，开机后先测端口再走代理））
  - 配置：`cmake -S . -B build -G "Visual Studio 18 2026" -A x64`（本机 VS 2026，见坑 P-013；换生成器前必须删除 build/）
  - 构建：`cmake --build build --config Release`（POST_BUILD 自动 windeployqt / 剔 D3D 三件 / 部署 VC 运行库 / 拷 paddleocr/。**构建前先结束 PopTools.exe 与 RapidOCR-json.exe 两个进程**，见坑 P-008）
  - 测试：`ctest -C Release --test-dir build --output-on-failure`（99 用例：域逻辑 QtTest——标注画板/日历农历与布局与存储/监控镜像与状态机/上屏 diff/SSE 与 reasoning/权限与会话 v3/工具注册表/记忆/MCP 配置/悬浮球手势/渲染节流等，全清单见 tests/domain_tests.cpp 用例名；**跑前先查 GetAsyncKeyState 无 Ctrl/Alt 残留**，见 P-046）
  - 运行：`build/Release/PopTools.exe`
  - 性能基线：`python scripts/perf_baseline.py`（启动耗时+内存采样，追加 temp_scripts/out/perf_baseline.jsonl；发版前跑一次对照历史）
  - 打包：`"D:\Inno Setup 6\ISCC.exe" packaging\PopTools.iss`（ISCC 6.7.3；输出 `dist/PopTools-Setup-<版本>.exe`）
  - Python 环境：项目根 `.venv`（Python 3.12，uv 管理；本机 Git Bash 无全局 python/cmake/uv，需用绝对路径 `D:\CMake\bin`、`D:\uv` 或临时 PATH）
  - UI 实拍冒烟脚本：`temp_scripts/script_ui_review_20261101.py`（模拟 F1 + 拖选区 + ESC，存 out/*.png）
- **当前状态**：**v0.5.1 已发布**（2026-11-19，Release id 451052105，含探针验收期保留）；**T-071 夜间批次已落码待真机验收**（6 提交 5887ee9..78abd32，见当前任务）；无 CI；OCR 引擎缺失时不阻断构建（运行时提示不可用）

## 历史任务

> 按任务组织（不是按对话），最新的在最上面。编号从 T-001 递增，永不复用。
> 注：本节由 git 提交历史（2026-08-06 ~ 2026-08-24，共 19 个提交）在交接文档首次创建时整理而成。
> 维护智能体：Agent_20261117004800

### T-075 日历摘要入口改道日程编辑器
- **执行智能体**：Agent_20261122011419
- **结果**：✅ 格内「»摘要」链接整链撤除（渲染/热区/模态分发/激活态），日报
  入口移至日程编辑器标题行（有日报才显「摘要»」，点击直弹 notepad）；月报
  pill 与 notepad 逻辑提升共享。构建 0 编译警告+ctest 全绿，未目测
- 相关坑：无 ｜ 提交 3315cfe（8 文件 +91/-143）

### T-073 界面美化精修——设置 + Goody 对话（UI 方向被否，转 T-074）
- **执行智能体**：Agent_20261121184000
- **结果**：⚠️ Fluent/dsh 底座上精修两界面（导航指示条/标题层次/间距 + IME 组词
  Enter 守卫/空态欢迎卡/行高 150%/细滚动条/过程行光环），构建 0 编译警告+ctest 99 绿；
  用户目测否决 UI 增量方向，拍板全量 dsh 化（T-074）→ IME 守卫等行为修复保留随
  T-074 提交，UI 精修过渡期服役
- 相关坑：无 ｜ 未提交（改动在工作区 9 文件，随 T-074 M1 处置）

### T-068 鼠标拖动「卡顿→瞬移」取证+根治+发版 v0.5.1
- **执行智能体**：Agent_20261119101500
- **结果**：✅ 探针实锤三类主线程停摆（30s 壁纸重算 ~160-170ms 主犯/IME 截屏/开机月报
  拼接 380ms），三处全挪离主线程+签名免重算，验证 30s 停摆归零；发版 v0.5.1
  （Release id 451052105，资产 56MB）；提交 9ffa3b4(fix)+2cb237b(bump)
- **相关坑**：P-068 ｜ 细档：[handover/t068_mouse_stall.md](handover/t068_mouse_stall.md)

### T-066 UIA 文本镜像输入监听（字母键序流完全替换）
- **执行智能体**：Agent_20261118130500
- **结果**：✅ TextMirrorMonitor 上线——UIA 焦点控件影子差分（输入/退格/替换以最终文本
  原文落盘 textlog.jsonl，拼音草稿→汉字 replace 全程可见）+ 同窗口续接防 RuntimeId 重建
  吞增量 + 密码字段影子重置防泄漏；KeyboardTextComposer/打码开关移除（完全替换）；
  真机验收达标（退格删"走"/"5→7"修正实证）；ctest 95 绿。路线史：TSF 被动 TIP 三诉求
  结构性冲突实锤转 UIA（weasel 本机实证）
- **相关坑**：P-067 ｜ 细档：[handover/t066_tsf_research.md](handover/t066_tsf_research.md)

> 更早：T-001~T-020 → [handover/archive/tasks-001-020.md](handover/archive/tasks-001-020.md) ｜ T-021~T-036 → [handover/archive/tasks-021-036.md](handover/archive/tasks-021-036.md) ｜ T-037~T-040 → [handover/archive/tasks-037-040.md](handover/archive/tasks-037-040.md) ｜ T-041~T-046 → [handover/archive/tasks-041-046.md](handover/archive/tasks-041-046.md) ｜ T-047~T-058 → [handover/archive/tasks-047-058.md](handover/archive/tasks-047-058.md) ｜ T-059~T-065 → [handover/archive/tasks-059-065.md](handover/archive/tasks-059-065.md)

## 当前任务

> 占位：无结算中任务。在途验收项见历史任务区各条目「待用户」与下方块。

**T-074 DeepSeek Harness 全量照搬——dsh 原样移植 + C++ 壳（M1 融合批落码待目测，2026-11-21）**：
- 用户拍板：界面/内核/插件架构全量照搬 dsh 且**跟随上游更新**（批注锁定「原样移植+
  给他套个壳」，不做内核语义重写）；全系统插件化=「桌面端智能管家，其余皆插件」；
  界面载体 WebView2 嵌真 dsh 前端；**管家职责收敛**（2026-11-21 三拍板）：日报/月报
  →dsh 原生定时任务（C++ 聚合器留作 MCP 工具后端、定时触发撤）、旧记忆→整理并入
  dsh 全局指令（一次性迁移）、窗口→原生标题栏+sidecar 常驻（PopTools 开着就在后台，
  弹出即用无"点了才启动"，PoC 包装边框撤掉）；API Key 由用户提供，写入本机
  PopTools.ini [dsh]（DshRuntime 经启动环境注入 DEEPSEEK_API_KEY——dsh 官方
  sidecar 供 Key 路径），不入仓库
- M0 三文档落盘：需求单 requirements/20261121-dsh-full-port.md + 盘点报告
  docs/design/20261121-dsh-inventory-report.md（96 万行/190 包/Cordis 2.9k/事件词表
  63 型/session-controller API 全列）+ 蓝图 docs/design/20261121-dsh-port-blueprint.md
- **定案**：sidecar 原样移植——dsh 自家桌面形态就是 Electron 壳跑 `dsh web` profile，
  PopTools 用 Qt 壳等价替换：npm @deepseek-ai/dsh（Node>=24.9 sidecar）+ WebView2
  加载 localhost:3080；桥接面唯一=MCP 能力桥/进程生命周期/展示路由；分期 M1 壳与
  运行时→M2 MCP 能力桥→M3 产品融合→M4 全系统插件化；回退=核心契约层五件套
  C++ 语义移植（报告 f 节矩阵，≈1.5-2 万行）
- **M1 已落码+提交（eb7034d，2026-11-21）**：install_webview2.py（NuGet 1.0.4370.26）+
  install_dsh.py（Node v24.22.3 + dsh 0.2.2-rc.5 1.3G；全局 .npmrc 旧代理端口 7897 经
  CLI 旗标覆盖——npm 优先级 CLI>env>user npmrc）+ src/ui/dsh（WebView2Host：
  QLibrary 动态解析 loader + DshRuntime：QProcess 托管 `node bin.js web --no-open`，
  stdout 解析「dsh web: <url>」启动行即进程级 token 登录态）+ dsh_poc 验证窗
- **M1 融合批已落码（21:40，未提交）**：①新 DshWindow（原生标题栏+WebView2 填满+
  状态覆盖层，关闭=hide 回后台不杀服务，几何记忆 dsh/windowGeometry）常驻语义=
  GoodyModule init 即 configure+start（PopTools 存续期 sidecar 一直在）+ WebView2Host
  增 initialize() 预热②GoodyModule 切路由：openChat/悬浮球/热键/设置页全走 DshWindow，
  呼吸光效=sidecar Starting 态；旧 GoodyWindow 回合链保留编译但 m_window 恒空（域删除
  与测试手术下批）；DailyRecordJob/MonthlyRecordJob 保留为工具后端，心跳/22:00/启动
  补账撤（goodySummariesChanged 心跳信号不再发）③设置页 Goody 组清到最小（悬浮球+
  打开入口，模型/权限/总结交 dsh 自身设置页；commitSettings/instantControls 同步收窄，
  Settings 新增 dshApiKey/dshWindowGeometry）④CMake：poptools_ui_dsh 正式入 ui 聚合
  （poptools_core PUBLIC）+goody 模块链接+PopTools POST_BUILD 部署 WebView2Loader.dll，
  dsh_poc 留根 CMake gated
- 侧车冒烟通过：`--dump-config` 出装配树；真启动 stdout 单行出 URL；DSH_HOME 指向
  %LOCALAPPDATA%/PopTools/dsh-home 生效；融合批构建 0 编译警告+ctest 99 绿；新版
  已拉起（node.exe 常驻实证）待目测：悬浮球/Ctrl+Alt+G → dsh 窗直达 → 真对话
- 坑（当场修）：SDK 10 的 interface 宏在 combaseapi.h 被 WIN32_LEAN_AND_MEAN 排除
  → 显式 include objbase.h；ICoreWebView2 取 URI 是 get_Source 非 get_Uri；
  npm 全局 .npmrc 旧端口压过环境变量
- 黑框根修（P-072）：隐藏期预建原生子窗 Qt 永不同步几何/可见性——WebView2 原生
  初始化推迟到首次 openWindow 后（openWindow show→initialize；showEvent 补
  applyBounds；controller put_IsVisible(TRUE)）；嵌入层文件级调试日志暂留
  （%LOCALAPPDATA%/PopTools/webview2_debug.log）；日志实证 rc=1080×720
  visible=yes + dsh 页导航完成，构建 0 编译警告
- **删除批已收口（bc9abb9 融合批用户目测通过后 → 8270ab3）**：src/core/assistant 全域
  退役（15 类）+ ui/goody 六件退役（悬浮球保留）+ ToolRegistry 共享链拆除
  （AppServices/MainWindow/Capture、PinModule 贡献块）+ GoodyModule 瘦身 984→178 行
  （DshWindow/悬浮球/热键/日历摘要槽四件事）+ Settings 废弃键拆除（热键/悬浮球键保留）
  + domain_tests 3688→1770 行（45 用例随域删，52 用例全绿，残留审计 src+tests 零命中）
- **记忆迁移完成**：旧记忆目录为空（MemoryStore 建成未积累，零损失）；
  dsh-home/AGENTS.md 已写入管家人格基线（dsh 原生 agent-instructions 机制=
  DSH_HOME/AGENTS.md，盘点报告 c 节），dsh 对话自带人设
- **M2 能力桥已收口（ca05a55，端到端实证）**：src/mcpserver（McpTools 纯函数库
  可单测 + MCP stdio 协议 exe）——工具六件：activity_daily（五流读取+白名单防
  穿越）/ write·read_daily_digest（goody/daily 约定路径）/ write·read_monthly_digest /
  capture_screenshot（虚拟桌面拼图）/ ocr_image（RapidOcrService 异步 120s）；
  dsh 接入=profiles/web/cordis.patch.yml 注入 mcp-manager 条目（stdio eager）+
  AGENTS.md 日报/月报工作流指令；domain_tests +2 用例=54 全绿；协议管道冒烟+
  端到端（node→mcp-manager→子进程存活）双实证
- 坑：P-073（dsh-home 双层路径漏算，P-067 同款——调试 home 与产品 home 分叉，
  配置不生效；孤儿测试 home 已清，教训：dsh-home 调试一律走产品
  AppLocalDataLocation=PopTools/PopTools 双层口径）
- **M2.5 全功能交底已收口（595aa07，2026-11-22）**：McpToolsDomain 域工具 16 件
  （日历日程读写+五项显示设置 / 活动监控设置 / 收纳盒 catalog+add+remove /
  翻译 translate_text / system_info+settings / digest_list+digest_search 上下文
  管理），tools/list 合并=22 工具清单；跨进程生效协议 ExternalSyncWatcher
  （dirty 标记读后即删：settings→syncExternal+广播、calendar→宿主重载、box→广播）
  + mcp.heartbeat 心跳→悬浮球呼吸（10s 无心跳回静默）；单实例命名互斥体
  （3080 EADDRINUSE 风险收口）；webview2 hostLog no-op（调试撤离）；AGENTS.md
  全功能指引+上下文纪律（先索引后取文，禁止整批读入；AI 可自维护月报头部索引）
- 待办（M3 产品融合）：①打包形态决策（见下方决策点）②收纳盒实时重载（v1 经
  设置变更兜底）③BoxModule 域深度工具（文件整理执行面）④截图历史目录管理
  （用户构想：近几天截图按层级组织 + AI 读图标注 + 定时清理）⑤日程编辑器/
  日历面板随 dsh 化的后续融合方向
- **打包形态决策点（待用户拍板）**：dsh 运行时 node(37MB zip)+dsh 包(1.3GB
  node_modules)+WebView2Loader——安装包整含则体积 >1.5GB 不现实；候选方案
  ①安装器首启下载（Inno [Code] 或首启检测缺失 → 引导下载器）②便携 zip 随
  附 install_dsh.py ③dsh 精简裁剪（去掉 experimental/未用包降体积）。当前
  源码树形态已可用（install_dsh.py + tools/ 定位健壮），iss 未注入 dsh 件
- **M3 融合收口（03ab228，2026-11-22）**：①安装器联网装 dsh（用户拍板）——iss
  [Code] ssInstall 时 WebView2 检测补装 + dsh 运行时下载解压（PowerShell 系统
  自带能力，失败不阻断）；pack_dsh_runtime.py 打包 tools/→Release 资产 zip；
  ISCC 编译验证通过 ②截图历史层（用户构想落地）——PinWindow saveHistoryCopy
  （张贴始终存副本 captures/年-月/，与 pinSaveDir 语义解耦）+ CaptureHistoryJob
  （15min 一轮：未标注图→RapidOCR→.label.json 侧车本地零 token；30 天保留期
  清理）+ MCP capture_history_list/search（OCR 文本检索）③收纳盒实时重载——
  BoxModule 转 QObject + box.dirty→reloadCatalog（MCP agent 整理桌面免重启）
- 待验收清单（用户）：①dsh 对话「我今天干了什么」（activity 工具）②「把今天的
  日报写一下」→ 日历日期链接 → notepad 弹日报（digest 工具+日历链路）③「把日历
  的农历关掉」（settings_set+dirty 协议即时生效）④「帮我整理桌面文件夹」（box
  工具+实时重载）⑤截图历史：贴几张图 → 等 OCR 标注 → 问「最近截了哪些图」⑥
  「每天 22 点写日报」schedule 定时（dsh 原生）⑦悬浮球呼吸随 agent 工具调用
- 红线：禁经桥接面改 dsh 内部/禁 fork dsh 修补（上游 issue 走上游）；
  extensions/cordis-runner（模型自改插件树 RCE 面）不启用
- **收口自证批（01:15，2026-11-22，a6e2a59）**：提交态复验全绿（防 P-070）+ MCP
  tools/list 实测 **25 件**（旧口径 22 件=M2.5 时刻，M3 +2、本次补 ocr_image +1）
  + 只读冒烟 13/13 + 截图历史层自证 6/6（模拟落盘→list/search 契约全对→清理）
  + 安装器 [Code] 对齐走查无缺口（zip 布局/标记口径/toolsDir 双形态/WebView2
  GUID）；双缺陷根修：ocr_image 漏注册（分发有、描述符无）+ box_catalog_get
  悬垂 UAF（内联 load().boxes() 遍历临时对象，P-074）。**验收手册
  [docs/20261122-t074-m3-acceptance.md](docs/20261122-t074-m3-acceptance.md)
  已备（7 项 step-by-step + 发版纪律备注）——待用户按手册真机验收后结算**

> 已提交待验收（验收清单原文 → [handover/archive/current-pending-acceptance-1118-1121.md](handover/archive/current-pending-acceptance-1118-1121.md)）：T-072 日程编辑器双修
>（f4f0f4f）｜ T-071 UI 收编+效率批（5887ee9..78abd32）｜ T-070 Goody 按 dsh 形式重构（4e153c6）｜ T-069b 托盘精简+F1 打桩（8ebd1cd）｜ T-069 Win11 Fluent 改版（2cbd47d）｜
> T-067 Goody 起居注+日历摘要（d3c926d）——注：其中 T-070 的 GoodyWindow 前端与 T-067 的
> 定时成文链路已被 T-074 dsh 原样移植改道（对话/日报走 dsh），验收项相应作废。

> 更早当前任务验收块（T-059/T-060/T-064/T-062+收纳盒，已被 v0.5.0/v0.5.1 发版覆盖）→
> [handover/archive/current-superseded-1117-1118.md](handover/archive/current-superseded-1117-1118.md)

**已定待开工：T-050 阶段三（管家总览 + 全历史检索）**——需求单 requirements/20261111-goody-ui.md 附录 A（体量 M）：
- HistorySearchTool：monitor 五流 + goody/daily + 聊天史 + memory 索引统一检索；域侧已就绪

**遗留候修（非阻塞，P3 级）**：R5-B3（PinWindow 数量上限）/ R5-B4（F3 剪贴板无图静默）；R6-C17/C18 缓议；SnapshotOcrPipeline 归属防护残余理论窗口；ClipboardListener 哈希口径；ChatSession 序列口径；流式光标"▍"；日历 DPR 变更/多屏未自适应（主屏物理一次构建，见细档 t054 §2）

**移交备忘**：①API Key 在本机 build/Release/PopTools.ini，不入仓库、不许读取外传；②重建前杀 PopTools.exe + RapidOCR-json.exe；③崩溃自诊日志在 build/Release/crash/；④本机屏幕 150% DPI（物理=逻辑×1.5）；⑤跑 ctest 前 GetAsyncKeyState 自检无 Ctrl/Alt 残留（历史夜间实验曾泄漏修饰键卡下，见 P-046）；⑥桌面日历与 rlqd 复刻版/原版 DesktopCal 不可同时运行（都子类化同两个桌面窗口）

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
> 维护智能体：Agent_20261117004800

### P-074 box_catalog_get 悬垂 UAF：range-for 内联遍历临时对象，读到堆碎片乱码
- **关联任务**：T-074 ｜ 记录智能体：Agent_20261122011419
- **现象**：MCP box_catalog_get 的 dir/name 非确定性乱码（UTF-16 字节错位重读、
  正确片段与垃圾交错、混入 0x10/0x24 长度前缀），同一 JSON 里 name/dir 一个对一个错
- **原因**：`for (… : BoxCatalog::load().boxes())`——临时 BoxCatalog 在 range-init
  全表达式末尾析构，boxes() 返回成员引用悬垂（C++23 P2718R0 前经典陷阱）；主程序
  BoxModule 具名变量承接故从未发病，domain_tests override 目录未踩中
- **解决**：具名变量承接（a6e2a59），5/5 复测全对。教训：**成员引用 getter 的
  临时对象内联 range-for 一律禁止**；诡异乱码先怀疑内存再怀疑编码（文件 od 完好
  + 乱码码点反推出 id 的 UTF-16 字节是定位决定性线索）

### P-075 强杀 PopTools 后 dsh sidecar 孤儿：mcp-manager respawn 致 exe 占用，链接 LNK1104
- **关联任务**：T-074 ｜ 记录智能体：Agent_20261122011419
- **现象**：taskkill 杀 poptools_mcp_server 后立刻被重新拉起（PID 变化），
  增量构建 LNK1104 无法写 exe，连杀无效
- **原因**：强杀 PopTools（taskkill /F）不给清理机会，dsh sidecar（node
  bin.js web）存活且其 mcp-manager stdio eager 持续 respawn mcp_server 子进程
- **解决**：先杀 sidecar（PowerShell 查 node 命令行认准 tools\dsh 那个，勿误杀
  Zotero 等其他 node）再杀 mcp_server。教训：**重建 mcp_server 前先清 dsh
  sidecar**；「杀不死子进程」= 有父在 respawn，向上找根源

### P-073 dsh-home 双层路径漏算：调试配置写错家 → MCP 配置「不生效」假象
- **关联任务**：T-074 ｜ 记录智能体：Agent_20261121184000
- **现象**：MCP patch 写入 %LOCALAPPDATA%/PopTools/dsh-home 后，手动跑 dsh 能 spawn
  服务器、产品链路（PopTools 拉起）始终不 spawn；两实例 settings/describe 结果不一致
- **原因**：P-067 同款——AppLocalDataLocation 是 org/app 双层（PopTools/PopTools），
  产品 DSH_HOME=…/PopTools/PopTools/dsh-home，而调试 home 用了单层
  …/PopTools/dsh-home，两个家互不相通
- **解决**：patch/AGENTS.md 归位产品双层口径；孤儿测试 home 删除。教训：**调试
  dsh-home 相关配置一律先核对产品实例的实际 DSH_HOME（PopTools/PopTools 双层）**，
  别按记忆的单层路径写

### P-072 隐藏期 winId() 预建原生子窗：Qt 永不同步几何/可见性 → WebView2 黑框
- **关联任务**：T-074 ｜ 记录智能体：Agent_20261121184000
- **现象**：常驻窗隐藏期预初始化 WebView2（winId 预建原生子 HWND），弹出后整块黑——
  GetClientRect 恒 100×30（构造默认）、WS_VISIBLE 未设，而 Qt 控件本身已 1080×720
- **原因**：Qt 对"顶层窗可见前经 winId() 提前 native 化的子控件"不回填原生几何/
  可见性（WA_DontCreateNativeAncestors 更是把父链一并跳过）；controller 边界又只在
  创建回调刷一次，显示后无 resize 不刷新
- **解决**：原生初始化整体推迟到首次显示（openWindow/show 后再 initialize——官方
  示例标准路径）+ showEvent 补 applyBounds + controller 显式 put_IsVisible(TRUE)；
  嵌入层加文件级调试日志（%LOCALAPPDATA%/PopTools/webview2_debug.log，稳定后撤）
  + 启动 12s 自动弹窗钩子免鼠标自证后已撤。教训：**嵌原生子窗（WebView2/Chromium）
  必须等顶层可见后再建，隐藏期预热只做进程/网络层**

### P-070 提交时树无法编译，交接"实测全绿"与提交态不符（P-038 重演）
- **关联任务**：T-070/T-071 ｜ 记录智能体：Agent_20261120005535
- **现象**：拉取 4e153c6 后基线构建即炸——GoodyWindow.h 第二处 `signals:` 段放了成员
  变量、cpp setBusy 签名与函数体间混入旧实现行且体内新旧两版并存、setToolPhase 函数头
  整行重复、文件尾 namespace 收尾重复、maxBubblePaneWidth 有定义无声明
- **原因**：上一会话在最终编辑后未重新构建即提交；交接"ctest 99 绿"对应的是提交前
  某状态（扫描确认损坏范围封闭在 GoodyWindow 两文件内）
- **解决**：T-071 夜战前置修复（5887ee9）。教训重申 P-038：**提交前必须当轮完整构建
  +ctest 复验；引用移交文档"全绿"结论前先复跑**

### P-071 重写区间删除逻辑把"删中段"写成"留尾段"，头部 system 连带丢失
- **关联任务**：T-071 ｜ 记录智能体：Agent_20261120005535
- **现象**：ChatSession trim 批量重写后 3 个会话用例红——system 消息整体消失、截断
  多删一条
- **原因**：新实现只保留 [cutEnd, end) 尾段；原实现语义是删 [blockStart, blockEnd)
  中段、头部 system 前缀原样保留——"删哪段/留哪段"没逐行对齐
- **解决**：保留段补回首块起点之前全部事件（78abd32 前的 fe65610 修正轮）。教训：
  **重写区间增删逻辑必须逐行对齐原实现的保留语义；既有测试就是护栏，改完必跑**

### P-069 删公共 API 前的残留审计漏 tests/：编译在最后一环才炸
- **关联任务**：T-069 ｜ 记录智能体：Agent_20261119172053
- **现象**：删除 ThemeColors::uiThemeColors 后 src/ 全绿，末轮全量构建在
  domain_tests.cpp 连环报错（测试还在断言旧配色约定）
- **原因**：残留审计 grep 只扫了 src/，漏了 tests/——domain_tests 有一个
  uiThemeColors 用例
- **解决**：测试用例迁移到 FluentTokens（断言新 token 关键值）。教训：**删公共
  API 的残留审计必须覆盖全部构建目标（tests/tools/packaging），不只 src/**

### P-068 低级钩子装在主线程：任何主线程停摆都以全系统鼠标瞬移呈现
- **关联任务**：T-068 ｜ 记录智能体：Agent_20261119101500
- **现象**：拖动中鼠标隔一会卡一下再瞬移；关 PopTools 无此现象。30s 壁纸重算 160ms 被感知为"隔一会卡一下"
- **原因**：WH_MOUSE_LL/WH_KEYBOARD_LL 装在 Qt 主线程，RIT 派发钩子要等主线程泵出回调——主线程一停摆全系统输入排队；且等待期 RIT 连 Raw Input 一并停摆，监听侧"冻结期输入仍在到达"的预设判定全程失效（v2 已改"冻结前有输入"）
- **解决**：三处主线程重活全挪离（壁纸异步+签名免重算/截屏 GDI 直出 QImage 进池/月报拼接进池）。教训：**钩子进程的停摆后果比"UI 卡"重一级——主线程任何 100ms 级重活都会顶成全系统输入停摆；新增定时器/重路径必须自查执行线程**

### P-067 监控落盘路径交接记载少一层：实为 PopTools\PopTools 双层嵌套
- **关联任务**：T-066 ｜ 记录智能体：Agent_20261118130500
- **现象**：按交接文档 %LOCALAPPDATA%\PopTools\monitor 查验收日志空手而归（目录不存在），
  监控实际一直在正常落盘
- **原因**：T-045 起记载的路径漏算 AppLocalDataLocation 的 org/app 双层，本机实际为
  %LOCALAPPDATA%\PopTools\PopTools\monitor
- **解决**：ActivityStore.h 注释已按实际勘误。教训：**交接文档里的路径要么当场验证要么
  标注"未复核"**，凭记忆转述的路径会带毒下一棒

### P-066 多屏截图 Pillow ImageGrab all_screens=False 时源 DC 仅主屏，越界读出黑块
- **关联任务**：T-063 ｜ 记录智能体：Agent_20261117164000
- **现象**：双屏截图脚本截副屏全黑，误判"副屏壁纸都没画"；实为抓取工具限制，屏幕内容正常
- **原因**：Pillow `ImageGrab.grab(bbox=…, all_screens=False)` 的源 DC 只有主屏大小，bbox
  超出主屏的部分读到全黑
- **解决**：`all_screens=True` 抓整个虚拟桌面再裁切。教训：**多屏截图验证必须
  all_screens=True**，黑图先怀疑抓取通道再怀疑渲染（temp_scripts/script_20261117_dualscreen_verify.py）

### P-065 审查建议补丁被照抄 → 失实注释/错位修复（夜间批次反复出现）
- **关联任务**：T-062 ｜ 记录智能体：Agent_20261117004800
- **现象**：一个夜间批次内四次——模块1 fixer 照抄 R1 建议文字造成 2 条失实注释；模块2 R2 自己开的 F-01 补丁守错分支；模块6 R1 的 BUG-6 处方前提不成立（beginSelect 先覆写）；模块8 B-1 字面处方两变体都有硬伤
- **原因**：审查代理给的"建议文字/补丁"基于其对代码的当时理解，同样会错；下游代理若放弃独立核验照单全收，错误就从报告渗入代码
- **解决**：流程硬规则「先核对再动手，不符则跳过并说明」+ 终验轮独立复核；四处均被后轮抓住并纠正。教训：**审查报告的补丁是建议不是指令，落地前必须对着代码重推一遍；报告间的分歧（fixer 驳回 R1）要用代码事实裁决而非采信资历**

### P-063 悬停判定降级成「热区内外」布尔 → 格间高亮滞留/迟到
- **关联任务**：T-061 ｜ 记录智能体：Agent_20261117004800
- **现象**：盒内滑过条目，高亮等一会才出现或直接不出现；只有进出整个网格时才刷新
- **原因**：照抄日历「目标变化才重绘」时把目标降级成 onHot 布尔——格 A→格 B 布尔不变不重绘，高亮滞留 A，滑出网格才迟到刷新
- **解决**：悬停按语义目标（格子/头部按钮下标）记录，目标变化才 renderFrame(新旧脏区并集)。教训：**「变化才重绘」必须以会改变画面的语义目标为单位，粗化成布尔就是把丢帧写进逻辑**

### P-064 文字基线硬编码偏移，跨 DPI 叠进图标底缘
- **关联任务**：T-061 ｜ 记录智能体：Agent_20261117004800
- **现象**：150% 下盒内条目标签顶进图标底部（文件夹图标有透明边距看不出，满幅 W 图标贴死）
- **原因**：标签基线 = cell.top + iconSize + 14——漏加图标顶偏移 10，且「文字顶距基线一个 ascent」整个没算，硬编码偏移只在构造它的 DPI 下碰巧成立
- **解决**：基线 = 图标底 + qRound(间距×dpr) + fm.ascent()，并钳文字底不出格子；格子高度按 150% 预算改 112。教训：**文字定位必过 QFontMetrics（ascent/descent），禁硬编码像素偏移**

### P-062 共享位图就地「清屏→重绘」，中间态被 explorer 采样整面板消失一帧
- **关联任务**：T-058 ｜ 记录智能体：Agent_20261116203300
- **现象**：滑过头部按钮日历消失 ~0.02s 再恢复；T-057 脏矩形收窄（P-061）后依旧，证明与失效范围无关
- **原因**：渲染器 `CompositionMode_Source` 全屏清透明后慢慢重画（10~30ms），窗口期位图近全透明；连续渲染重叠时 explorer 的擦除流程恰在此窗口 AlphaBlend，贴出的就是「没有日历」的帧
- **解决**：Qt 改私有暂存帧渲染、`publishFrame` 整帧 memcpy 发布（撕帧窗口 ~20ms→~1ms 且前后帧仅差按钮高亮）；DLL 双缓冲原子擦除兜壁纸中间帧。教训：**跨进程共享的位图禁止就地清屏重绘，必须离屏渲染整帧原子发布**

### P-054 农历权威表手抄入源码 → 整体错位 20 项
- **关联任务**：T-054 ｜ 记录智能体：Agent_20261116003308
- **现象**：手抄 201 项农历压缩表进 ChineseCalendar.cpp，从 1910 项起整体错位（丢一个重复值），单测日/月全错
- **原因**：手工转录大体量数字常量表必然出错——与已归档 P-028 同一坑重踩
- **解决**：权威表文件 → 脚本（temp_scripts/script_gen_lunar_table.js）程序化生成表 + 脚本比对 diff=0 才收工。教训重申：**大常量表永远脚本生成 + 程序化 diff，禁手抄**

### P-055 农历压缩表存 quint16 → 闰大月标志被静默截断
- **关联任务**：T-054 ｜ 记录智能体：Agent_20261116003308
- **现象**：表逐项 diff 全对，2026-11-16 却算成农历十月廿十（+12 天）；12 个闰八月~闰腊月年份全偏
- **原因**：`constexpr quint16 kLunarInfo[201]`——闰八~闰十二月年份表值 ≥ 0x10000（bit16=闰月大小），quint16 静默截断闰月标志（MSVC 未报 narrowing 错误），闰月天数丢失
- **解决**：表改 `quint32` 并加注释；闰月判定类常量表用 `quint32` 起步。教训：**常量数组的位含义超出 16 位时，容器类型按最大位宽选**，narrowing 在 constexpr 数组里不保证报错

### P-056 注入 DLL 动态 CRT（/MD）静默注入失败
- **关联任务**：T-054 ｜ 记录智能体：Agent_20261116003308
- **现象**：SetWindowsHookExW 成功（err=0）但 DLL 从未进 explorer（模块列表无、注入侧日志零行、看门狗每 2s 重复 StartDock）；系统对加载失败的注入 DLL 静默跳过钩子
- **原因**：CMake 默认 /MD 动态 CRT——注入进程内解析 CRT 依赖失败即静默不加载；rlqd 原 build.bat 的 `cl /LD` 默认 /MT 静态 CRT 恰好规避
- **解决**：钩子 DLL 目标钉死 `MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"`；另补 SendMessageTimeout(WM_NULL) 强制触发 WH_CALLWNDPROC（该钩子只拦 SendMessage，原版靠环境消息碰运气）。教训：**注入类 DLL 一律静态 CRT**；注入成功与否以 explorer 侧日志/模块列表为准，钩子句柄非空 ≠ 注入成功

### P-057 7-Zip 24.09 MSI 管理员解压落到 Files/Files 双层路径
- **关联任务**：T-055 ｜ 记录智能体：Agent_20261116165900
- **现象**：install_rapidocr.py 的 msiexec /a 结束后报 7z.exe 不存在；实际解到 temp_scripts/tool/7zip/Files/**Files**/7-Zip/（多一层 Files）
- **原因**：24.09 MSI 目录表自带 Files 目录前缀，脚本 TARGETDIR 又拼了一层 Files；旧版 MSI 布局无此前缀，故搬迁前未触发
- **解决**：ensure_7zip 改为在 TOOLS_7ZIP 下递归 glob 定位 7z.exe（兼容新旧布局），找不到才解压。教训：第三方 MSI 解包落点别按目录表硬编码，定位动态化

### P-058 QDialog::done() 只 hide 不析构，destroyed 不触发致日历永久冻结
- **关联任务**：T-056 ｜ 记录智能体：Agent_20261116165900
- **现象**：点日历设置后日历整体冻结（无 hover 无点击）；取证：窗口队列全活、无线程自旋、无设置窗 HWND、SHM seq 恒定
- **原因**：QDialog 确定/取消/关窗/ESC 走 done() 只 hide——QObject::destroyed 永不触发，宿主 m_modal 卡死 true，nativeEvent 吞掉全部 DLL 事件；五个弹窗（设置/搜索/选日期/信息框）全中，仅带 WA_DeleteOnClose 的菜单/编辑器/覆盖层幸免
- **解决**：改监听 QDialog::finished（done() 必发）+ deleteLater + trackModal（QPointer 对号复位，防菜单「关旧开新」竞态误清）。教训：**hide ≠ destroy，弹窗收尾逻辑禁止只挂 destroyed**；活体取证三板斧（窗口枚举/线程 CPU/SHM seq）比 dump 快

### P-059 无 parent 的 QDialog 默认落「活动屏」，多屏机器开到副屏
- **关联任务**：T-056 ｜ 记录智能体：Agent_20261116165900
- **现象**：点日历设置"毫无反应"（窗实际开在 1920 外的副屏 2632 处），叠加 P-058 后用户感知即"卡死"
- **原因**：无 parent 顶层窗由系统按活动屏放置；本项目弹窗一律 nullptr parent
- **解决**：centerOnPanel 统一锚定日历面板中心（物理÷dpr；QMessageBox show 后尺寸才定，随即再搬）。教训：**多屏环境下无 parent 弹窗必须显式定位到业务屏**

### P-060 后台进程 activateWindow 偶发被前台锁拒绝，靠失焦关窗的浮窗会"关不掉"
- **关联任务**：T-056 ｜ 记录智能体：Agent_20261116165900
- **现象**：日程编辑器双击打开后，点外面有时能关有时不能，无规律
- **原因**：编辑器仅靠 WindowDeactivate 关闭；双击经桌面转发（前台=explorer），后台进程的 activateWindow 受 Windows 前台锁限制时灵时不灵——从未激活过就永不"失焦"
- **解决**：打开编辑器期间装卸共享 WH_MOUSE_LL（GlobalMouseHook，划词卡片先例）：点编辑器外即 saveAndClose，销毁即卸钩。教训：**浮窗"点外关闭"不能只依赖激活态（失焦/Deactivate），须系统级鼠标钩子兜底**

### P-061 注入侧 WM_CLONE_UPDATE 整桌面失效，高频重绘肉眼闪烁
- **关联任务**：T-057 ｜ 记录智能体：Agent_20261116165900
- **现象**：鼠标挪到日历头部按钮区，整个日历闪烁；高频重绘时 explorer 被无效化洪水淹没
- **原因**：InvalidateRect(NULL) 失效整桌面——壁纸重画+全屏混合，违反红线 4（脏矩形）
- **解决**：失效范围收窄到 SHM 头日历矩形 ±24px（覆盖投影 ±18 与发光外扩 ±11），头部缺失兜底全失效。教训：**跨进程位图上屏的失效通知必须带脏矩形，勿整窗**

> 冷坑索引：P-001~P-020、P-024、P-026~P-027 → [handover/archive/pits-001-028.md](handover/archive/pits-001-028.md) ｜ P-008、P-021~P-023、P-025、P-029、P-031~P-035、P-038、P-039、P-046 → [handover/archive/pits-008-046.md](handover/archive/pits-008-046.md)

## 修改台账

> 维护智能体：Agent_20261117004800

> 更早台账（2026-11-01 ~ 11-11）→ [handover/archive/ledger-arch.md](handover/archive/ledger-arch.md)

| 时间 | 智能体 | 板块 | 操作 | 摘要 |
|---|---|---|---|---|
| 2026-11-22 02:20 | Agent_20261122011419 | 头部 / 历史任务 / 台账 / ui calendar 五文件 / core CalendarLayout 两文件 | T-075：摘要入口改道日程编辑器（格内链接撤除+编辑器标题行直弹 notepad） | 构建 0 编译警告 ctest 全绿；3315cfe；未目测 |
| 2026-11-22 01:15 | Agent_20261122011419 | 头部 / 当前任务 / 踩坑记录 / 台账 / docs(验收手册) / mcpserver 两文件 | T-074 收口冲刺：提交态复验+MCP 25 件冒烟 13/13+截图历史 6/6+安装器走查；双缺陷修复（ocr_image 注册+box UAF）a6e2a59；P-074/P-075 入账；验收手册落 docs | 构建 0 编译警告 ctest 全绿；7 项真机验收待用户 |
| 2026-11-22 01:55 | Agent_20261121184000 | 头部 / 当前任务 / 台账 / packaging(iss) / scripts(pack) / pin / app(Box+CaptureHistory+Main) / mcpserver / CMake | T-074 M3：安装器联网装 dsh+截图历史层+收纳盒实时重载；ISCC 编译验证 | 构建 0 编译警告 ctest 54 绿；03ab228；全链路复验（mcp 子进程存活） |
| 2026-11-22 01:05 | Agent_20261121184000 | 头部 / 当前任务 / 台账 / mcpserver / app(Main+Watcher) / goody / settings / main / dsh-home AGENTS.md | T-074 M2.5：域工具 16 件（22 清单）+dirty 跨进程协议+心跳呼吸+单实例+日志撤离 | 构建 0 编译警告 ctest 54 绿；595aa07；打包决策点待用户 |
| 2026-11-22 00:15 | Agent_20261121184000 | 头部 / 当前任务 / 踩坑记录 / 台账 / src(mcpserver) / CMake / tests / dsh-home 配置 | T-074 M2：MCP 服务器六工具+协议框架+dsh patch 注入+AGENTS.md 工作流+2 用例 | 构建 0 编译警告 ctest 54 绿；端到端实证（mcp 子进程存活）；P-073 入账 |
| 2026-11-21 22:45 | Agent_20261121184000 | 头部 / 当前任务 / 台账 / assistant+goody+settings+app 源码 / CMake / tests / dsh-home AGENTS.md | T-074 删除批：旧 Goody 域全量退役+domain_tests 手术+记忆迁移（空）+AGENTS.md 人格基线 | 构建 0 编译警告 ctest 52 用例全绿；bc9abb9+8270ab3 提交 |
| 2026-11-21 21:40 | Agent_20261121184000 | 头部 / 当前任务 / 台账 / settings 源码 / app 模块 / ui 源码 / CMake / 归档 | T-074 M1 融合批：DshWindow 常驻窗+GoodyModule 切路由+设置页收窄+WebView2Loader 部署；存量验收块 T-067~T-072 剪切归档；用户三拍板入账 | 构建 0 编译警告 ctest 99 绿；node 常驻实证待目测 |
| 2026-11-21 21:10 | Agent_20261121184000 | 头部 / 当前任务 / 台账 / scripts / tools / src(ui/dsh+dsh_poc) / CMake | T-074 M1：安装脚本两件+Node/dsh 落 tools+WebView2Host/DshRuntime/dsh_poc 落码+CMake gated 块；侧车冒烟出启动 URL 成功 | dsh_poc.exe 构建 0 编译警告已拉起待目测；interface 宏坑当场修 |
| 2026-11-21 20:05 | Agent_20261121184000 | 头部 / 历史任务 / 当前任务 / 台账 / 需求单 / 设计文档 | T-074 M0：dsh 全量盘点（子代理重下载）+sidecar 定案+三文档落盘；T-073 入历史（UI 否决，行为修复保留） | npm/Node/WebView2 渠道全验证；M1 待开工 |
| 2026-11-21 18:50 | Agent_20261121184000 | 头部 / 当前任务 / 台账 / ui(settings+goody) 源码 | T-073：界面美化精修（设置导航指示条/标题层次/留白 + Goody IME Enter 守卫/空态欢迎卡/行高 150%/消息间距/composer 焦点描边/细滚动条/过程行光环） | 构建 0 编译警告 ctest 99 绿；未提交待目测 |
| 2026-11-21 01:35 | Agent_20261120005535 | 当前任务 / calendar 源码 / 台账 | T-072：日程编辑器撤工具条/页脚+窗高收缩+弹出位置格心居中（f4f0f4f，用户两截图批注） | 构建 0 警告 ctest 99 绿；新版已拉起待目测 |
| 2026-11-20 02:45 | Agent_20261120005535 | 头部 / 当前任务 / 踩坑记录 / 项目快照 / ui+goody+core+settings+monitor 源码 / 需求单 / 归档 | T-071 夜战收口：UI 收编批+日历悬停脏矩形（538598c/78abd32）；新增 P-070/P-071；陈旧验收块无损搬移 archive/current-superseded-1117-1118.md；快照 0.5.1/99 用例 | 6 提交全绿待晨验；推送走代理尝试 |
| 2026-11-20 01:05 | Agent_20261120005535 | 头部 / 当前任务 / 踩坑记录 / goody+core 源码 / 测试 / 需求单 | T-071 夜战立项+P0 修复+批次 A/ChatSession 四项/分散效率批（5887ee9/1bd70c9/fe65610/8fc5236）；新增 P-070 | 用户睡眠授权全权代决；双子代理盘点取证；每提交独立构建+ctest 99 绿 |
| 2026-11-16 10:45 | Agent_20261116003308 | 踩坑记录 / 细档 / hook 源码 / CMake | 用户首次拉起：注入静默失败（P-056）——钩子 DLL 改静态 CRT + SendMessage 强触发；explorer 侧日志确认注入成功（双子类化 + DIB 1920×1080） | 日历首次真实上屏，用户目测进行中 |
| 2026-11-16 16:59 | Agent_20261116165900 | 头部 / 历史任务 / 踩坑记录 / 项目快照 / 修改台账 | 新增 T-055、P-057；快照 scripts 行补全脚本清单并把代理端口对齐 17890 | 净克隆环境重建（tools 三件套 + .venv + build + ctest 96 绿）；脚本双修复未提交待用户 |
| 2026-11-16 17:50 | Agent_20261116165900 | 头部 / 历史任务 / 当前任务 / 踩坑记录 / 修改台账 | 新增 T-056、P-058、P-059；当前任务补 T-056 回归重点 | 日历首轮目测 4 问题修复（表头对齐/今日单框/删账户/弹窗卡死根因+副屏），ctest 96 绿，未提交待用户 |
| 2026-11-16 18:03 | Agent_20261116165900 | 头部 / 历史任务 / 踩坑记录 / 修改台账 | T-056 增补编辑器外点修复；新增 P-060 | GlobalMouseHook 兜底"点外即关"，真实点击三步自验全过，ctest 96 绿，未提交待用户 |
| 2026-11-16 18:42 | Agent_20261116165900 | 头部 / 历史任务 / 当前任务 / 踩坑记录 / 修改台账 | 新增 T-057、P-061；当前任务重写回归重点 | 头部收编 3 按钮+设置页「桌面日历」页+DLL 脏矩形修闪烁；文字锚点修正随 75347e6/23da605 已提交，本轮未提交待用户 |
| 2026-11-16 21:47 | Agent_20261116203300 | 头部 / 历史任务 / 当前任务 / 踩坑记录 / 修改台账 | 新增 T-058、P-062；当前任务 T-057 回归重点 ① 标注已验收 | 悬停按钮整面板闪烁根修（0ada3bb：暂存帧原子发布+DLL 双缓冲擦除），真机验收通过并推送 |
| 2026-11-17 00:55 | Agent_20261117004800 | 头部 / 当前任务 / 踩坑记录 / 修改台账 | 当前任务补录上会话 T-059/T-060 收纳盒概况（此前无交接痕迹）+ T-061 双修；新增 P-063、P-064 | T-061：标签间距根修（kCellH 112 + ascent 基线定位）+ 悬停高亮顺滑化（语义目标重绘 + commitSurface 脏矩形提交），构建+ctest 全绿，未提交待用户真机验收 |
| 2026-11-17 05:30 | Agent_20261117004800 | 头部 / 历史任务 / 当前任务 / 踩坑记录 / 修改台账 | 新增 T-062、P-065；当前任务改写为「T-059~T-061 已提交 + T-062 夜间批次完成，双待真机验收」（含 T-062 行为面修复验收重点六项） | T-062 夜间批次收口：用户睡眠授权（先提交→逐模块卫生→每模块一提交→只审查不视觉验收）；1a9c548 起九提交（模块1~9 各一 refactor 提交 + 本次 docs），串行 R1→F→R2→[F2→R3] 流水线每模块跑到 PASS，审查报告存 temp_scripts/hygiene/ |

| 2026-11-18 00:40 | Agent_20261117164000 | 头部 / 历史任务 / 当前任务 / 踩坑记录 / 修改台账 / 细档 | 合并并行会话：新增 T-063（原本地草稿 T-059 重编）、T-064（原 T-060 重编）、P-066（原 P-063 重编）、细档 t063_calendar_single_screen.md；T-063 特性移植到远端 CLK4 槽位协议架构（虚拟桌面并集画布+双缓冲+覆盖层预览拖动） | 两会话并行撞号合并收口；构建 ctest 96 绿，未提交待真机回归 |
| 2026-11-17 20:10 | Agent_20261117164000 | 头部 / 历史任务 / 当前任务 / 踩坑记录 / 修改台账 / 细档 | 新增 T-059、P-063、细档 t059_calendar_single_screen.md；遗留候修"多屏未自适应"更新为已修；当前任务加 T-059 交互回归清单 | 桌面日历单屏化+多屏花屏/复制根修（协议 v2 + 擦除两遍法 + 跨屏拖动），ctest 96 绿，用户实拍初验通过，未提交待用户 |
| 2026-11-17 22:20 | Agent_20261117164000 | 历史任务 / 当前任务 / 细档 | T-059 增补拖动残影根修：头部 prevRect 字段（失效=prev∪cur）+ 平移快路径 + ~80fps 节流；发现 explorer 驻留旧 DLL 代码坑（日志加 build 标记，需重启 explorer 生效） | 用户报拖动残影 + 要求顺滑，ctest 96 绿，未提交待用户（验证前重启 explorer） |
| 2026-11-17 23:30 | Agent_20261117164000 | 历史任务 / 细档 | T-059 二次增补拖动闪动+不跟手根修：窄失效改 Qt 进程直接跨进程 InvalidateRect（省 explorer 消息泵排队）+ 失效矩形传最近两帧位置包络（容忍擦除异步错位）；DLL 未动，explorer 无需重启 | 用户以其桌面软件开发经验点名两大经典症状，ctest 96 绿，未提交待用户 |
| 2026-11-18 00:20 | Agent_20261117164000 | 历史任务 / 细档 / hook+canvas 源码 | T-059 三次增补（协议 v3 双缓冲帧翻转）：段内两块帧区 + frameIndex 原子翻转根治发布撕裂（~2ms memcpy 窗口被高频擦除采样=面板消失一帧）；失效合并单包围盒（拆两块=擦除追不上失效的正反馈累积）+ 不再失效 SysListView32（图标每帧重绘闪烁）+ 节流 16ms + overlay 平移零重绘；explorer 已重启注入 v3（日志 build 20261117c dblbuf），ctest 96 绿，未提交待用户 | 用户复测仍闪+不跟手+"越拖越明显"，正反馈定位 |
| 2026-11-18 01:10 | Agent_20261117164000 | 历史任务 / 当前任务 / 细档 | T-059 四次增补（跟手根治）：拖动预览画进调整覆盖层（与虚线框同帧同层，Qt 零跨进程延迟），桌面层调整期间隐藏、confirm/cancel 恢复；删除整条桌面实时拖动链路（平移快路径等）；移动模式宿主零参与（尺寸不变不重渲染）＝跟手天花板；DLL 零改动 | 用户复测"不闪了但慢半拍"，跨进程擦除链路物理下限 40~60ms 无法根治，方案换轨；ctest 96 绿，未提交待用户 |
| 2026-11-18 02:00 | Agent_20261117164000 | 历史任务 / 当前任务 / 细档 | T-059 五次增补（窗口移动自激根修）：overlay 拖动中每帧 setGeometry 平移半透明窗口，经过鼠标位置时 Windows 合成新 mouse 事件→事件洪水随拖动时长累积（不跟手+闪烁+误差累积三合一）；修=窗口固定覆盖所在屏（内容局部坐标绘制，拖动零窗口移动，跨屏一次切换）+ onChanged 同屏快路径；DLL 零改动，ctest 96 绿，未提交待用户 | 用户复测"依然不跟手+误差累积式闪烁"定位窗口移动自激；另：用户批评后停止 UI 自动化探针（会动真实鼠标），只改代码验证交用户 |
| 2026-11-18 02:40 | Agent_20261117164000 | 历史任务 / 当前任务 / 细档 / renderer+宿主源码 | T-059 六次增补：确认/取消空窗根修——毛玻璃 WallpaperProvider 宿主持有跨 renderer 共享（缓存丢失重算 100~200ms 是主因）+ render(force) 确认/取消回调立即恢复桌面帧；DLL 零改动，ctest 96 绿，未提交待用户 | 用户确认拖动跟手+不闪 ✓，报 ✓/× 后日历消失一小会 |
| 2026-11-18 03:00 | Agent_20261117164000 | 历史任务 / 当前任务 / 细档 / overlay+宿主源码 | T-059 七次增补：删 ✕ 取消按钮与 onCanceled 路径（用户拍板"拖到哪里算哪里"；该路径有失效缺口缺陷随删除蒸发）、✓ 移至右上角、Esc=就地确认、onConfirmed 追加全失效兜底；DLL 零改动，ctest 96 绿，未提交待用户 | 用户确认拖动跟手不闪 ✓✓，报 ✕ 后日历消失+框选才显示 |
| 2026-11-18 03:30 | Agent_20261117164000 | 历史任务 / 当前任务 / 细档 / overlay 源码 | T-059 八次增补：跨屏拖动卡死根修——窗口切屏时局部坐标原点跳变 ±1920px 致位移反向突变、面板边界抖动死锁；位移改全局坐标差值（globalPosition - m_startGlobal）与窗口位置解耦；DLL 零改动，ctest 96 绿，未提交待用户 | 用户报只能单屏内拖动 |
| 2026-11-17 16:45 | Agent_20261117164000 | 头部 / 当前任务 / 修改台账 / update 源码 | 新增 T-060 在途块（更新自查实现+bump 0.5.0+打包）；提交 f6dde5a（T-059）与 3a460a6（T-060）本地待推送；代理 17890 不通，push/Release 待用户开代理 | 用户指令：提交推送+实现更新自查+发 Release；gh 缺失需 token 或手动上传 |
| 2026-11-18 10:22 | Agent_20261118102200 | 头部 / 当前任务 / 修改台账 / hotkey 源码 | 新增 T-065 在途块；头部时间更新 | F1/F3 裸键热键最高优先级（WH_KEYBOARD_LL 优先钩子+RegisterHotKey 兜底，仅 core/hotkey 两文件），构建 ctest 96 绿，未提交待真机验收 |
| 2026-11-18 10:55 | Agent_20261118102200 | 当前任务 / 修改台账 / temp_scripts | T-065 增补自证结果：探针双相位（抢键方赢→PopTools 链首吞键）+ 隔离实验（覆盖层真实弹出稳定不自关）+ F3 贴图链路全过；新坑候选：冻结画布截图肉眼不可辨，验覆盖层以窗口枚举为准 | 用户指令"拉起项目验证"，四项 PASS 全绿，PopTools 留运行待真机复验 |
| 2026-11-18 13:12 | Agent_20261118130500 | 头部 / 历史任务 / 当前任务 / 修改台账 / 需求单 | T-065 结算（用户验收+提交 5287349 入历史任务）；T-066 立项（TSF 文本镜像输入监听，需求单 requirements/20261118-tsf-text-mirror.md）；本机 SDK/IME 侦察 | 用户批注拍板 TSF 路线+字母流完全替换+立即开工；调研子代理进行中 |
| 2026-11-18 13:55 | Agent_20261118130500 | 头部 / 当前任务 / 需求单 / 细档 | T-066 调研收口：新增细档 t066_tsf_research.md（六问结论+设计启示）；需求单开放点回填（被动加载死刑/签名无门槛/HKLM 必须/宿主盲区）；当前任务改写为三选一路线待用户定；AGENTS.md 增「下载与逆向纪律（所有 Agent 通用）」+子代理传路径规则（用户三次批注校准） | weasel 本机实证（装→查→卸→基线比对一致，探针存 temp_scripts/weasel_probe/）；被动 TSF 三诉求结构性冲突实锤 |
| 2026-11-18 15:21 | Agent_20261118130500 | 头部 / 历史任务 / 当前任务 / 项目快照 / 踩坑记录 / monitor+settings+模块源码 / 测试 | T-066 结算入历史任务（feat 提交：镜像上线+字母流完全替换）；当前任务留 T-066b 候修；快照勘误（版本 0.5.0/ctest 95/monitor 描述）；新增 P-067 落盘路径勘误坑 | 真机验收达标（退格原文/拼音→汉字 replace 实证）；诊断定位 RuntimeId 重建吞增量并以同窗口续接修复；KeyboardTextComposer+打码开关全链移除 |
| 2026-11-18 16:00 | Agent_20261118130500 | 头部 / 当前任务 / assistant+calendar+ui+模块源码 / AppServices / CMake / 测试 / 需求单 | T-067 三阶段落码：聚合器【输入原文摘录】段（T-066b 收口）+ DailyRecordJob 起居注刷新 + MonthlyRecordJob 新建 + GoodyModule 接线（静默化移除成文气泡）+ AppServices 双槽/信号 + 日历链接全链（布局热区/渲染/面板分发/钩子豁免/notepad）；探查代理绘接入点地图（后台） | 需求单四拍板（起居注静默/双击面板后可点/月报开机滚动/系统 notepad）；ctest 97 绿（+2 用例），未提交待真机验收 |
| 2026-11-18 14:12 | Agent_20261118130500 | 头部 / 当前任务 / monitor 源码 / 模块源码 / CMake / 测试 | T-066 UIA 文本镜像落码：新增 TextMirrorMonitor（影子差分+churn 护栏+密码影子重置）、UiaTextReader 增 RuntimeId、ClipboardListener 增 clipboardUpdated 信号、模块接线三触发源+textlog.jsonl；新用例入 domain_tests（97 用例全绿） | 未提交待真机验收（验收清单在当前任务块）；字母流移除排验收后 |
| 2026-11-19 10:16 | Agent_20261119101500 | 头部 / 当前任务 / monitor+hotkey 源码 / MainWindow / CMake / 需求单 / temp_scripts | T-068 立项并部署取证桩：MainThreadProbe 探针（三钩子 RAII 打点+主线程心跳+worker 落盘）+ 系统侧 Raw Input/光标/CPU 被动监听；构建 ctest 100% 绿，双路日志上线并录得启动期 380ms 泵停摆首条线索 | 用户报拖动卡顿瞬移；不动鼠标纯被动取证，跨会话待日志回查；取证代码未提交 |
| 2026-11-19 12:32 | Agent_20261119101500 | 头部 / 当前任务 / 踩坑记录 / wallpaper+capture+monitor+assistant 源码 / temp_scripts | T-068 定案+根治：根因三类主线程停摆（30s 壁纸重算 160-170ms 主犯 / IME 快照截屏 / 开机月报拼接 380ms）；三处全挪离主线程（签名免重算+GDI 直出 QImage 进池+聚合进池），ctest 100% 绿；探针验证 30s 停摆归零；新增 P-068 | 用户报"刚刚就卡了"触发日志回查，30s 严格周期定位主犯；修复未提交待真机验收 |
| 2026-11-19 16:35 | Agent_20261119101500 | 头部 / 历史任务 / 当前任务 / 细档 / 版本三处 / dist | T-068 结算入历史任务（9ffa3b4 fix + 2cb237b bump，tag v0.5.1 已推）；细档 t068_mouse_stall.md；发版 GitHub Release id 451052105 + 上传安装包 56MB（旧 0.5.0 包已清）；perf 基线追加（启动 324ms/主进程 159MB，快照待复核） | 用户指令提交+推送+发版；探针随验收期保留在版本内，体感确认后删探针收口 |
| 2026-11-19 17:20 | Agent_20261119172053 | 头部 / 当前任务 / 项目快照 / 踩坑记录 / ui 全域源码 / CMake / main / 测试 / 需求单+设计文档 | T-069 UI 现代化改版 M0-M3 落码：theming 新库（FluentTokens/QSS 生成器/窗口 chrome）+设置界面重写（SettingCard/ToggleSwitch/即时生效去 OK/Cancel）+Goody 头像行/卡片描边/自适应输入+暗色小窗收编+OcrPanel/TranslateCard/悬浮球接 token+ThemeColors 退役；domain_tests 主题用例迁移；新增 P-069 | 网调（双子代理代码盘点+开源方案调研）→用户三拍板→方案确认后落码；构建+ctest 全绿，未提交待用户目测验收；需求单 requirements/20261119-ui-fluent-restyle.md，方案 docs/design/20261119-ui-fluent-restyle-proposal.md |
