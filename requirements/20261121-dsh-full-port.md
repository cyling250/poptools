# 📋 需求单：DeepSeek Harness 全量照搬——dsh 原样移植 + C++ 壳（T-074）

**一句话目标**：把 DeepSeek Harness（dsh）**原样**移植进 PopTools——界面、核心 agent 架构、
Cordis 插件架构、内置插件设计全部照搬，C++ 只做壳（桌面能力宿主 + 运行时托管），
后续跟随 dsh 上游更新。

**场景**：项目开发（接手已有项目，架构升级，L 级跨会话工程）

**交付物**：
1. M0 全量蓝图：dsh→PopTools 模块映射 + 分期清单（docs/design/20261121-dsh-port-blueprint.md）
2. dsh 运行时落地：dsh 前端（React）+ 内核在 PopTools 窗口内原样运行（WebView2 嵌入；
   内核载体按盘点报告定案）
3. C++ 壳：窗口/托盘/热键/悬浮球保留 + 桌面能力桥接 API（截图/OCR/注入日历/活动监控/
   文件系统 → dsh 插件可调用）
4. 全系统插件化路线：PopTools 现有六模块 → dsh 插件的迁移路径（桌面端智能管家，
   其余皆插件——用户 2026-11-21 拍板）

**关键约束**：
- **dsh 代码原样移植，不做内核语义重写**（用户批注：要能跟随 dsh 上游更新，
  「给他套个壳」）——C++ 侧不重写 agent loop/Cordis，只做运行时托管与桥接；
  壳与 dsh 之间只允许一个清晰的桥接面，上游更新 = 替换 dsh 包 + 桥接面回归
- 桌面级能力保留 C++：DLL 注入日历、全局热键、WH_* 钩子、OCR 子进程——TS/Web 做不了的部分
- WebView2 运行时（Win11 系统自带，本机已确认 157.0.4341.x）；本地 Qt 套件无 WebEngine，
  不下载 QtWebEngine，走 WebView2 loader SDK
- 性能红线不变（主线程禁重活；WebView2 属独立进程/线程，不占 Qt 主线程）
- 过渡期旧 GoodyWindow 与现网功能可用，平滑切换

**验收标准**：
- Goody 窗内呈现 dsh 原版界面（像素级一致）
- dsh 上游更新可低摩擦合入：改动集中在桥接面与资源替换，无内核语义重写
- dsh 插件可在 PopTools 内安装/运行/管理；桌面能力经桥接面可被插件调用
- 每期构建 0 编译警告 + ctest 绿 + 用户目测验收

**AI代决假设**：
1. WebView2 loader SDK（头文件+lib+loader dll）经 scripts/ 依赖脚本下载进 tools/
   （延续 install_qt.py / install_zxing.py 依赖惯例）
2. dsh 版本固定为盘点时 main 快照；升级机制设计为「替换 dsh 资源包 + 桥接面适配层」
3. 过渡期设置页加切换开关（旧 Qt 界面 ↔ dsh 界面），稳定后再删旧路径
4. 分期节奏：M0 蓝图先行（用户「不着急」表态 + 蓝图先行惯例），M1 起每期用户验收

**开放项（等 dsh 全量盘点报告回来定案，子代理进行中）**：
- 内核载体：dsh agent 内核（TS）跑在 WebView2 JS 上下文（Node API 由 C++ 壳垫片）
  vs Node sidecar 进程 vs 混合——取决于 dsh 内核对 Node API 的依赖面
- 分期清单细化：内核先行 or 界面先行的顺序
- Cordis 插件的加载形态：dsh 内置插件原样 + PopTools 桌面能力包装为宿主插件的接法
