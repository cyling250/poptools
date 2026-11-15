# 📋 需求单：桌面日历重写（照搬 E:\rlqd → Qt）+ 日历/收纳盒痕迹清零

**一句话目标**：清零本项目"桌面日历 + 桌面收纳盒"全部代码/文件/交接文档痕迹（含 2026-11-15 YYNote 在途实现），随后照搬 E:\rlqd（DesktopCal 3.33 行为复刻，已验证实现核心需求）的实现方式，移植为 Qt 版桌面日历。

**场景**：项目开发（Windows 桌面 Qt 应用）。

**交付物**：
1. 清零提交：core/calendar、ui/calendar、core/desktopwallpaper、ui/shared/DesktopEmbedHost、CalendarModule、YYNote 需求单、设置键/设置页、测试用例、README、交接文档中全部日历+收纳盒痕迹。
2. 注入 DLL `calendar_hook`：desktop_hook.cpp 机制照搬（WH_CALLWNDPROC 注入 explorer、子类化 SHELLDLL_DefView/SysListView32、WM_ERASEBKGND AlphaBlend 共享位图），纳入 CMake 构建。
3. Qt 应用侧：共享内存画布、QPainter 渲染器（毛玻璃/月格/农历/节气/节日/日程/今日高亮）、布局与命中测试、农历节气引擎、任务与设置 JSON 存储、壁纸读取+模糊、宿主控制器（注入启动/消息/Explorer 重启自愈/托盘）；日程编辑器、主菜单、日历设置、调整位置和大小、内容搜索五窗。
4. CalendarModule 模块接入（托盘入口）；ctest 单测（农历、布局命中、JSON 往返）；README 与交接文档更新。

**关键约束**：严禁改动 E:\rlqd 任何内容；C++17 / Qt 6.8.3 / MSVC；ctest 全绿；构建前结束 PopTools.exe 与 RapidOCR-json.exe。

**验收标准**：日历位于壁纸之上/图标之下；右键出桌面菜单、框选正常、图标可拖过日历；双击日期空白开当日日程编辑器；Explorer 重启 2s 内自愈；ctest 全绿；用户真机目测通过。

**AI代决假设**：
1. 不重写 git 历史（历史提交含旧代码属既成事实），只清工作区与文档并正常提交（清零、移植各一提交）。
2. CrashReporter 保留（通用崩溃自诊基建，非日历专属）；坑 P-031 保留（Goody 悬浮球仍依赖该贴底教训）。
3. 数据落 `%LOCALAPPDATA%\PopTools\calendar\`（settings.json + tasks.json），JSON 字段与 rlqd 完全同构并兼容旧 `All` 格式。
4. 日程 JSON 与 rlqd 同构，但消息通道/共享内存/窗口标识等跨进程协议字符串改为 PopTools 命名（DLL 与应用侧一致即可）。
5. 菜单中 rlqd 未实现的项（主题皮肤/打印/账户/云同步等）保留入口按原版行为提示"未实现"。
6. 旧 %LOCALAPPDATA% 下历史日历数据文件不动（用户数据红线）。

**下游衔接**：直接执行。
