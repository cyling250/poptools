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
**开放点（调研回填后锁定）**：①被动加载模型（TIP 未被选为活动输入法能否进进程并存）②Win11 24H2+ 对 TIP 签名的强制程度 ③按用户 HKCU 注册的免管理员路径。

（调研子代理报告落地后，本文件开放点区回填结论并随 docs 提交。）
