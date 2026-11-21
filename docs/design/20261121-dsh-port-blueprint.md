# DeepSeek Harness 全量照搬蓝图（T-074 · M0）

> 状态：M0 定稿 2026-11-21 · Agent_20261121184000
> 配套：需求单 requirements/20261121-dsh-full-port.md ·
> 盘点报告 docs/design/20261121-dsh-inventory-report.md ·
> dsh 源码副本 temp_scripts/dl_dsh_20261121/（不入库）
> 方向锁定（用户 2026-11-21）：dsh 原样移植 + C++ 套壳，跟随上游更新；全系统插件化
> （桌面端智能管家，其余皆插件）；界面像素级一致。

## 一、定案：sidecar 原样移植（Qt 壳替换 Electron 壳）

**决定性事实**：dsh 自家的桌面形态（apps/desktop）就是「Electron 44 壳：宿主进程跑
`dsh web` profile + 窗口加载 Web 前端资产」。PopTools 做完全相同的事，只把 Electron 壳
换成 Qt/C++ 壳——**这不是仿制，是 dsh 生产部署形态的等价实现**。

```
┌────────────────────────────────────────────────────────────┐
│ C++ 壳（PopTools 现有底座）                                  │
│ 主窗口/托盘/热键/悬浮球（保留） · WebView2 宿主 · sidecar 管理 │
│ MCP 能力 server：截图·贴图·OCR·注入日历·活动监控·文件检索·通知 │
└──────────────┬───────────────────────────┬─────────────────┘
               │ 进程生命周期                │ MCP (stdio/本地 socket)
┌──────────────▼───────────────────────────▼─────────────────┐
│ dsh host（Node sidecar，原样：npm @deepseek-ai/dsh）         │
│ webserver 127.0.0.1:3080 · Cordis 插件树 · agent 内核 ·      │
│ 会话事件日志 v4 · compaction · 审批 · 工具管线 · MCP manager  │
└──────────────┬─────────────────────────────────────────────┘
               │ http://127.0.0.1:3080（REST + /api/remote.mux WS）
┌──────────────▼─────────────────────────────────────────────┐
│ dsh WebUI（React 原样，WebView2 加载）——像素级一致            │
└────────────────────────────────────────────────────────────┘
```

**为什么不是 C++ 语义移植**：用户批注锁定「后续也要跟随 dsh 上游更新」——C++ 重写
agent loop/Cordis 后与上游永久分叉，每次上游更新都要人工追平。sidecar 下 dsh 整体
（内核+前端+插件）零改动，升级 = npm 升版本 + 桥接面回归。

**桥接面纪律（唯一接缝）**：C++ 与 dsh 之间只允许三类接触——
1. **MCP 能力桥**：PopTools 桌面能力经 C++ MCP server 暴露，dsh 的 mcp-manager 原生接入
   （dsh「Everything is a Plugin」+ MCP 是其标准扩展缝，更新最安全）；
2. **生命周期管理**：sidecar 进程启停/健康检查/端口协商/版本核对；
3. **展示集成**：无边框窗、托盘、悬浮球、全局热键 → 路由到 dsh 会话窗。
禁止经桥接面改 dsh 内部结构、禁 fork dsh 代码修补（上游可合入的 issue 走上游）。

**内存预算**：Node sidecar ~60-100MB + WebView2 ~100-150MB；Goody 使用期峰值
~350-400MB（现基线 159MB），关窗可回收 sidecar；桌面级模块（日历/截图）零增量。

## 二、运行时载体（已验证 2026-11-21）

| 件 | 来源 | 落点 |
|---|---|---|
| dsh | npm `@deepseek-ai/dsh@0.2.2-rc.5`（91 依赖自拉包族 + cordis 4.1；engines node>=24.9） | `tools/dsh/`（install_dsh.py，走代理 npm install） |
| Node 运行时 | nodejs.org 官方 zip（win-x64，v24 LTS ≥24.9） | `tools/node/`（install_dsh.py 一并装） |
| WebView2 loader SDK | NuGet 包（头文件+lib+loader dll） | `tools/webview2/`（install_webview2.py） |
| WebView2 运行时 | 系统自带（本机已确认 157.0.4341.x；Win11 通用） | 无需分发 |
| dsh home/配置 | 独立目录，不与 PopTools.ini 混放 | `%LOCALAPPDATA%/PopTools/dsh-home/`（M1 探明 DSH 环境变量口径） |
| API Key | dsh credentials-local 自己管 | PopTools 不读不存（移交备忘①不破） |

