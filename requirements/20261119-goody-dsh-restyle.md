# 📋 需求单：Goody 按 deepseek-harness 形式重构（T-070）

**一句话目标**：Goody 管家从后端调用链到前端 UI 对齐 DeepSeek 官方 agent harness（dsh）的形式，用 Qt/C++ 实现；用户已拍板"就按照 dsh 的来"（全权委托）。

**场景**：项目开发（接手已有项目，Qt Widgets）

**交付物**：
1. 后端：ChatSession 会话事件日志化（turn/step/user message/assistant message/tool call/tool result 持久事件 + 投影派生模型历史）、GoodyModule agent loop 形态（工具调用回合循环 + 事件分型）、LlmClient 流式契约对齐（瞬态 delta / 结算消息）
2. 前端：GoodyWindow 按 dsh WebUI 布局形态重做（事件驱动渲染、消息节点分型、composer 输入区、会话列表）
3. 构建 + ctest 绿，拉起供用户目测

**关键约束**：
- 参照物：dsh 源码已解压 temp_scripts/dl_dsh/src/deepseek-harness-master/（MIT 许可，可借鉴实现）
- Qt Widgets 技术栈不变（不引 QML/WebEngine）；现有会话数据（ChatSession v2 JSON）需可读或可迁移
- 现有功能不回退：多会话、工具调用+审批（PermissionManager）、思考折叠、代码块复制、编辑重发/重新生成/删除四件套、悬浮球
- 复杂度裁剪：Cordis 插件体系/profile/HMR/多后端沙箱等框架特性不搬（单应用不需要），只对齐**形式**——事件模型、循环结构、UI 形态

**验收标准**：
- 会话持久化含完整事件历史，模型历史由投影派生（"模型可见即已记录"）
- UI 呈现 dsh WebUI 的消息节点分型（用户/助手/工具调用卡/思考块/审批卡）与布局形态
- ctest 绿；用户目测认可

**AI代决假设**（用户离开，饭点委托）：
1. ChatSession v2 → v3 事件日志格式，实现 v2 只读迁移（旧会话无损进入新模型）
2. 事件类型最小集：turn/start|end、step/start|end、system/message、user/message、assistant/message、tool/call、tool/result + 瞬态 assistant/live-chunk——不搬 dsh 全部 30+ 事件类型
3. Cordis 插件树/HMR/profile 不搬；approval 对齐 PermissionManager 既有三模式
4. UI 以 apps/web Chat 视图形态为准（子代理报告回来后定布局树细节）
