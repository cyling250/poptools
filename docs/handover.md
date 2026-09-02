# 项目交接文档

> 本文档由 AI 维护，用于任务交接。任务完成后写入「历史任务」，未完成任务见「当前任务」。
> 最后更新：2026-09-02

## 项目快照

- **项目名称**：Snipaste（前身为 MySnipaste，2026-08-24 更名）
- **一句话描述**：Windows 全场景截图工具——区域截图 + 本地离线 OCR（PaddleOCR）+ 大模型翻译（OpenAI 兼容 API）+ 贴图，Snipaste 免费替代方案
- **当前版本**：0.1.3。版本号在两处需同步改：`CMakeLists.txt` 的 `project(... VERSION ...)` 与 `packaging/Snipaste.iss` 的 `#define MyAppVersion`
- **技术栈**：
  - C++17，代码统一在 `snipaste` 命名空间
  - Qt 6.8.3 LTS（LGPL 动态链接，本地免安装套件 `tools/qt/6.8.3/msvc2022_64`，不依赖全局 Qt）
  - CMake + MSVC（VS 2022，VS 2026 亦可，见坑 P-001）
  - OCR：PaddleOCR-json v1.4.1（QProcess 常驻子进程管道模式：stdin 发图片路径，stdout 读 JSON；初始化约 0.6s，之后约 200ms/张；其自带 opencv_world4100.dll 随 paddleocr/ 目录部署）
  - 翻译：大模型 API 双协议（设置 `translate/apiFormat`）——OpenAI 兼容 `/chat/completions` + Bearer（默认 DeepSeek，可换 Ollama 等）与 Anthropic `/v1/messages` + `x-api-key`（DeepSeek 兼容端点 / 官方 Claude）；`QNetworkAccessManager` 实现；请求显式关闭思考模式（`thinking` 字段，端点不支持时 400/422 降级重发），响应做 `<think>` 兜底清洗；提示词注入用户配置的翻译领域与专有名词术语表
  - 窗口/控件识别：Win32 窗口枚举（user32 + dwmapi）+ Windows UI Automation（UIA 控件树，系统组件无第三方依赖），实现见 `core/capture/SmartRectDetector`
  - 热键：Win32 `RegisterHotKey`（user32），经 `core/hotkey/GlobalHotKey` 封装（nativeEventFilter）
  - 设置持久化：QSettings 写 exe 同目录 `Snipaste.ini`，单例 `core/settings/Settings`
- **架构概览**（三层分层，core 与 ui 各为静态库，依赖方向 ui → core）：
  - `app/MainWindow`（QObject，非窗体）＝应用控制器：系统托盘 + 三个全局热键（F1 截图 / F3 贴图 / Ctrl+Alt+Shift+X 划词翻译）+ 服务编排
  - 截图主流程：`CaptureWindow`（全屏画布式编辑，内部按控制器拆分：SelectionController / AnnotationEditor / ToolDrawingController / TextEditingController / OcrController / OutputController）→ OutputController 产出到剪贴板 → ResultDialog 查看或 PinWindow 贴图
  - OCR 流程：OcrController → `PaddleOcrService`（子进程）→ 识别文本以文本框覆盖画布，或打开 `OcrPanel` 文字栏
  - 翻译流程：`Translator`（大模型 API，提示词=基础指令+领域/术语表）→ OcrPanel（截图翻译）或 `TranslateCard`（划词翻译卡片，全局仅一张）；`SelectionTranslator` 负责模拟 Ctrl+C 抓取选中文本；目标语言在两处入口共享并持久化（`translate/targetLang`），切换即记忆；源语言不记忆，每次新任务重置为自动检测；语言显示名↔代码映射统一在 `core/translation/Languages.h`
  - 标注数据模型与渲染在 `core/annotation`（AnnotationCanvas / AnnotationItem / AnnotationPainter），UI 工具在 `ui/capture/tools`（ToolBase + ToolFactory + 各工具类）
- **目录含义**：
  - `src/main.cpp` — 程序入口
  - `src/app/` — 应用控制器（托盘 + 热键 + 服务编排）
  - `src/core/` — 核心域静态库 `snipaste_core`（annotation 标注模型与渲染、capture 多屏捕获与窗口检测、hotkey 全局热键与全局鼠标钩子、ocr OCR 服务、settings 设置、translation 翻译与划词翻译）
  - `src/ui/` — 界面层静态库 `snipaste_ui`（capture 截图画布+工具栏+OCR 面板、pin 贴图窗口、result 结果面板、settings 设置对话框、translate 划词翻译卡片）
  - `src/resources/` — qrc 资源与应用/工具图标（svg）
  - `scripts/` — 第三方依赖一键下载脚本（install_qt.py / install_paddleocr.py），走本机代理 `127.0.0.1:7897`
  - `tools/` — 第三方绿色套件（qt / paddleocr），不入库，由脚本生成
  - `packaging/` — Inno Setup 6 安装包脚本 `Snipaste.iss`，打包源为 `build/Release` 整目录
  - `dist/` — 安装包输出（已忽略；现存 0.1.2 / 0.1.3 两版 Setup exe）
  - `temp_scripts/` — 一次性脚本与中间产物（不入库）
  - `build/` — CMake 构建输出（已忽略；当前 `build/Release/` 有完整可运行产物，含 Qt 运行库、paddleocr/ 引擎）
- **常用命令**：
  - 安装依赖（新机器必做，需代理 127.0.0.1:7897）：`python scripts/install_qt.py`、`python scripts/install_paddleocr.py`
  - 配置：`cmake -S . -B build -G "Visual Studio 17 2022" -A x64`
  - 构建：`cmake --build build --config Release`（POST_BUILD 自动执行 windeployqt、拷贝整个 paddleocr/ 目录）
  - 运行：`build/Release/Snipaste.exe`
  - 打包：`ISCC.exe packaging\Snipaste.iss`（输出 `dist/Snipaste-Setup-<版本>.exe`）
  - Python 环境：项目根 `.venv`（Python 3.12.14，uv 管理）
- **当前状态**：T-017（工具栏可维护性重构 + 调色盘移第二行 + OCR 扫描图标）已完成，Release 构建通过、用户真机验证通过；无自动化测试、无 CI；OCR 引擎缺失时不阻断构建（运行时提示不可用）

## 历史任务

> 按任务组织（不是按对话），最新的在最上面。编号从 T-001 递增，永不复用。
> 注：本节由 git 提交历史（2026-08-06 ~ 2026-08-24，共 19 个提交）在交接文档首次创建时整理而成。

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

## 踩坑记录

> 每条坑必须链接到产生它的历史任务条目。

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