## 三、分期清单（每期构建 0 编译警告 + ctest 绿 + 用户目测验收）

- **M1 壳与运行时**（先决路径，风险最高放最前）：
  1. WebView2 嵌入 PoC：Goody 窗内嵌 WebView2 加载本地页（COM/WRL 初始化、resize/焦点/
      无边框窗圆角裁剪、输入法路由到网页）
  2. install_dsh.py：Node zip + npm install @deepseek-ai/dsh（幂等、代理、版本锁定）
  3. DshRuntime：QProcess sidecar 管理（启动 `dsh --profile web`、健康探测、优雅关停、
      崩溃重启、空闲回收）
  4. 端到端：WebView2 打开 localhost → dsh 界面出现 → 建会话 → 发消息 → 流式回复
- **M2 桌面能力桥**：C++ MCP server（第一批 tools：截图/贴图/OCR/日历读写/活动监控摘要/
  文件检索/桌面通知）→ dsh mcp-manager 配置接入 → agent 实际调用桌面工具验收；
  审批对齐：dsh approval fail-closed + PopTools PermissionManager 三模式语义一致
- **M3 产品融合**：悬浮球/热键/托盘入口路由 dsh 会话窗；设置页接管 dsh 配置项；
  旧 GoodyWindow 转退役开关（默认新 UI，设置页可回旧）；会话数据现状处置（旧 v3
  会话保留只读，不强行迁移）
- **M4 全系统插件化**：六模块能力逐个 MCP 化（截图→贴图→翻译→监控→日历→收纳盒）；
  桌面 UI 保留原生（注入日历/截图画布等 TS 做不了的部分）——「桌面端智能管家，
  其余皆插件」在此落地；每模块独立验收
- **上游更新机制（贯穿）**：npm 版本升级 → 回归桥接面三件套（MCP tools 契约/生命周期/
  展示路由）→ dsh home 数据兼容性核对

## 四、风险与回退

| 风险 | 缓解 |
|---|---|
| npm 安装面依赖代理/网络 | install_dsh.py 幂等 + 版本锁定 + 本地缓存；离线场景报告不硬撑 |
| Node 版本绑定 >=24.9 | install 脚本锁定具体 v24.x zip，随 dsh engines 更新 |
| WebView2 在 Qt 无边框窗内的圆角/z序/IME | M1-1 PoC 专门验；圆角可退矩形窗（dsh 原生也是矩形内容区） |
| dsh 端口占用/多实例 | 端口协商 + 单实例守卫（PopTools 托盘已有单实例） |
| sidecar 卡死/崩溃 | 健康探测 + 自动重启 + 用户可见状态 |
| 性能红线 | sidecar 独立进程不占 Qt 主线程；红线各条照守（钩子/脏矩形等不变） |
| 安全 | dsh 自带 bash/fs 工具 + PopTools 桌面能力——审批 fail-closed 对齐；
  API Key 走 dsh credentials-local；`extensions/cordis-runner`（模型自改插件树）不启用 |
| **回退方案** | sidecar 路线失败 → 盘点报告 f 节移植矩阵：核心契约层五件套 C++ 语义移植
  （会话日志/投影/loop/工具管线/compaction，≈1.5-2 万行） |

## 五、T-073 处置与现状

- 保留：IME 组词 Enter 守卫（行为修复）、ProcessRow 徽章主题重抛、T-070 全部域逻辑
- 过渡期：旧 Qt GoodyWindow 继续服役（未提交改动在工作区），M3 上退役开关后删旧路径
- 未提交（用户未指令提交）；本轮盘点/蓝图/需求单三文档已落盘
