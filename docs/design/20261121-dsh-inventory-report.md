# DeepSeek Harness（dsh）源码盘点报告

> 盘点时间：2026-11-21 · Agent_20261121184000 · 子代理产出
> 快照：`deepseek-ai/deepseek-harness` master 分支（默认分支，main 不存在），最新推送 2026-11-18，
> v0.2.2-rc.5，MIT，要求 Node >= 24.9。源码副本：`temp_scripts/dl_dsh_20261121/deepseek-harness-master/`
> 用途：T-074「dsh 原样移植 + C++ 壳」的决策与实施依据；配套蓝图
> docs/design/20261121-dsh-port-blueprint.md。

**一句话结论**：dsh 是约 **96 万行生产源码（917k TS/TSX + 44k CSS）、约 190 个子包、vende 了整个
插件框架（Cordis）、并带生成式 RPC 编译器（Typert）** 的 pnpm monorepo。C++ 全量移植不可行；
但 dsh 自家桌面形态就是「宿主进程跑 harness + webserver（127.0.0.1:3080）+ Electron 窗加载
Web 前端」——**PopTools 用 Qt/C++ 壳替换 Electron 壳即可原样运行 dsh**（sidecar 路线），
与用户「原样移植 + 套壳 + 跟随上游更新」的批注完全吻合。

## a. 包结构总表

仓库布局：`packages/*/*`（两级目录，组/包）+ `apps/*` + `vendor/*` + `python/*`。分组合计约 190 个子包。

**apps（≈12 万行）**
| 目录 | 行数 | 职责 |
|---|---|---|
| `apps/web` | 83,106 | WebUI 前端 Vite 入口，薄壳（`@deepseek-ai/dsh-client-web` 的 vite build） |
| `apps/desktop` | 19,508 | **Electron 44 桌面壳**：Desktop Host 进程内跑 `dsh web` profile，窗口加载打包 Web 资产并在同一文档内激活 client 插件 |
| `apps/cli` | 16,250 | `dsh` bin：profile 启动器（`web`/`headless`/`sdk`/`sdk-minimal`/`acp` + `plugin` 管理命令）、args 解析、`--dump-config`、profile 落盘引导 |
| `apps/desktop-host` / `desktop-publisher` / `plugin-market-generator` | 0.6k/1.8k/1.5k | 桌面宿主胶水 / 发布 / 插件市场元数据 |

**vendor（≈7k 行，dsh fork 的上游框架）**
| 包 | 行数 | 职责 |
|---|---|---|
| `cordis` | 2,865 | 插件框架内核（见 c 节） |
| `loader` | 1,580 | 配置驱动加载器：解析 cordis.yml / patch 行、`!!js` 表达式、依赖等待 |
| `include` / `hmr` / `schemastery` / `cosmokit` / `logger-console` / `timer` | 1.2k/0.5k/1.0k/0.6k/0.2k/0.2k | 配置 include、热重载基座、配置 schema 库、工具集 |

