# T-054 细档：桌面日历照搬移植（E:\rlqd → Qt）+ 日历/收纳盒痕迹清零（Agent_20261116003308）

> 需求单：requirements/20261116-rlqd-calendar-port.md。参照实现 E:\rlqd\clone（DesktopCal 3.33 行为复刻，用户认定已实现核心需求，**严禁改动原目录**——本任务只读参照 + 复制皮肤贴图）。

## 0. 清零阶段（先于移植，独立提交）

- 删除：旧日历（T-042/T-043/T-051/T-052 系列）、2026-11-15 YYNote 在途实现、收纳盒（T-044）、desktopband 全部代码/设置键/设置页/测试；交接文档主档/细档/归档三层去痕（删 T-044/T-048/T-053 条目与 P-036/P-037/P-028 坑及相关台账行）。
- 保留：CrashReporter（通用崩溃自诊）、DesktopStack Ball 层（Goody 悬浮球）、P-031（贴底通用教训）、坑 P-029/P-046。
- ctest 89 用例全绿（108→89，日历/收纳盒/desktopband 用例随代码清除）。

## 1. 移植架构（照搬 rlqd，跨进程标识改 PopTools 命名）

**注入 DLL** `src/hook/calendar_hook.cpp` → `PopToolsCalendarHook.dll`（CMake 目标 poptools_calendar_hook，纯 Win32，user32/gdi32/comctl32/msimg32）：
- 导出 `StartDock(hostWnd)`：OpenFileMapping 写 hostWnd → 定位 ProgMan→SHELLDLL_DefView→SysListView32 → 对 explorer 桌面线程 `SetWindowsHookExW(WH_CALLWNDPROC)`（本 DLL 随钩子注入）→ PostMessage WM_NULL 触发；`StopDock()`：摘钩子 + WM_APP+0x102 通知框架窗 CleanupExplorer。
- explorer 内：子类化 SHELLDLL_DefView（WM_ERASEBKGND 先 DefSubclassProc 画壁纸再 AlphaBlend 共享位图；WM_NOTIFY 转发 NM_CLICK/NM_DBLCLK + LVM_HITTEST 图标标记）与 SysListView32（WM_ERASEBKGND 返 1 保持透明；鼠标 move/down/up/dblclk 转发）；框架窗 1s 定时器 = explorer 重启自愈。
- 协议：SHM `Local\PopToolsCalendarShared_v1`（4096 头 + magic CLK1/width/height/ready/hostWnd/rect/seq）；WM_APP+0x101（刷新）/0x102（停止）/0x103（事件：EV_MOVE/DOWN/UP/DBLCLK/LEAVE，wParam 低 16 位事件+高 16 位图标标记，lParam 屏幕物理坐标）。日志 %TEMP%\PopToolsCalendarHook.log。

**Qt 应用侧** `src/ui/calendar/`：
- `CalendarSharedCanvas`：CreateFileMapping(命名)+CreateDIBSection(**hSection 绑共享段**, offset 4096)——QImage(Format_ARGB32_Premultiplied) 直接包住 DIB 位内存，QPainter 画即共享段写，explorer 直接 AlphaBlend 读（预乘 alpha 匹配 AC_SRC_ALPHA）。
- `CalendarRenderer`：GDI+ → QPainter 逐项移植（阴影字=偏移 1px 双绘；九宫格贴图 drawImage 九份；毛玻璃=壁纸模糊图+黑 182 叠加+顶部 220px 渐变；表头/格子/网格线/周数金条/头部七图标/引导气泡/调整模式装饰）；字体 SimSun→YaHei UI 兜底，px = pt×4/3×dpr。
- `WallpaperProvider`：注册表 WallPaper → TranscodedWallpaper → CachedFiles → Spotlight（>200KB 且 ≥1024×600 取最新）候选；WallpaperStyle 铺排（0 适应/2 拉伸/10 填充/tile）；1/4 降采样 + 双向 BoxBlur(r=10, 2 遍，逐字节照搬) → SmoothTransformation 放大；20s 缓存，30s 定时刷新。
- `CalendarHostWidget`（HostForm 移植）：屏外 1x1 隐藏窗（Qt::Tool|Frameless，WA_ShowWithoutActivating）承载 DLL 事件（`nativeEvent` 收 0x8103）；QLibrary 从 exe 目录加载 DLL；三定时器 watchdog 2s / clock 1s（跨日重绘）/ wallpaper 30s；悬停提示（头部七钮+引导气泡）；单击 ≤4px 抖动判定→翻页/菜单/设置/插件/账户；双击日期开编辑器；拖拽缩放 = 调整模式覆盖层。
- 五窗：CalendarMenuForm（自绘弹层 Popup，勾选项/分隔线/失焦即关）、CalendarEventEditor（无边框圆角深色编辑器，一行一条，ESC/Ctrl+S/失焦保存，工具条五钮后四项提示占位）、CalendarSettingsForm（五开关+字号 70-160%）、CalendarSearchForm+CalendarDatePickerForm、CalendarAdjustOverlay（分层透明覆盖层：14px 边缘缩放/内部移动/✓×，最小 520×420，Esc/Enter）。
- `CalendarModule`：init 只装配（宿主轻创建 + singleShot 1200ms 后 startDuty——画布/注入/壁纸模糊是重活，性能红线 6）；托盘四项（显示/调整/设置/停用-启用）；`shutdown()` 必须 StopDock（否则 explorer 残留子类与日历位图）。

