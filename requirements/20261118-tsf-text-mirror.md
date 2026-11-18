# 需求单：T-066 TSF 文本镜像输入监听

**一句话目标**：用 TSF 输入处理器（TIP）DLL 在所有 TSF 应用的输入框上被动镜像最终文本——输入了什么、回退了什么都原样抄回来，取代现有字母键序流（keys.jsonl）。
**场景**：项目开发 · PopTools 活动监控文本层重写（T-045 的"LL 钩子只能拿到字母"局限的根本解决，用户 2026-11-18 拍板走 TSF 路线）
**交付物**：
1. `src/tsf/` PopToolsTextMirror.dll——COM TIP：编辑上下文影子快照 + OnEndEdit 双向 diff，静态 CRT（日历 hook 同款纪律）
2. 主进程接收端：worker 读 IPC → 密码门卫 → ActivityStore 新流 `<日期>/textlog.jsonl`
3. 注册/注销器：PopTools 启动时注册 TIP profile、关闭/停用时注销；设置页"活动监控"加开关
4. 字母流移除（KeyboardTextComposer + keys.jsonl）——**排在 TSF 镜像真机验证通过之后**

**关键约束**：不吞键、不影响用户正常输入（微软拼音共存是硬前提）；红线 1/2 照旧（DLL 回调只取文+入队，落盘全在主进程 worker）；密码框静默纪律三流沿用；可随时开关与卸载。
**验收标准**：记事本 / 浏览器（地址栏+网页输入框）/ Office / PopTools 自身 Qt 输入框——中文上屏、英文、退格删除、粘贴，均以最终文本形态出现在 textlog.jsonl；timeline 与每日记录不回归；ctest 全绿。
**AI代决假设**：
1. IPC 用命名管道（DLL 侧不可写即丢弃、不缓冲；PopTools 不在＝不监听），压测有问题再换 SHM 环
2. 影子快照按文档管理器缓存（≤8KB/文档，LRU ≤4 个），删除文本由影子差分还原
3. ImeCommitDetector/SnapshotOcrPipeline（OCR 上屏快照链）先保留后议退役——待 TSF 覆盖面实测
4. 时间线键盘心跳、T-065 热键钩子均不动
5. 若签名是硬门槛：自建证书本地信任 + 签名 DLL（步骤入文档），不影响开源发布
**开放点回填（2026-11-18 13:55 调研收口，细档 [docs/handover/t066_tsf_research.md](../docs/handover/t066_tsf_research.md)）**：
1. **被动加载模型——实锤死刑**：未选中即不被加载（官方文档原句＋build 26300 本机实证：注册+启用未选中 → 全进程零加载）；键盘类别每语言仅一个活动 TIP，选中即顶掉微软拼音；`GUID_TFCAT_TIP_OTHER` 不存在（SDK 头文件实证），SPEECH/HANDWRITING 类别能否并存激活属未验证假设。
2. **签名——无硬门槛**：weasel 全程未签名照常安装注册运行；自签+本地信任仅惠及 SecureMode/UWP/SmartAppControl 边缘（代决假设 5 由此不再触发）。
3. **注册——必须机器级**：HKCU 不生效（官方 API 无 per-user 注册），需管理员写 HKLM/HKCR；卸载须先杀 ctfmon 再删键（ctfmon 会回写复活）。
4. **新增宿主盲区事实**：经典 EDIT 控件（TRANSITORY）对 TSF 完全不可见；RichEdit 默认关 TSF；可观察宿主仅 TSF-aware（Chromium 系/新版记事本/Word/SES_USECTF）。
**判定：被动 TSF TIP 与"中文上屏＋全部应用＋零干扰"三诉求结构性冲突 → 路线待用户定（UIA 主体 / SPEECH 类别一次性探针 / en-US 收窄版）。**
