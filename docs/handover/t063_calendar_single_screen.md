# T-063 细档

> ⚠ 并行会话合并说明（2026-11-18）：本细档成文于本地独立会话（当时协议独立演化为「v2 所在屏矩形 + prevRect + frameIndex 双缓冲」）。合并时远端另一会话已把架构升级为「CLK3/4 槽位协议 + DesktopSharedCanvas/Stack/SurfaceHost/CalendarPanel 重组」，本地特性已按下文方案移植到远端架构：帧翻转保留（协议头 frameIndex@CLK4）、screenRect/prevRect 由「槽矩形 + 远端每槽 prev 失效」天然取代、擦除两遍法改为槽位并集版、画布升格虚拟桌面并集。文中「协议 v2/v3」表述为本地演化史，最终落地见远端 desktop_protocol.h（CLK4）。

：桌面日历单屏化 + 多屏花屏/复制根修（协议 v2）

> 执行智能体：Agent_20261117164000 ｜ 状态：✅ 代码落地 + 真机目测初验通过（用户实拍副屏），未提交待交互回归
> 关联：T-054（注入机制）/ T-058（原子发布与双缓冲）｜ 新坑：P-066

## 1. 用户问题与根因（本机实测）

用户双屏扩展（1920×1080 ×2，均 100% DPI，虚拟桌面 (0,0)-(3840,1080)）：
- **日历复制成两份**：两块屏同一相对位置各出现一份日历；
- **副屏花屏**：副屏日历文字/格子乱码碎片，主屏完好；
- 需求：整个桌面只出现一份日历、完整归属一个屏幕、支持拖动（含跨屏）与大小调整。

实测拓扑（temp_scripts/script_20261117_monitor_topology.py）：多屏下
`Progman / SHELLDLL_DefView / SysListView32` 窗口矩形**横跨整个虚拟桌面 (0,0)-(3840,1080)**，
而共享位图只按主屏 1920×1080 建。explorer 擦除桌面是**按区域分块**进行的，每块擦除 DC 带
各自视区原点（org）；旧代码把日历位图固定 blend 在「当前 DC 逻辑 (0,0)」：
- 擦到副屏区域（org=(-1920,0) 等价映射）→ 日历原样贴到副屏同位置 → **复制**；
- 擦除区域是不与整屏对齐的条带 → 位图按条带原点错位贴出碎片 → **花屏**；
- 主屏 org 恰为 (0,0) → 永远正确。

## 2. 修复方案

### 2.1 协议 v2（共享头 40B → 56B，段名 `Local\PopToolsCalendarShared_v2`）
头部追加 `screenLeft/Top/Right/Bottom`：日历所在屏矩形（虚拟桌面物理坐标）。
- DLL 侧 `GetScreenClientRect()`：头部矩形 − DefView 客户原点（ClientToScreen）→ 客户区坐标；
  头部未携带时兜底取客户原点所在显示器（MonitorFromPoint）。
- 应用侧 `CalendarSharedCanvas::setScreenRect()`，每次渲染后与 `setCalendarRect`（虚拟坐标）一起写。

### 2.2 DLL 擦除两遍法（calendar_hook.cpp DefViewProc WM_ERASEBKGND）
坐标约定：dc 逻辑 L → 客户 D = L + org；bg 位图像素 = 客户 − 屏左上；bg 视区原点取 `org − 屏左上`。
1. `GetClipBox`（逻辑）+org → 设备；与所在屏矩形求交；**不相交 → `return DefSubclassProc(dc)` 纯系统擦除，严禁贴日历**（复制/花屏根因处）；
2. 相交 → 第一遍：`SaveDC` + `ExtSelectClipRgn(dc, 屏矩形, RGN_DIFF)` 让**屏外区域走系统原生擦除** + `RestoreDC`；
3. 第二遍：bg 双缓冲（clip=交区−屏左上，壁纸经 DefSubclassProc(bgDC) 按 bg 视区原点落位）→
   `BlendCalendar(bg, 屏左上−org)` → 一次 `BitBlt(dc, 屏左上−org, ...)` 上屏（保持 T-058 原子性）。
   直画兜底路径同样限定屏矩形。`WM_CLONE_UPDATE`：脏矩形换算客户坐标；`wParam!=0` = 全失效（清旧屏残影）。