## 2. 坐标约定（本项目首次引入，后续改动必看）

- **布局/命中/DLL 事件/画布一律物理像素**（explorer 在物理像素上 AlphaBlend，150% DPI 下主屏 1920×1080 物理）；settings.json 的 X/Y/W/H 也存物理像素（与 rlqd 一致）。
- Qt 原生弹窗定位/覆盖层几何 = 物理 ÷ dpr；CalendarAdjustOverlay 内部全逻辑坐标，回调边界 ×dpr 换回物理。
- 主屏几何仅启动时取一次：DPR 变更/多屏自适应未做（已知限制）。

## 3. 农历权威链（P-028 教训的重演与规避）

代理端口未开 → 改走国内源：npmmirror 直连下载 `solarlunar 3.1.0`（jjonline/calendar.js 同源算法与表）→ `temp_scripts/out/lunarInfo201.txt`（1900-2100 共 201 项）→ `temp_scripts/script_gen_lunar_table.js` **脚本生成** C++ 表（手抄必错位，本次实际踩过：首版手抄丢 20 项整体错位）→ `temp_scripts/script_gen_lunar_truth.js` 生成 2020-2030 逐日真值 → 单测锚点：春节 2020/2024/2025/2026/2027/2028、中秋 2025/2026、端午 2024/2025/2026、除夕 2025/2026、元宵 2025、闰四月 2020-05-23 / 闰二月 2023-03-22 / 闰六月 2025-07-25、2026 全部 12 个月首、2026 全部 24 节气（rlqd 分钟级公式逐日比对）、ISO 周数。

## 4. 数据与打包

- `%LOCALAPPDATA%\PopTools\calendar\settings.json + tasks.json`（JSON 与 rlqd 逐字段同构；tasks 兼容旧格式 `{"All":[{"Key","Value"}]}`；`CalendarSettings::setDataDirOverride` 供单测重定向）。
- 皮肤贴图（9Grid 三张 PNG）复制入 `src/resources/calendar/` 走 qrc，不依赖外部文件；DLL 由 CMake 输出到 build/Release 与 exe 同目录（Inno 打包整目录自动带上）。
- 消息通道/SHM/窗口标识已改 PopTools 命名——**与 rlqd 复刻版/原版 DesktopCal 不可同时运行**（都子类化同两个桌面窗口，后注入者覆盖前者上屏）。

## 5. 已知限制与验收状态

- 视觉与 C# 版存在细微渲染差（Qt/GDI+ 字体度量与 ClearType 差异），布局/配色/交互逐项同构。
- 年范围 1901-2100；Explorer 重启 2s 自愈（watchdog）；单屏（主屏）。
- 自动化：ctest **96 用例全绿**（+7 日历用例）；DLL LoadLibrary 冒烟 + 导出函数齐全；未做 explorer 真机注入自测（P-031 规矩：AI 不做自主验收，渲染/交互留用户真机目测）。
