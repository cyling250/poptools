# 已提交待验收任务块归档（2026-11-18 ~ 2026-11-21 原文剪切）

> 从主档「当前任务」区原文剪切搬移（无损，未改写）。这批任务均已提交、
> 待真机/目测验收；验收时按本册原文的验收清单执行。
> 剪切时间：2026-11-21 21:40 · Agent_20261121184000

**T-072 日程编辑器按用户反馈双修（已提交 f4f0f4f，待目测验收，2026-11-21）**：
- 用户两截图批注：①编辑器底部五图标（插时间/重复/提醒/地点/颜色，后四项 rlqd
  占位）语义不明+150% DPI 显示不全+无提示，「数据保存在本机」页脚 → 两块全撤
  （唯一有功能的"插时间"一并撤，用户拍板），窗高 430→340，残留审计零残留；
  ②弹出位置原钉格子左上角 → 改以所点日期格心居中（屏钳制不变，T-062 G-02
  物理口径不变）
- 构建零警告 ctest 99 绿；新版已拉起（用户目测：双击日期格看位置与编辑器形态）

**T-071 夜间批次：全面 UI 收编 + 代码效率优化（6 提交 5887ee9..78abd32，待真机验收，2026-11-20 凌晨）**：
- 需求单 requirements/20261120-ui-efficiency-nightshift.md（用户睡眠授权全权代决"自己完成"；
  双子代理盘点证据存 temp_scripts/night_20261120/；各提交正文即技术细节档）
- **5887ee9 P0 前置**：修复 T-070 提交态编译损坏（GoodyWindow.h signals: 段成员变量/
  setBusy 新旧混行/setToolPhase+namespace 重复/缺声明）——4e153c6 起树无法编译（P-070）
- **1bd70c9 行为修复**：ProcessRow 补写 goodySummary 属性（主题切换清空过程行摘要真 bug）；
  日历编辑器删每键整窗 update()
- **fe65610 ChatSession 四项**（v3 格式与外部行为不变）：投影增量化（旧每事件全量重投影
  O(n²)）/ trim 批量定前缀+单次重投影 / 侧栏计数缓存 / 防抖落盘挪 IO 池（save 同步语义
  保留）
- **8fc5236 分散效率批**：壁纸 blurredWallpaper 只回缓存（T-068 漏网点同步重算 ~160ms）
  +两面板 kickWallpaperRefresh（duty 启动即踢）/ Goody 侧栏 200ms 合帧 / reasoning 光标
  增量 / SSE 读偏移 / Settings beginBatch 批量 sync / 键盘钩子 QMetaMethod 预解析 /
  渲染器 QFontMetrics+删除线字体缓存
- **538598c UI 收编批**（零视觉重设计）：散写橙三处→overlay::warmAccent()；文字基线
  ascent 锚定（P-064 口径）；编辑器/ESC 胶囊接深套 token；FluentTheme.h 纪律注定案三类
  豁免（GoodyTheme 表/内容惯例色/回退渐变）
- **78abd32 日历悬停脏矩形**：renderFrame 增 commitDirty，悬停级改动免整屏发布（BoxPanel
  先例，红线④收尾）；onLeave 补复位两链接悬停标记（原漏复位）
- **质量线**：每提交独立构建 0 警告+ctest 99/99 绿
- **晨验清单**：①Goody 对话/工具/审批/主题切换（过程行摘要不清空/侧栏计数正常）②duty
  开启 ~0.2s 内出壁纸底、换壁纸 30s 内跟上③日历悬停不闪不残留（含重进同一链接）④设置
  页拨开关即时生效仍正常⑤ESC 胶囊/调整覆盖层文字不漂
- **不修项（含理由）**：日历/盒渲染器配色重收敛（视觉已定稿待验收，验收后立专项）；全局
  弹窗 palette（与 P-023 钉扎交互）；截图画布拖选脏矩形（核心交互无法夜验）；工具结果
  摘要 fromJson（低频）；气泡逐条 setStyleSheet（P-023 刻意为之）