**packages 核心组**
| 组 | 代表包（行数） | 职责 |
|---|---|---|
| `core/` | `agent-loop`(2,422)、`session`(3,573)、`agent`(1,817)、`tools`(3,875)、`system-prompt`、`scope`、`tool-executor-native`/`-ptc`(1,731)、`agent-default-model`、`agent-tool-presentation` | 产品 API 主脊：会话日志、提示装配、工具注册表、Agent 服务、具体循环 |
| `session/`（18 包） | `session-persistence-jsonl`(5,628)、`session-persistence-sqlite`(2,592)、`session-projection`(1,206)、`session-projection-cache`、`session-format`+四段迁移包(v0→…→v4, 6.8k)、`session-query-sqlite`(1,853)、`session-checkpoint-policy`、`session-title*`、`working-directory` | 持久化、格式迁移链、投影注册表、全文检索 |
| `llm/` | `llm`(4,186)、`llm-deepseek`(5,174)、`llm-pi-ai`(5,444 多供应商)、`llm-retry`、`token-meter`(1,625)、`deepseek-llm-api-extensions` | 词表+适配器 seam + DeepSeek/多供应商适配 |
| `api/` | `gateway`(5,948)、`session-controller`(10,896)、`workspace-controller`、`settings-controller`、`terminal-controller`(2,880)、`workspace-files`、`job-controller`、`remote-access`(1,080)、`remotes`(779) | Typert RPC 网关 + BFF 控制器族 |
| `typert/` | `generator`(8,381)、`protocol`(1,897)、`registry`(980) | **TS 编译期 RPC 契约生成器**（装饰器→wire schema→Host/Client 双面产物） |
| `client/`（80+ 包，前端） | `web`(954 内核)、`connection`(2,444)、`modules`(3,464)、`store`、`ui-chat`(30,722)、`ui-conversation`(27,124)、`ui-primitives`(27,969)、`ui-schedule`(16,717)、`ui-trajectory`(15,477)、`ui-workspace`(13,222)、`ui-settings-*`、`ui-tool`(11,553)、`ui-theme`(2,194)、`ui-renderer`(6,762)… | 浏览器插件体系 + 全部 UI |
| `boot/` | `app-boot`(5,570)、`hmr`(1,747)、`plugin-manager`(3,962)、`plugin-market`、`cmdline`、`config-editor` | profile 组装、HMR 编排、插件安装管理 |
| `bundle/` | `base`(1,045 YAML)、`web-app`、`headless`、`sdk-app`、`sdk-minimal`、`acp-app` | 可安装的 cordis.patch.yml 叠层 |
| 能力 seam 族 | `fs/`（`fs-local` 1,401 + `tool-fs` 2,678 + `tool-fs-search` 1,628）、`shell/`（`tool-bash` 871/`tool-pwsh` 1,018）、`subprocess/`(7,187)、`sandbox/`、`terminal/`(PTY)、`ssh/`、`lsp/`、`mcp/`(6,016)、`web/`、`skill/`、`attachment/`、`storage/`、`spill/`、`jobs/`、`goal/`(2,630)、`schedule/`(4,036)、`workflow/`、`team/`、`subagent/`(13,328)、`ptc-runtime/`(4,882)、`document/`(Office)、`compaction/`(2,911+1,789+…)、`context/`、`preset/`、`guard/`、`interaction/`（审批/提问/权限预设）、`credentials/`、`settings/`、`account/`、`telemetry/`、`identity/`、`feedback/`、`deliverables/`、`todo/`、`plan/`、`workspace/`、`acp/`、`sdk/` | 每族 = Service Definition + Provider + Consumer 三角色 |
| `extensions/` | `tool-cordis`(12,261)、`cordis-host-runner`、`cordis-client-runner`、`ui-cordis` | Agent 自改运行时：模型可读写插件树 |
| `experimental/`（60 包，46k+） | `client-ui-forge`(37,740)、`liveshare`(33,239)、`inspector`(17,961)、`timeline`、`cua*`、`browser-use*`、`forge*`、`hooks-*`、`subagent-oar`、`worktree`、`webhook`、`webworker-runtime`… | 评估期能力，非产品主干 |

