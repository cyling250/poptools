# UI 现代化改版设计方案（Win11 Fluent）

> 2026-11-19 起草 · 依据：需求单 requirements/20261119-ui-fluent-restyle.md
> 用户拍板：Win11 Fluent 风 / 全部界面整体改版 / 方案确认后落码
> 事实底稿：代码盘点 + 网络调研（GitHub API / Microsoft Learn 官方文档，2026-11-19 快照）

## 一、现状诊断（丑在哪 / 乱在哪）

| # | 问题 | 证据 |
|---|---|---|
| 1 | **样式碎片化**：约 43 处内联 setStyleSheet、0 个 .qss 文件，主题应用逻辑有 5 份相似实现（SettingsDialog / GoodyWindow / MessageBubble / OcrPanel / TranslateCard 各一份 applyTheme） | 改一处全局观感要动五个地方，改版最大的重复债 |
| 2 | **token 半成品**：ThemeColors.h 只有 22 个语义色，无字体/圆角/间距刻度；圆角 5~12px、字号 12/13px 散落硬编码；主色 #2B6FF2 在 ToolBar.cpp:195 等处重复硬编码 | ThemeColors.h 注释自述最初只为翻译浮窗服务 |
| 3 | **游离暗色小窗**：CalendarSearchForm（#2D3038/#181A20）、BoxManagerDialog（#23252d/#3a3d47）、CalendarEventEditor、capture ToolBar 硬编码深色十六进制，不接主题表、不随明暗切换 | 浅色系统下这些小窗仍是突兀深色块 |
| 4 | **两套视觉气质并存**：桌面自绘件走"深色玻璃+毛玻璃壁纸"家族，对话框走标准控件+QSS，无统一语言 | 七三开，无体系 |
| 5 | **设置界面是"表单堆"**：QFormLayout 标签右对齐传统表单 + 加粗标题 + QFrame 分隔线，信息密度高但无卡片层级、无呼吸感；底部 OK/Cancel 按钮是老对话框语义 | SettingsDialog.cpp:58-97、178-254 |
| 6 | **Goody 对话架构先进但视觉欠打磨**：流式+Markdown+代码高亮+思考折叠+hover 工具条都在，但配色平淡、无头像/时间戳、代码块无语言标签、输入框定高 64px | MessageBubble.cpp、GoodyWindow.cpp:145-158 |

## 二、候选开源方案对比与选型结论

### 对比表（C++/Qt Widgets 可用）

| 方案 | License | 成熟度 | 结论 |
|---|---|---|---|
| QFluentWidgets C++ 官方版 | **闭源付费 ~$599** | 组件最广（170+） | ✗ 个人免费项目不投钱 |
| Fluent-Qt（calvinhxx） | MIT | 277★，2026-05 创建，当日仍在提交 | △ 太年轻，无 Mica |
| QtFluentWidgets（Type3limit） | MIT | 92★，2026-02 创建，无 Releases | △ 社区极小 |
| Qt-Fluent-Widgets（Fairy-Oracle 移植） | **GPL-3.0** | 95★，组件最全（含 Mica/SettingCard） | ✗ GPL 传染，闭源分发不可用 |
| QSS 模板（Breeze/QDark） | MIT | 663★/3088★，久经考验 | △ 只能改控件皮，Fluent 上限低 |
| QGoodWindow | MIT | Win11 Mica/深色标题栏 | △ 整库引入只为一个 DWM 调用，不值 |
| QML/QQuickWidget 混合 | — | GPT4All 77.3k★ 走 QML | ✗ 混合渲染有性能/焦点代价，重写代价最大 |

### 选型结论：**零新依赖，自研轻量 Fluent 视觉层**