### 2.3 Qt 宿主坐标体系（CalendarHostWidget）
- 设置 x/y/width/height 升级为**虚拟桌面物理像素**（对既有用户无感：主屏原点 (0,0) 与旧语义重合）。
- `enumNativeScreens()/resolveScreenForRect()`：QScreen 逻辑中心 → MonitorFromPoint → 原生矩形；
  **矩形中心落在哪块屏就归属哪块屏**，其次最大重叠，最后回退主屏（拔屏自愈）。
- `clampSettings()`：钳制在归属屏内（520×420 最小）；`applyScreenIfNeeded()`：所在屏/分辨率/DPR
  变化 → 画布+渲染器按该屏物理像素重建并重设 hostWnd（画布重建清零头部）。
- 渲染：传给渲染器的 settings 副本**平移到画布本地坐标**（−屏左上）；渲染器内部写的面板矩形
  （本地）渲染后由宿主改回**虚拟坐标**；DLL 事件坐标（虚拟）在 nativeEvent 减屏左上后进命中测试；
  `logicalPos()` 改为所在屏原点感知（弹窗/编辑器定位）。
- 看门狗（2s）追加屏拓扑自愈：重解析所在屏，变化即重建 + `setScreenRect` + startDock + render。
- render() 发布后比对「上次发布的屏/矩形」，变化（跨屏/改尺寸）→ `WM_CLONE_UPDATE wParam=1` 全失效。

### 2.4 跨屏拖动（CalendarAdjustOverlay 原点感知 + 虚拟桌面钳制）
- 覆盖层构造增加 `screenLogical` 参数：`nativeToLogical/logicalToNative` 按屏原点平移换算
  （主屏原点 (0,0) 时与旧 `divRect/mulRect` 语义重合）；回调交还虚拟桌面物理坐标。
- 宿主 `enterAdjustMode`：拖动范围 = 所有屏物理并集（覆盖层窗口盖全部屏）；onChanged 回调链
  `clampSettings → applyScreenIfNeeded → renderDuringAdjust`——**矩形中心跨过屏幕边界即整体
  跳到该屏并重建画布**（跨屏瞬间一次，壁纸缓存丢失致单帧卡顿可接受）；确认/取消路径同样处理
  （取消可能已跨屏，回 original 所在屏）。

## 4. 拖动残影根修 + 顺滑化（2026-11-17 晚，用户反馈后追加）
用户实测：拖动后旧位置留残影，隔一会才消失。根因：拖动中每帧只失效了**新位置**，
旧位置要等确认后的全失效 / 30s 壁纸定时器兜底才被擦掉。
- **协议头再加 prevRect 字段**（虚拟坐标，段名仍 v2）：DLL `WM_CLONE_UPDATE` 失效范围
  = prev ∪ cur（各 ±24px，prev==cur 时只失效 cur）。
- **平移快路径**：拖动中面板内容不变 → doRenderDuringAdjust 直接把共享位图里的上一帧
  `CompositionMode_Source` 平移画进暂存帧（免毛玻璃/文字/九宫格整面板重绘）；尺寸变化或
  基准失效（跨屏重建/正常渲染后）回退全渲染。源是共享位图（上一帧完整内容，只读不写），
  无撕帧窗口。
- **节流**：renderDuringAdjust 与上次发布 <12ms 时合并，QTimer::singleShot 补末帧（~80fps 上限）。
- **旧代码驻留坑**：explorer 已加载同路径 DLL 时，重编译后 LoadLibrary 返回旧模块——
  explorer 端 mapped 日志行带 `(build 20261117b)` 才是新代码；否则需重启 explorer
  （PopTools watchdog 2s 内自动重注入）。

## 5. 拖动闪动+不跟手三轮迭代（用户以其桌面软件开发经验点名两大经典症状）
**一轮（残影消失后复测：闪动+不跟手）**：
- 闪动 = 失效提交与 explorer 实际擦除异步错位（擦除时面板又移了几帧，固定新旧两
  矩形的边缘漏出壁纸抖动）→ 失效矩形改传**最近两帧位置包络**（m_envPrevB ∪ 当前）。