**python/**：`sdk`（8,436 行）是 TS SDK 的孪生：客户端启动 `dsh --profile sdk` 子进程走 JSON-RPC。

**相互依赖形态**：所有包只 peer 依赖 `@deepseek-ai/cordis` 与其他包的 `/types` 面；运行时耦合全部
经 `inject: ['服务键']` 声明，包级 import 依赖极浅。**依赖图的实际载体是 cordis 服务键而非 npm 图**
——移植时最重要的结构观察。

## b. 核心 agent 架构

### b1. Agent loop 精确结构（`packages/core/agent-loop/src/agent.ts`，ReactLoopAgent）

- **两层循环**：`kick()` → `while (await turn())` → `turn()` 内 `while(true)` 步循环 → `step()` 内 attempt 重试循环。
- **turn**（回合）：先 append `turn/start{turn}`；循环体：claim inbox → `preStep()` → `step()` → 步间检查
  next-step inbox → 全部结束后 `agent/turn-stopping`（serial 终检）→ append `turn/end{turn, reason}`。
  turn reason 词汇：`completed / aborted{cause} / blocked / error{LlmFailure} / max-tokens（粘性，后续正常步
  不可降级）/ interrupted（崩溃补写）/ forked`。
- **step**（步）= 一次模型请求 + 它触发的全部工具执行。`step()` 内：`prepareRequest`（`agent/request`
  waterfall 提议 config → **`llm.prepareCall()` 先绑定适配器再准许输入**）→ 系统提示投影提交
  （`system/message` 按节点 reconcile）→ user 准许（首 attempt 才写 `user/message`）→ `buildRequest()`
  （写 `request/header`（reason: initial/resume/change/series + startsSeries）、工具增删生成
  `developer/message`、`request/context`、**从日志派生冻结消息 `session.deriveMessages()`**）→ 流式调用 →
  失败则 `assistant/attempt` + `agent/request-error` waterfall（compaction 在此挂 retry）→ 成功
  `assistant/message`（内嵌完整压缩流）→ 工具批执行 → `additionalContexts` 注入 next-step inbox →
  无 tool-call 则步结束。
- **收尾恢复**：步失败时 `ToolCallRecovery`（监听 session/event）补写缺失的 `tool/result`（合成错误结果）
  再 `step/end`。
- **inbox 三入口**：`followup()`（next-turn+wake）、`steer()`（next-step+wake）、`inject()`（next-step 不唤醒）；
  `cancel(cause)` cause 词汇：`user/parent/hook{reason}/disposed`。phase 机：
  `idle / maintenance / running{abort,turn,step,wakeRequested}`。
- **live 事件词表**（Cordis Events）：
  - `agent/*`（emit，除注明）：`agent/created`、`agent/disposed`、`agent/status`（idle/running）、
    `agent/inbox/inserted|claimed|discarded|spliced`、`agent/error`、`agent/assistant-stream`
    （start/chunk*/end 三帧）；waterfall（可包装/否决）：`agent/pre-step`（权威
    reject|enter(messages)）、`agent/request`（只能调控制项）、`agent/request-error`（返回 retry 或原错误）；
    serial：`agent/turn-stopping`。
  - `tools/*`：`tools/pre-execute`、`tools/execute`（around）、`tools/post-execute`、`tools/result`、`tools/change`。
  - `session/*`：`session/created`、`session/disposed`、`session/event`（一切持久事件的广播出口）、`session/flush`。
  - `system-prompt/assemble`（waterfall）、`system-prompt/change`、`llm/adapters-updated`、`llm/models-updated`、
    `approval/request`（waterfall）、`fs/*`、`compaction/summary-error`、`hmr/*` 等。
  - 派发模式公共契约：emit（不等待）/ waterfall（不等待、有返回值、around 语义）/ parallel（等待、并发）/
    serial（等待、顺序、有返回值）/ bail（首个 bail 止）。
- **持久会话事件词表**（`KNOWN_SESSION_EVENT_TYPES` 63 个）：核心 `turn/start`、`turn/end`、`step/start`、
  `step/end`、`user/message`（content+source: human/injected/goal）、`developer/message`、`system/message`、
  `assistant/message{message, stream[], usage?, interrupted?}`、`assistant/attempt{stream[]}`、
  `tool/call{callId,name,arguments 原始 JSON 串}`、`tool/result{message, error?, meta?}`、
  `request/header{header, reason, startsSeries?}`、`request/context`、`session/end-seed`；插件贡献：
  `approval/asked|decided|policy`、`compaction/start|prune|summary|end`、`todo/write`、`goal/change`、
  `plan/mode`、`image/offload`、`feedback/*`、`hook/*`、`job/*`、`llm/retry*`、`model/selection`、
  `permission/preset`、`sandbox/mode`、`subagent/*`、`team/*`、`tool-workflow/*`、`tool/ptc-dispatch*`、
  `deliverables/presented`、`workspace/*`、`session/title*` 等。信封统一：
  `{seq, time, type, data, ignorable?}`，消息类事件额外带 `surfaceOp: 'append'|{op:'replace',startSeq,endSeq}`
  与 `sourceEventSeqs`。