1. 三个 C++ Fluent 库全是 2026 上半年新生儿（最高 277★），做地基太险；GPL 版功能最全但传染。
2. poptools 已有现成底座：ThemeColors 语义色板（明暗双套）+ P-022/P-023 样式纪律 + 强自绘能力（BoxRenderer/CalendarRenderer/悬浮球全自绘），自研边际成本低。
3. Mica 直接调 Win32 官方 API `DwmSetWindowAttribute(DWMWA_SYSTEMBACKDROP_TYPE)`，约 20 行，不需要 QGoodWindow 整库。
4. 聊天渲染现有架构（AutoHeightBrowser + setMarkdown）底层正是调研推荐的最优解（Qt 内置 md4c），**架构不动，只做视觉升级**；代码高亮已有自研 GoodyCodeHighlighter，无需引 KSyntaxHighlighting。
5. SettingCard 卡片组件自研（QWidget 组合，人天级），落自家 ui 库可控。
6. 风险对冲：万一自研 SettingCard/卡片体系工作量失控，备选回退是引入 MIT 的 Fluent-Qt 补组件，接口层预留即可。

## 三、设计 token 规范（ThemeColors.h 扩展）

### 圆角（Win11 Geometry 官方规范）

| token | 值 | 用途 |
|---|---|---|
| radiusOverlay | **8px** | 窗口、对话框、卡片容器、弹层 |
| radiusControl | **4px** | 按钮、输入框、下拉、复选、滚动条 |
| radiusPanel | **12px（保留）** | 贴桌面自绘面板（日历/收纳盒/Goody 玻璃卡）——signature 圆角，区别于窗内控件 |

替换现有散落的 5/6/10px；Goody 卡片 12px 与 radiusPanel 对齐。

### 间距（4px 网格）

| token | 值 | 用途 |
|---|---|---|
| spaceXS / spaceS / spaceM / spaceL | 8 / 12 / 16 / 24 | 控件间 / 标签与控件·卡片组间 / 边缘留白·卡内边距 / 分区间 |

### 字体刻度（Win11 Typography 官方 ramp；强调用 Semibold 不用 Bold）

| token | 字号/行高 | 字重 | 用途 |
|---|---|---|---|
| fontCaption | 12/16 | Regular | 辅助说明、时间戳 |
| fontBody | 14/20 | Regular | 正文、控件 |
| fontBodyStrong | 14/20 | Semibold 600 | 强调、卡片标题 |
| fontBodyLarge | 18/24 | Regular | 页面引言 |
| fontSubtitle | 20/28 | Semibold 600 | 页标题 |

字体栈：`"Segoe UI Variable Text", "Segoe UI", "Microsoft YaHei UI"`（西文数字走 Segoe，中文回退雅黑）。

### 色板（Win11 官方值 + 品牌主色保留）

| token | 浅色 | 深色 |
|---|---|---|
| bgBase（窗口基底/Mica 回退） | #F3F3F3 | #202020 |
| bgCard | 白 70%（#B3FFFFFF） | #2A2A2A |
| bgCardHover | 白 50% | #313131 |
| cardStroke（1px 描边） | 黑 6% | 白 8% |
| textPrimary | 黑 89% | #FFFFFF |
| textSecondary | 黑 62% | 白 77% |
| textTertiary | 黑 45% | 白 53% |
| textDisabled | 黑 36% | 白 36% |
| accent（品牌主色保留 #2B6FF2） | #2B6FF2 | #5B93FF（深色下提亮一档） |
| accentHover / accentPressed | 提亮/压暗一档 | 提亮/压暗一档 |
| divider | 黑 8% | 白 8% |

文本四档透明度、明暗双套直接落 ThemeColors（沿用 uiThemeColors(dark) 模式）。

### Mica 云母

- 长驻窗口（设置对话框）启用：`DWMWA_SYSTEMBACKDROP_TYPE = 2`（Win11 22H2+），配 `DWMWA_USE_IMMERSIVE_DARK_MODE` 深色标题栏；22000 老版用 undocumented attr 1010 兜底。
- 回退链：Win10 / 关透明 / 省电 → 固色 bgBase。
- 半透明玻璃卡窗（Goody）维持现有 WA_TranslucentBackground 自绘路线，Mica 只给原生框架窗用。