- 不跟手 = `PostMessage→explorer 消息泵排队→处理消息才失效→排队擦除` 两段队列滞后
  → 窄失效改 **Qt 进程直接跨进程 `InvalidateRect`**（user32 全局 API，照搬 UI 自动化
  手段；findDesktopSurfaces() 找 Progman→DefView→ListView，WorkerW 兜底），仅窗口
  找不到时回退 PostMessage。WM_CLONE_UPDATE 保留全失效（wParam=1）用途；DLL 端
  prev∪cur 窄分支保留作兜底路径。

**二轮（单包围盒 + 图标免疫 + 协议 v3 双缓冲，解决"闪"与"越拖越明显"）**：
- **擦除负载×2 正反馈**：每帧失效 prev/cur 两块 → explorer 每帧两次全套擦除
  （壁纸重绘+合成+上屏 ~10ms/次），追不上 60fps 失效速率 → 未擦区域逐帧累积 →
  越拖越卡越闪。修：失效合并**单个包围盒**（每帧 1 次擦除）。
- **图标窗口被殃及**：每帧失效 SysListView32 → 区域内桌面图标每帧强制重绘 →
  图标高频闪烁越积越广。修：只失效 DefView，图标零重绘。
- **发布撕裂根治（协议 v3 双缓冲帧翻转）**：单缓冲整帧 memcpy ~2ms 窗口被高频
  擦除采样 = 面板消失一帧（每秒数次"开始拖动就不停闪"的主因）。段名升
  `_v3`，段内两块帧区 + 头部 frameIndex：应用整帧写后台缓冲后原子翻转索引，
  DLL BlendCalendar 按索引取前台帧——读侧任何时刻都是完整帧，撕裂窗口为零。
- **配套**：节流 16ms（与单包围盒擦除容量对齐）。
- **验证标记**：日志 `dib created x2 ... (build 20261117c dblbuf)` = v3 双缓冲版
  已注入（协议换段名，老 DLL 读不到 v3 段会退化为纯系统擦除，需重启 explorer）。

**三轮（"不闪了但慢鼠标半拍"→ 跟手根治）**：跨进程擦除链路（渲染→发布→explorer
擦除→DWM 合成）物理下限 40~60ms，永远追不上鼠标——快拖 2000px/s 视觉滞后近百像素。
方案换轨：**拖动期间日历预览直接画进调整覆盖层**（与虚线框同帧同层，Qt 绘制零跨进程
延迟），桌面层日历隐藏：
- enterAdjustMode：`hideDesktopCalendar()`（清透明帧发布+全失效）→ 渲染预览帧
  `m_adjustPreview`（面板+四周 32px 阴影/发光余量）→ overlay 构造注入 previewProvider。
- overlay paintEvent：黑罩 → 预览 drawImage（对齐 inner 左上收进 margin）→ 虚线框。
  移动模式宿主零参与（尺寸不变则不重渲染预览，overlay 平移绘制）＝跟手天花板；
  缩放模式尺寸变化才节流重渲染（16ms+补帧）。
- onChanged：settings+clamp+applyScreenIfNeeded（跨屏归属/重建画布，预览帧与画布
  尺寸无关）+updateAdjustPreview(false)；onConfirmed/onCanceled 同理；overlay
  destroyed → m_adjusting=false → render() 恢复桌面帧（隐藏帧的恢复保障，所有退出
  路径都汇聚在此）。
- 删除整条桌面实时拖动链路（renderDuringAdjust/doRenderDuringAdjust/平移快路径/
  m_lastAdjustRect）；render() 正常路径的窄失效（包络+直接 InvalidateRect）保留。
- 预览渲染复用 renderer（画进预览帧，面板位于帧内 (32,32)）；renderer 会污染头部
  日历矩形为预览坐标，画完恢复真实虚拟坐标（桌面层隐藏期间无害）。
- DLL 零改动（无需重启 explorer）。

**四轮（"依然不跟手+误差累积式闪烁"→ 窗口移动自激根修）**：overlay 拖动中每帧
`setGeometry` 平移半透明窗口，窗口经过鼠标位置时 Windows 合成新 mouse 事件喂回队列
→ 事件洪水随拖动时长累积（虚线框/预览一起滞后=不跟手、回放抖动=闪烁、越拖越明显
——与用户观察到的"误差累积"完全吻合）：
- **窗口固定化**：overlay 覆盖 inner 中心所在的屏（构造按屏列表选初始屏），拖动中
  窗口零移动，虚线框/预览作为窗口内容以局部坐标绘制；仅 inner 中心跨屏时切屏一次
  （grabMouse 的 SetCapture 在 SetWindowPos 后保持，拖动不中断）。