### b2. 会话事件日志 schema 与存储

- **格式版本 4**（`SESSION_FORMAT_VERSION`），JSONL 物理：v1+ = `session.vN.jsonl[.zstd]`；**代际发布制**：
  已提交的代路径永不改名/替换/删除；头部 `SessionHeader{version,id,createdAt,cwd?,parentSession?,isSeeded,…}`。
- 读路径 fail-closed：未知事件类型且无 `ignorable:true` → 拒绝重建（防新版日志被静默吞）；相邻迁移链每包
  只做一步 vN→vN+1；崩溃尾修复属 agent 层（`interruptedTurnClosers` 合成缺 tool/result、step/end、turn/end）。
- 后端接口 `SessionPersistence{create/open/stat/list/export}` + 每会话 `SessionHandle{append/read/close}`，
  批 append 连续 seq 且 lossless JSON；默认 jsonl-zstd（worker 线程 + zstd），另有 sqlite。

### b3. 投影派生模型历史

- **派生方向唯一**：模型历史 = `deriveMessages()` 纯投影自日志（surface 折叠：`foldSurface()` 按
  append/replace 维护有序 surface）。assistant/message 内嵌 timed stream 是回放/遥测/持久化的唯一事实，
  live 增量走 `agent/assistant-stream`。**"Model-visible means logged"**。
- **客户端投影**：域插件注册 `ProjectionDefinition{key, stateSchema, stateVersion, init, apply(state,event),
  wire?}`，注册表主动折叠已提交事件，`snapshot()` 返回带事件水位线的完整值 + 变更流；可检查点缓存
  （写后 200 事件/5s）。AgentLoop 自身注册 `modelSelection` 与 `inbox` 两个投影。

### b4. 上下文压缩（compaction）

- 族分工：`ctx.compaction`（契约）← `compaction-basic`（自动触发）+ `compaction-tool-result-pruner`
  （阈值 8192 字符，保头 4096/尾 1024）+ `compaction-image-offload` + `command-compact`（`/compact`）。
- 触发：**`agent/pre-step`** 上 token-meter 测压（默认 `thresholdRatio 0.8` / `retainRatio 0.16`）+
  **`agent/request-error`** 仅对 canonical context-overflow 恢复；恢复在开放步内进行。
- 摘要请求设计（值得照搬）：**重放对话原文 + 压缩指令作为最后一条 user message**（辅助请求是上一请求的
  真前缀 → 复用供应商 KV cache）。摘要包裹在 `<compacted-summary>` 标签内。落盘：
  `compaction/start{compactionId,turn|null}` → `compaction/summary{…, surfaceOp:replace}` → `compaction/end{…}`。

### b5. 权限/审批流

- `ctx.approval`：`request()` → 记 `approval/asked{id,toolName,callId?,reason?}`（log-only 审计）→
  `approval/request` waterfall（答者链可认领或 next() 委托）→ 结果 `allowed-once / rejected / cancelled /
  unavailable` → 记 `approval/decided`；**无答者 → fail-closed 拒绝**。
- `permission/preset` 预设服务：`read-only / workspace-write / danger-full-access` 三档，绑定 sandbox 模式 +
  approval 策略（danger 档 approval=never）；环境变量 `DSH_PERMISSION_MODE`；会话创建时由 host 设置服务
  记录授权，子会话继承。