### 明暗主题

跟随系统 colorScheme（现有机制保留），全 token 双套。

## 四、分界面改造清单

### M0 底座（全局基建）

1. **theming 模块**（src/ui/theming/ 或 ThemeColors.h 扩展）：tokens（色/圆角/间距/字体）+ 通用控件 QSS 生成器（按钮/输入/下拉/复选/滚动条，圆角 4px、hover/pressed 态、Win11 式 focus ring）+ `applyMica(QWidget*)` 助手。
2. **5 份 applyTheme 收拢**：各窗口薄壳化，改调统一 QSS 生成器（P-022/P-023 纪律不变）。
3. **游离暗色小窗收编**：CalendarSearchForm / CalendarEventEditor / BoxManagerDialog / capture ToolBar 接 token，随明暗切换。

### M1 设置界面（样板间，风格基线 gate）

- 左导航升级：图标+文字、选中态 accent 左指示条+浅色底、项高对齐 Win11 NavigationView；宽度自适应。
- 内容区 QFormLayout 表单 → **自研 SettingCard 卡片组**：每卡"图标 + 标题 + 一行描述 + 右侧即时控件"，卡片 radiusControl、hover 微底色、组间 spaceM；展开式子项缩进对齐 Win11 Expander。
- 底部 OK/Cancel → 移除（设置项本就即时生效），仅留标题栏关闭。【默认拍板 D1，可改回保留】
- 窗口启用 Mica 基底 + 深浅跟随。
- 主色：保留品牌 #2B6FF2，不跟随系统 accent。【默认拍板 D2，可加"跟随系统"设置项延后做】
- （可选）顶部设置搜索框，即时过滤跳转——延后到 M3。

### M2 Goody 对话界面（架构不动，视觉升级）

- 气泡：用户侧保留 accent 实底，圆角对齐 token + 微阴影；assistant 卡片用 bgCard + 1px cardStroke 描边（Fluent 卡片特征）。
- 头像：assistant 用悬浮球同款"G"玻璃球小图（品牌呼应），user 用首字母圆标；时间戳 fontCaption 挂气泡外侧。
- 输入区：64px 定高 → 1~6 行自适应高度；placeholder 提示；发送按钮 accent 常驻+图标。
- 代码块：header 栏（语言标签 + 复制钮）+ 深底圆角容器，现代 AI 聊天标配。
- 思考折叠区 / hover 工具条 / 会话侧栏条目态全部对齐 token。
- 窗口玻璃卡圆角与边框色对齐 radiusPanel / cardStroke。

### M3 铺开收尾

- 日历面板 / 收纳盒 / 悬浮球 / 贴图窗 / 截图覆盖层：自绘家族统一取新 token（色板值替换、圆角对齐 radiusPanel，玻璃质感保留）。
- OcrPanel / TranslateCard 改调统一 applyTheme。
- （可选）设置搜索框。
- 全局验收：明暗切换、主色一致性、无游离硬编码色。

## 五、分期与验收

| 期 | 内容 | 验收 gate |
|---|---|---|
| M0 | theming 底座 + 5 份 applyTheme 收拢 + 暗色小窗收编 | 构建 ctest 绿 + 用户目测无回归 |
| M1 | 设置界面改版（SettingCard + Mica） | **用户目测认可风格基线后才铺开后续**（AI 不做自主验收） |
| M2 | Goody 对话视觉升级 | 用户目测 |
| M3 | 其余界面统一 + 清理 | 用户目测全局统一 |

## 六、风险

1. Mica 仅 Win11 22H2+，回退链已定义（固色基底）。
2. QSS 对原生控件 hover/focus 的平台差异——沿用 P-023 双通道钉扎。
3. 自研 SettingCard 人天级可控；失控备选：MIT 的 Fluent-Qt 补组件。
4. 自绘件动色板需重渲染验证，但 ThemeColors 已接入它们，改动是值替换非结构改。