- **onChanged 同屏快路径**：中心仍在所在屏 native rect 内 → 跳过 clampSettings/
  applyScreenIfNeeded（省掉每 mouseMove 一次的显示器枚举）；跨屏才走完整归属流程。

**七轮（"只能单屏内拖动，跨屏拖不动"→ 窗口切屏坐标基准跳变）**：窗口固定化后，拖动
位移用窗口局部坐标（`event->pos() - m_startLocal`）计算——面板中心跨屏 → overlay
切屏（窗口原点跳变 ±1920px）→ 下一帧局部坐标瞬间反向突变 → 面板被钳回旧屏 → 窗口
切回 → **边界抖动死锁，永远跨不过去**。修：位移改用**全局坐标差值**
（`globalPosition - m_startGlobal`，与窗口位置解耦），跨屏连续；m_startLocal 移除。

**五轮（"拖动跟手+不闪 ✓，但点 ✓/× 后日历消失一小会才显示"）**：确认/取消恢复链路
跨屏（或 renderer 重建）时 `WallpaperProvider` 跟着重建 → 毛玻璃缓存丢失 → render()
重新做「磁盘读壁纸+缩放+双向 boxBlur」（主线程 100~200ms），期间桌面处于隐藏帧 = 空窗。
- **毛玻璃 provider 宿主持有跨 renderer 共享**：`m_wallpaper`（尺寸匹配时复用缓存；
  尺寸变化才重建 provider）→ renderer 构造加可选 `sharedWallpaper` 非拥有指针。
- **确认/取消回调末尾 `render(true)`**：render 加 force 参数跳过 m_adjusting 守卫，
  overlay close 前桌面帧已发布（黑罩半透明，日历透过可见），关闭瞬间即完整显示。

**六轮（产品简化 + ✕ 路径缺陷蒸发）**：用户拍板"拖到哪里算哪里，撤销无意义"→
**删除 ✕ 按钮与 onCanceled 路径**（该路径存在"日历消失+框选才补绘"的失效缺口，
未完全定位即随删除蒸发）；✓ 移至虚线框右上角；Esc 改为就地确认（与 ✓/Enter 同义）；
onConfirmed 在 render(true) 后追加**一次全失效兜底**（窄失效区域如有遗漏即被全桌面
重绘补齐，一次性开销无闪烁）。

## 6. 验证记录
- 构建 Release 全绿；ctest 96/96 绿（2026-11-17）。
- 真机截图自验：主屏置位 → 主屏正常、副屏干净无花屏无复制（calendar_verify_primary2_*.png）。
- 副屏置位试验（settings.json X=2450,Y=100 + 重启）：**用户实拍确认副屏日历渲染良好、主屏无日历**。
- 注入日志 `%TEMP%\PopToolsCalendarHook.log`：v3 段 mapped、subclass ok=1/1、
  `dib created x2`（双缓冲）、ready=1；轮次验证标记：mapped 行 `(build 20261117b)`、
  dib 行 `(build 20261117c dblbuf)`。

## 7. 已知边界与注意
- **混 DPI**（双屏不同缩放比）公式上支持（原点感知换算），未实测；本机双屏同 DPI。
- **taskkill 强杀 PopTools** 不走 StopDock：explorer 内残留旧 DLL 子类实例。旧实例读不到 v1 段
  （应用已用 v2）走「协议不可用→纯系统擦除」，且新 DLL 在子类链上层命中即 return，实测无干扰；
  要彻底清干净可重启 explorer。
- **试验遗留**：settings.json 现停在副屏 (2450,100)（试验位）。用户可用日历头部「调整」按钮跨屏拖回。
- 副屏壁纸毛玻璃为近似（WallpaperProvider 按壁纸图+铺满样式渲染该屏尺寸，未按每屏裁剪相位），
  视觉可接受，与主屏同机制。
- 截图验证脚本：temp_scripts/script_20261117_dualscreen_verify.py（Pillow 必须 all_screens=True，
  见 P-066）。