- 工具管线审批位序严格：`tools/pre-execute`（allow/deny/**ask**）→ ask 交 `ctx.approval`（allowed-once 才
  继续）→ 单调 guards → around `tools/execute` → fs 意图门 → `tools/post-execute`。

### b6. 工具注册表接口

- `ToolDefinition extends ToolSchema{name,description,parameters:JSON-Schema}` + `output{schema, render,
  presentationMeta?}` + `execute(args, exec)→Promise<canonical JSON>` + 可选 `projectContent /
  finalizeContent / timeoutMs / isConcurrencySafe(args) / presentCall(args)→ToolCallView /
  presentResult(args,result)→ToolResultView`。schema 声明编译成 JSON Schema 并运行时双向校验
  （自研 646 行校验器）。
- `ToolsService`（`ctx.tools`）：`register(definition,{condition?})→disposer`、`resolve({agent,modelSelection,
  signal})→ResolvedToolSet`（步级准入）、`schemas(scope)`（只发 name/description/parameters）、
  `execute({calls,signal,agent,log:{turn,step},maxParallelToolCalls,toolSet})`。
- 执行器双模：native（直接批执行）与 **PTC**（`run_code` 唯一模型面 + 生成的 TS SDK 子调用桥接，子调用记
  `tool/ptc-dispatch`）。UI 挂钩 `presentCall/presentResult` 纯函数化，live 与回放共用。

### b7. LLM 客户端契约

- **消息/块**：`ContentBlockMap`：`text / reasoning（思考链，一等公民）/ image{attachment,offloaded?} /
  file / tool-call{raw arguments 字符串} / tool-addition / tool-removal`。**finish_reason**：
  `stop / tool-calls / max-tokens / aborted{failure} / error{failure}`。`TokenUsage` 三计数字段互斥
  （input 为未缓存输入，缓存单列 cacheRead/cacheWrite）。
- **流协议**：`StreamChunk = block-start{index,blockType} | text-delta | reasoning-delta |
  tool-call-delta{id,argumentsDelta} | block-end{assembled block} | usage | finish{reason, replayState?}`。
- **服务面**（`ctx.llm`）：`registerAdapter(providers, adapter)`、`listProviders/listModels/
  listConfigurableProviders/discoverModels`、`resolveCallConfig`、
  **`prepareCall(config,signal)→PreparedLlmCall{config, adapterDefaults, context{contextWindow},
  systemPromptUpdate?, toolUpdate?, retryPolicy, stream(request)}`**（绑定先于输入准许是 loop 的硬约束）。
  适配器：deepseek 官方/ApiKey/账户三型 + pi-ai 多供应商；`request.purpose` 标注辅助请求。
  系统提示中途变更能力位：`systemPromptUpdate:'in-history'` 与 `toolUpdate:'in-history'|'addition-only'`。

## c. Cordis 插件体系

### 内核（vendor/cordis，2,865 行，极小）
- **插件 = 实现 Service 的对象**：函数（带 `inject?: string[]` + `apply(ctx)`）或 `Service` 子类
  （`[Service.init]` 钩子）。**Context = 服务仓库**：服务以稳定 `ctx.<key>` 占位；`inject` 声明依赖 →
  加载顺序由服务可用性驱动。
- **事件**：`ctx.emit/waterfall/parallel/serial/bail`；TS 声明合并 `interface Events` 定词表。
- **可逆效应**：一切注册经 `ctx.effect(fn, label)` 或 `ctx.on()` 装配并返回 disposer；unload 按注册逆序
  回收。scope：每 Agent 一个子 Context；`isolate(name,label)` 做服务隔离域。fiber（842 行）管生命周期
  状态机。
- 文件分布：context 152 / events 375 / fiber 842 / registry 337 / reflect 458 / service 125 / logger 263 /
  utils 297。

### 配置驱动装配（dsh 层）
- **profile** = 命名组合：bundle 有序叠层 + 用户 `cordis.patch.yml` + home patch + `--patch` 覆盖。
  **bundle** = 补丁 YAML 分发格式（`dsh.bundle.patch` 字段）；patch 行 = `{id, name, disabled?, config?,
  inject?}`，按 id 整行替换。
- `!!js` 表达式装载期求值；Include 支持每次挂载决策重估。
- **HMR**：模块替换、Include 刷新、profile 重组共享一个协调队列；web/base profile 默认启用（仅配置热重载）。

### 插件包完整形态（样本：host-authentication-token / tool-fs）
`package.json`（name=`@deepseek-ai/dsh-*`，`main: lib/index.js`，可选 `dsh.bundle` 字段）+ `src/index.ts` 导出：
`export const name`、`export const inject = ['connection','credentials']`、`export const Config = z.object({…})`
（schemastery 配置 schema，volatile 字段热生效）、`export async function apply(ctx, config)` 或 class Service。
可选 `locale/*.json`、`./types` 子路径导出。

### 内置插件清单（dsh-base ~80 行 YAML + dsh-web-app 追加）
- **dsh-base**：timer、hmr、llm、deepseek-llm-api-extensions、session、session-log-deepseek、
  typert-registry/loader/gateway、remote-access、session-title(+llm)、user-questions、agent、
  agent-default-model、plugin-package-inventory-deepseek、jobs-local、llm-retry、config-editor、settings、
  authorization、deepseek-account/auth、credentials-local、llm-pi-ai（挂起式）、session-persistence-jsonl、
  attachment-local、session-query-sqlite（默认 `openAt:never` 全文搜索关闭）、working-directory、
  session-projection(+cache)、storage/json/domain、otel+session-telemetry-otel（FEEDBACK_ONLY）、
  subprocess-local、cli-account、sandbox-local、sandbox-policy、bash/pwsh-sandbox（按平台）、approval
  （user-approval）、permission-presets、shell-env、tool-bash/tool-pwsh（按平台）、tool-jobs、
  fs-observation-policy（默认关）、tool-fs、tool-fs-search、agent-instructions、skill(+filesystem)、
  tool-skill、upstream-skills、commands、command-feedback、goal(+round-driver+command)、plan-mode、
  token-meter、compaction-basic、command-compact、subagent、agent-team(+tool)、subagent spawn/fork-in-process、
  tool-subagent-control/tool-subagent/tool-subagent-fork、ptc-runtime-node、workflow-ptc、tool-workflow、
  tool-call-timeout-policy、spill-local/policy、session-checkpoint-policy、tool-result-pruner、image-offload、
  tool-todo、tool-goal、repeat-tool-reminder、web(+search-deepseek/deepseek-api/fetch-http)/tool-web。
- **dsh-web-app 追加**：desktop 遥测（条件）、subagent-model-selection-settings、message-feedback、
  session-log-export、open-in-app、ui-* 全家、workspace(+changes)、session-reference/file-reference-local、
  session-stats/turn-outline、directory-picker-auto、plugin-inventory、**session-controller /
  job-controller / terminal-controller / workspace-files / schedule / settings-controller /
  mcp-manager(+account) / workspace-controller**（Remote 控制器）、cordis-host/client-runner、
  web-startup/webserver/web-runtime、client-hmr、modules、connection、authentication-token、file-upload、
  api-remotes、locale/theme/shortcuts/ui-layout/ui-renderer…；并把 base 的 agent 平面行 disable 后移入
  agent preset 平面（`presets/standard|cordis|ptc|minimal.patch.yml`）。
- **工具目录**（docs/tool-catalog.md）：`plugin_manager`、`plugin_market`、`ask_user_question`、
  `run_code`(PTC)、`exit_plan_mode`、`bash`/`pwsh`、`read/write/edit/apply_patch/read_image`、
  `glob/grep`（打包 ripgrep）、`terminal_*`6 个、`user_terminal_*`、`office_*`3、`present`、
  `cordis_inspect`、`create/get/update_goal`、`schedule_*`4、`lsp`、`ralph`、`skill`、
  `list_session/list_workspace`、`session_*`5、`subagent_oar`、`subagent`/`subagent_fork`、
  `interrupt/list/send/wait_agent`、`job_kill/list/output`、`team_task_*`4、`todo_write`、
  `enter/exit_worktree`、`workflow`、`load_workspace_dependencies`、`web_search/web_fetch`、
  `stagehand_*`6（浏览器）。

## d. WebUI 前端

- **技术栈**：React 18 + Vite 6 + TypeScript；**无组件库、无 Tailwind**——CSS Modules + clsx + 自家
  ui-primitives（27,969 行，唯一跨包组件通道）；状态管理：zustand vanilla + immer +
  subscribeWithSelector + rafFlush（client-store）；ui-renderer 统一 useSyncExternalStore 桥接；
  虚拟滚动 @tanstack/react-virtual；Markdown/代码高亮/KaTeX 自带。
- **内核形态**：`apps/web/src/main.ts` 挂载点 → `client/web`（954 行）AppWebEntry：boot 页 → 等
  `window.__DSH_BOOT_READY__` → 读 `window.__DSH_BOOT__` 模块清单（host 注入）→ 在浏览器里再起一个
  **完整 cordis Context**，装载 client 插件（每个 UI 包有 node 半 + browser 半双面）→ 激活审计 →
  交给 ui-renderer 挂载 React 树。**前端本身就是插件树**：页面=插件贡献的 slot/surface。
- **页面/组件树**：ui-layout（框架壳）→ ui-sidebar（会话列表/搜索/Workspace）→ 主区
  ui-conversation/ui-chat（Chat 节点树：assistant/tool/command/compaction/inbox/retry/turn-error/
  turn-max-tokens/turn-tail/process-groups/… 每种事件类型一个 ConversationNodeDefinition + keyed renderer）、
  composer（ui-input-trigger '/'|'@' 管线）、右侧 ui-dockkit 停靠面（terminal/files/document-preview/browser
  标签）；ui-settings-*；ui-approval（审批卡）、ui-plan、ui-goal（GoalBar）、ui-jobs、ui-schedule、
  ui-agent-team、ui-trajectory、ui-tool。
- **API 面**：
  - **RPC**：`POST /api/<namespace>/<method>`，payload 仅 `{args}`；Typert Gateway 校验 codec/查 lookup
    （`agent`→自动冷恢复+去重并发 resume、`session`）。二进制字段走 result-relative byte attachment。
  - **流**：`/api/remote.mux` WebSocket 多路复用（RemoteStreamHandle{iterate,send,end,dispose}）。
  - **session-controller Remote 方法全列**：`list, search, create, selectModel, initializeDefaultModel,
    modelCatalog, canOpenWorkspacePath, openWorkspacePath, workspacePathApplications, rename, fork, prompt,
    attachment, updateQueue, cancel, page/events, observeEvent*, observeProjections*, state, observeState*,
    wait, follow*, projections, control*`（* 为流式）。
  - **job-controller**：`list*, follow*, kill`；**settings-controller**：`describe, update, replace, mutate,
    openSettingsDocument`；**workspace-controller**：`create, initializeDefault, rename, delete,
    insertBefore, insertSessionBefore, archiveSession, unarchiveSession, pinSession, unpinSession, follow*`；
    **terminal-controller**：`environment, shells, list, create, watch*, retain*, attach*(双向), rename, close`；
    **workspace-files**：`read, readBytes, stat, list, search, changes*`。
  - **精确 Fetch 路由**：`/api/file`（媒体引用）、raw Blob/Stream 上传（file-upload）；
    `GET /plugins/<id>/client.js`（插件 client bundle）。
- **主题系统**：`ui-theme` 独占。三层：`--dsw-static-*` 原语调色板（~207 个）→ `--dsw-alias-*` 语义 token
  （~267 个：label-primary、bg-base/bg-layer-2、border-l4、link、elevation-*、menu-surface-fill、
  mask-blur…）→ 组件只许用 alias（lint 拒绝 static 引用与字面色）。**亮暗切换 = `body` vs
  `body[data-ds-dark-theme]` 两段重绑**；base.css 管字号/字体/圆角（`--dsw-radius-xs..panel`）/动效；
  corner-shape.css 提供 superellipse；0.5px 发丝线、hover 媒体查询、焦点可见性有专门 spec。
- **脱离其 server 单独跑前端需要**：① `window.__DSH_BOOT__` 清单注入器；② Connection 两半（HTTP RPC 信封
  + `remote.mux` WS 帧协议）或 mock（仓库自带 `llm-mock-server`/`remote-mock`/`client-test-runtime`）；
  ③ client 半模块裁剪；④ theme 三层 CSS 纯静态可直接用。——**sidecar 路线下全部不需要**（真 server 在跑）。

## e. server 形态与 C++ 重实现工作量评估（回退方案用）

- **形态**：无独立 server 框架——`host/webserver`（1,061 行）是朴素 Node http 路由载体
  （默认 `127.0.0.1:3080`，TLS 可配，gzip L1，拒 wildcard bind，300MiB JSON 上限）；业务全部是 Typert
  Remote 描述符；认证 `authentication-token`（cookie 持久签名密钥 + 30 天 cookie + owner principal /
  api peer scope）。
- **C++ 重实现估算**：HTTP/WS 路由 + RPC 信封 + cookie 认证 + JSONL 存储 ≈ 3–5 千行；Typert 编译期
  schema 生成在 C++ 无对应物 → 手写每方法校验或显式 descriptor 注册表；zstd 需引库或退化纯 JSONL；
  parity API 面总计 **≈ 1.5–2.5 万行 C++**。

## f. 移植矩阵（若走 C++ 语义移植的判定——sidecar 定案后此表仅作回退依据）

| 模块 | dsh 规模 | 判定 |
|---|---|---|
| Cordis 内核（服务仓库+事件五模式+effect） | 2.9k | 语义移植（≈2–3k 行 C++） |
| Loader/patch/bundle/profile | 1.6k+5.6k | 语义移植（裁剪） |
| 会话事件日志 schema+JSONL 存储 | 3.6k+5.6k | 语义移植（核心资产） |
| agent loop（turn/step/pre-step/request-error） | 2.4k | 语义移植 |
| 投影注册表（session-projection） | 1.2k+缓存 | 语义移植 |
| 工具注册表+执行管线 | 3.9k+ | 语义移植 |
| LLM 词表+Runtime+DeepSeek 适配器 | 4.2k+5.2k | 语义移植；pi-ai 不搬 |
| compaction 族 | ~5k | 语义移植 |
| 审批/权限预设/sandbox | ~4k | 语义移植（裁剪；Windows 无 bwrap/Landlock/Seatbelt） |
| Typert RPC 网关+生成器 | 15k | 需重设计，不照搬 |
| WebUI（client 内核+80 ui 包） | 300k+ | 不照搬代码，借架构（主题三层 token 可 1:1 抄成 QSS 变量体系） |
| host/webserver+connection+auth | ~5k | C++ 直译 |
| 前端插件 roster | — | 不搬 |
| extensions/cordis-runner（agent 自改插件树） | 27k | 不搬（高风险区：模型可增删插件=RCE 面） |
| experimental 全族 | 150k+ | 不搬 |
| HMR | 2.2k | 不搬 |
| Python SDK / Electron desktop / ACP | ~30k | 不搬 |

**量化判断**：与 PopTools 价值直接对应的核心契约层约 **4–5 万行 TS**，语义移植为 C++ 约 **1.5–2 万行**；
其余 90%+ 是 Web/Electron/实验/生态件。sidecar 路线下这份工作量整体不需要——dsh 原样运行，
C++ 只做壳与 MCP 桥。

## 遗留说明
- 解压产物与 ZIP 保留于 `temp_scripts/dl_dsh_20261121/`（含解压脚本），未入库、未动 git。
- 分发渠道已验证（2026-11-21）：npm `@deepseek-ai/dsh@0.2.2-rc.5`（engines node>=24.9，bin lib/bin.js，
  91 依赖，主包解压 6.2MB/536 文件，自拉 @deepseek-ai/dsh-* 包族 + cordis 4.1）；GitHub Release 无
  二进制资产（仅源码包）。
- 未读取 `build/Release/PopTools.ini`；未触碰 PopTools 源码。
