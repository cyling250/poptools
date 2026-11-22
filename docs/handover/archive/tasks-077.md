# 归档分册：T-077 ~ T-077c（原主档历史任务区剪切，2026-11-23 归档压实）

> 归档 = 原文整段剪切搬移，条目搬入即定稿，只增不改。

### T-077 自动更新——PopTools Release 自装 + dsh npm 更新
- **执行智能体**：Agent_20261122011419
- **结果**：✅ 落码。PopTools：UpdateChecker 扩 assets 直链+UpdateDownloader
  流式下载→Inno 静默装→安装器接管自动重启（开发树门控只跳转页）；dsh：
  DshUpdateService 查 npm 官方源对比 package.json → 停 sidecar→npm i→重启
 （失败回拉旧版）；均弹窗确认制+24h 定期。数据面自证 npm/GitHub 通
- 相关坑：无 ｜ 提交 6493e6f（真发布端到端验收随下次发版自然覆盖）

### T-077b 混 DPI 接屏修复（字体大+虚线框偏移）
- **执行智能体**：Agent_20261122011419
- **结果**：✅ 落码待复测。用户接回外接屏（150%→100% 切换）后字体大+调整
  虚线框只有日历 2/3——同根：host m_dpr 构造定死且「物理 rect 相同仅 dpr
  变」的屏切换被 onScreenLayoutChanged 的 target==old 判定漏掉。修复：host
  refreshDpr 动态重估（dprChanged 信号）+ CalendarPanel::onDprChanged
 （renderer 重建+视觉等比重映射+壁纸重算）；overlay 现取 dpr 换算自正
- 相关坑：无 ｜ 提交 c6b8a4c

### T-077c dsh 孤儿 sidecar 清理（EADDRINUSE 看门狗空转根治）
- **执行智能体**：Agent_20261122011419
- **结果**：✅ 端到端实证。用户报 dsh 拉不起——dsh-runtime.log 实锤孤儿
  sidecar 占 3080（attempts=10 空转）。修复：start() 前清理占口孤儿（TCP 表
  查监听 PID+映像验证本运行时 node 才终止+日志留证）；失败文案更新看门狗
  语义。SDK 头链坑：winsock2.h 须先于 windows.h（LEAN_AND_MEAN 排 AF_INET）
- 相关坑：无 ｜ 提交 4b1dee9