- 坑：P-070 ｜ P-071

**T-070 Goody 按 deepseek-harness 形式重构（已提交 4e153c6，待真机验收，2026-11-19）**：
- 需求单 requirements/20261119-goody-dsh-restyle.md（用户拍板"就按照 dsh 的来"全权委托；
  dsh=deepseek-ai/deepseek-harness 官方 agent harness，源码已下载解压
  temp_scripts/dl_dsh/src/，MIT 许可可借鉴；双子代理研读报告：后端 agent-loop/
  会话事件日志、前端 WebUI 布局规格）
- 已落码（15 文件 +1184/-488，ctest 99 绿，提交 4e153c6）：
  ①后端 **ChatSession v3「会话即事件日志」**：事件词表 turn/start|end(system/user/
  assistant/tool result/approval asked|decided（审计对 fail-closed），模型历史由
  **投影派生**（"模型可见即已记录"），**消息块化**（text/reasoning/tool_call 块，
  arguments 保原始 JSON 串）；v1/v2 无损迁移；trim 按完整回合块淘汰（不拆
  tool_call/result 对，dsh compaction 切点纪律）；四件套按投影消息下标截断事件
  ②LlmClient：assistant 消息携 blocks+finish（finish_reason 判据）
  ③GoodyModule loop：turn 边界事件化（completed/max-tokens/aborted/error/blocked
  五种收束；max-tokens 粘性——截断的 tool_calls 不执行补错误 result）；审批
  asked/decided 审计事件
  ④前端 **dsh WebUI 形态**：GoodyTheme token（neutral-bluish 色板/deepseek 蓝/
  圆角 8-24/字号 11-15），**气泡→文档流**（user 淡蓝 20px 圆角泡右对齐/assistant
  全宽无框无头像——M2 头像行撤除），**ProcessRow 过程行**（状态徽章点+工具名+
  摘要折叠行，替代 ⚙/✓✗ 文本行，GoodyModule 经 toolRowStarted/Finished 接入），
  composer 大圆角卡（自适应 44~200px），发送/停止同位钮（↑/■），侧栏 280，
  消息列 880 居中，审批卡 warning 描边
- 待真机验收：①旧会话数据无损迁移（v2 文件载入新格式）②对话/工具调用审批链
  视觉（淡蓝气泡/过程行徽章/审批卡）③深浅主题切换 ④悬浮球/侧栏多会话不回归
- 诚实边界：①Cordis 插件体系/HMR/compaction LLM 摘要器不搬（单应用不需要，
  超限裁剪为回合块淘汰）②消息时间戳仍未实现（schema 之外）③过程行深浅切换
  徽章色不重抛（finish 态色深浅同值可接受）④代码块 banner（语言标签）未做，
  悬浮复制钮保留
- 坑：**临时 QJsonArray range-for 是 UB 陷阱**（"断组"过滤失效/starts 幻影 -1
  全由此起——Qt 容器临时对象 range-for 悬垂），全部改具名变量遍历；**QtTest
  -maxwarnings 默认 2000 条**，海量 qWarning 插桩会静默吞掉后续输出（探针失败
  误判"未执行"），调试用 QFAIL 携带信息

**T-069b 托盘菜单精简 + F1 卡顿打桩（已提交 8ebd1cd，验收期监控中，2026-11-19）**：
- 需求单 requirements/20261119-tray-menu-f1-probe.md（用户报三件事：①托盘菜单拉起
  时 F1 卡顿 ②偶发 F1 卡顿无法复现要打桩监控 ③菜单精简到 设置/关于/退出）
- 已落码：①**打桩**：MainThreadProbe 增 mark(tag) 通用链路标记（全量落盘
  probe-*.jsonl 的 k:"mark" 行，QPC/tick/前台PID）；F1 全链路五点 f1_hotkey→
  f1_start→cap_begin→cap_grabbed（ScreenCapturer::capture 抓屏分段）→cap_shown
  + 托盘菜单 tray_menu_show/hide（MainWindow aboutToShow/Hide）——用户日常用
  数天后按日志定位（与钩子延迟/泵卡顿记录同钟对齐）；②**菜单精简**：五模块九项
  托盘入口全撤（Capture/ActivityMonitor/Goody/Calendar×4/Box×2），m_trayAction
  成员与文案更新链路同删；AppServices 新槽 calendarAdjust/calendarDutyStopped/
  calendarSetDuty/goodyOpenChat/boxOpenManager/boxAnyVisible/boxSetVisible
  （各模块 shutdown 撤槽防悬垂）；设置页补等价入口：通用页「收纳盒」组（管理+
  显示开关）、日历页「启用桌面日历」总开关（duty 语义）+「调整桌面日历位置和
  大小」按钮（关闭设置页让路）、Goody 页「打开 Goody 管家」卡
- 待办（**2026-11-20 首批实测已入账**）：F1 五段链路三次采样——抓屏段
  （cap_begin→cap_grabbed，ScreenCapturer::capture 多屏拼图）稳定 43~52ms
  为延迟主段，热键分发 <2ms、建窗显示 ~7ms；与托盘菜单动画竞争时叠加为
  可感知卡顿（用户报的 BUG 机理吻合）。**用户拍板"继续服役再看"**：抓屏
  挪 worker 优化挂起，打桩继续攒数据；另录得一次 299ms 后台泵停摆
  （fg=ZCode，~20:00，根因未定位，mark 未覆盖后台场景——复现再扩打点）。
  回查入口：%LOCALAPPDATA%\PopTools\PopTools\probe\probe-*.jsonl
- 代决：「启用桌面日历」=desktopSurface duty 语义（重启不记忆，同原托盘行为）；
  mark 截断 31 字符全量落盘；收纳盒显隐=任一可见→全隐藏

**T-069 UI 现代化改版 Win11 Fluent（已提交 2cbd47d，待用户目测验收，2026-11-19）**：
- 需求单 requirements/20261119-ui-fluent-restyle.md（用户三拍板：Win11 Fluent 风/全部
  界面整体改版/方案确认后落码；D1 设置去 OK/Cancel 即时生效、D2 主色保留品牌蓝
  #2B6FF2、D3 贴桌面面板保留 12px 圆角——均已按默认拍板实施）；方案与选型
  docs/design/20261119-ui-fluent-restyle-proposal.md（选型结论：零新依赖自研
  Fluent 视觉层，Mica 直调 DWM；开源 C++ Fluent 库均 2026 新库未采）
- 已落码（M0-M3 全流程，构建+ctest 全绿）：①M0 底座：src/ui/theming/FluentTheme
  （FluentTokens/fluentTheme 明暗双套+fluentInput/Button/ScrollBarQss 生成器+
  applyFluentWindowChrome 深色标题栏+Mica 尝试；ui/theming 新静态库
  poptools_ui_theming 全功能库 PUBLIC 链接）+游离暗色小窗收编（CalendarSearchForm/
  CalendarDatePicker/BoxManagerDialog 接 token 随明暗切换；CalendarEventEditor/
  capture ToolBar 常暗语境钉深色 token）+main.cpp 全局字体栈（Segoe UI Variable→
  Segoe UI→雅黑）；②M1 设置界面：SettingsDialog 全重写（SettingCard 分组卡片+
  ToggleSwitch 自研 Win11 开关+左导航胶囊选中+页内滚动+Mica/深浅标题栏+
  即时生效：控件变更即写 Settings 并 notifySettingsChanged 广播，文本框 400ms
  防抖 done 冲刷，MainWindow Accepted 后统一广播移除）；③M2 Goody：气泡改
  Fluent 卡片（bgCard+cardStroke 描边+8px 圆角）+头像行（GoodyAvatar 自绘：
  assistant "G" 玻璃球/user "我" 圆牌，逐帧取 token 免重抛）+输入框 1~6 行自适应
  （44~120px）+token 对齐（气泡/侧栏/输入区/权限条）；④M3：OcrPanel/TranslateCard/
  GoodyFloatingBall 接 token；ThemeColors.h 退役（只余 overlay 语义色+
  systemSchemeDark，struct/uiThemeColors 删除）；domain_tests 主题用例迁移
  FluentTokens；capture ToolBar/TextEditingController 字面量收编
- 待用户目测验收：设置界面（导航/卡片/开关/即时生效）/Goody（气泡描边/头像/输入
  自适应）/日历搜索/选日期/收纳盒管理（浅色系统下不再恒暗）/截图工具条/悬浮球/
  OcrPanel/TranslateCard 明暗切换
- 诚实边界：①Mica 在 Qt 不透明 backing store 下不可见，实际呈现=Mica 回退固色
  （官方等值观感），真云母需 WA_TranslucentBackground（对话风险未做）；②消息逐条
  时间戳暂缓（ChatSession 消息无逐条 ts 字段，需动存储 schema，超视觉改版边界）；
  ③导航无图标（暂无合适 qrc 图标源，文字导航已可用）；④Win10 无 Segoe UI Variable
  自动回退 Segoe UI+雅黑
- 红线：样式一律取 fluentTheme() 禁止窗口内写十六进制字面量（半透明玻璃描边除外，
  注释声明）；新窗口主题接法见 FluentTheme.h 头注释

**T-067 Goody 起居注 + 日历摘要展示（已提交 d3c926d，待真机验收，2026-11-18）**：
- 需求单 requirements/20261118-goody-calendar-summary.md（用户四拍板：起居注静默自主、
  日链接双击开面板后才可点、月报开机滚动重算【月报更新】往月冻结仅对话触发、notepad）
  范围界定：起居注只是 Goody 一项职责，其对话/工具/记忆/权限/MCP 角色不变
- 已落码：①DailyAggregator 新增【输入原文摘录】段（textlog 按窗分组：insert/replace 进
  正文、delete 计删字"删N字"）+ isPendingDay 扩 textlog（T-066b 就此收口）；②
  DailyRecordJob::maybeRefreshToday（当日五流字节指纹 + 60min 节流 + 首次成文优先，
  GoodyModule 20min 心跳驱动，全程静默）；③MonthlyRecordJob（开机重写当月滚动月报，
  提示词含【月报更新】，goody/monthly/<yyyy-MM>.md，整月 MD 拼接 24k 预算）+ 对话工具
  app.generate_monthly_record（重算任意月，无按钮）；④AppServices goodyDailyDigestPath/
  goodyMonthlyDigestPath 查询槽 + goodySummariesChanged 信号；⑤日历：CalendarLayout
  月报链接(monthLink)/格内摘要条带(cellSummaryLinkRect) 热区、Renderer 链接绘制（激活
  高亮/悬停底色/日程行让位一行）、CalendarPanel 编辑器开启期间链接分发（eventsAccepted
  前置 pre-gate）+ 外点钩子豁免（点链接不关面板，UP 走面板路径防双弹）+ notepad 弹出
  （md 原文写 %TEMP%）+ CalendarModule 槽注入与信号接线；⑥22:00 成文的托盘气泡移除
  （起居注纪律）
- 待真机验收（PopTools 拉起新构建）：①打字后等心跳/重启 → textlog 进日摘要；②有摘要
  日期格显示「»摘要」链接；③双击开面板 → 点该日链接 → notepad 弹当日摘要；④未双击时
  点链接无效；⑤左上角「MM月工作摘要»」点击弹月报；⑥对 Goody 说"重新生成 10 月月报"→
  goody/monthly/2026-10.md 更新
- 诚实边界：格内链接条带固定像素不随 fontScale（对齐 headerButtons 先例）；notepad 抢
  焦点会使编辑器失焦关闭（WindowDeactivate 既有链路，交互可接受）
