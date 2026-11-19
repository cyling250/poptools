# 归档：当前任务陈旧验收块（2026-11-17 ~ 11-18 会话遗留）

> 2026-11-20 夜间批次（T-071，Agent_20261120005535）从主档当前任务区**原文无损搬移**。
> 搬移原因：主档 500 行预算达标；这些块的待办已被后续发版覆盖（v0.5.0 Release id
> 448376550 于 2026-11-18 发布、v0.5.1 于 2026-11-19 发布，均含本页所述全部内容）。
> 验收清单原文保留备查。

**T-060 更新自查在途（已提交未推送，2026-11-17）**：
- 已落地：core/update/UpdateChecker（releases/latest 异步 + 三段数值比较，版本宏 POPTOOLS_VERSION 由 CMake project VERSION 注入）；启动延迟 5s 静默检查（仅新版提示：前往下载/跳过此版本/关闭，跳过记 update/ignoredVersion）；关于对话框加"检查更新"；版本 bump 0.5.0（CMake+iss，关于对话框随宏自动）
- 已构建：dist/PopTools-Setup-0.5.0-20261117_1619.exe（53MB，旧包已清）
- 待办：①开代理后 git push（2 提交：f6dde5a T-059 / 3a460a6 T-060）②打 tag v0.5.0 推送 ③GitHub Release 创建+上传安装包（gh CLI 缺失，需 token 走 curl API 或用户网页手动）
- 提交：3a460a6 feat(update) T-060

**T-059 待交互回归（未提交，2026-11-17）**——单屏化+花屏根修+拖动跟手根治后用户需验证：
- ①**先重启 explorer**（explorer 驻留旧 DLL 代码，重编译不会自动生效；任务管理器→Windows 资源管理器→重新启动，PopTools watchdog 2s 自动重注入），日志 `%TEMP%\PopToolsCalendarHook.log` 的 mapped 行带 `(build 20261117b)` 且 dib 行带 `(build 20261117c dblbuf)` 才是新代码；②副屏无日历无花屏（用户实拍已确认 ✅）；③日历头部「调整」→ 拖动：**日历预览画在覆盖层虚线框内跟手移动（桌面层拖动期间隐藏，✓/× 确认/取消后回到桌面新位置）**、跨过屏幕边界整体跳屏；④悬停头部按钮不闪（T-058 修复未回归）；⑤双击单元格编辑器弹在日历所在屏；⑥试验遗留：settings.json 在副屏 (2450,100)，可「调整」拖回主屏
- 异常排查同 T-054 清单（注入日志 + crash/）；细节见 [handover/t059_calendar_single_screen.md](../t059_calendar_single_screen.md)

**T-064 启动更新自查（已提交未推送，2026-11-18）**：
- 已落地：core/update/UpdateChecker（releases/latest 异步 10s 超时 + 三段数值比较，版本宏
  POPTOOLS_VERSION 由 CMake project VERSION 注入 core/app 双目标）；启动延迟 5s 静默检查
  （仅新版提示：前往下载/跳过此版本/关闭；跳过记 update/ignoredVersion；失败不打扰）；关于
  对话框加"检查更新"手动入口（标准按钮 setButtonText 判 exec 返回值）；版本 bump 0.4.0→0.5.0
  （CMake+iss，关于对话框文本随宏自动）
- 已发布：Release v0.5.0（id 448376550，git 凭据走 API 创建+上传），资产
  PopTools-Setup-0.5.0-20261117_1708.exe（56MB，合并后代码重打包，含 T-063/T-064 全部内容；
  旧包已删）

**待用户真机验收：T-059/T-060/T-061 收纳盒（已提交 1a9c548）＋ T-062 全仓库代码卫生夜间批次（9 提交 e9a1232..8b41161，2026-11-17 凌晨自动完成）**：
- 收纳盒（T-059/T-060/T-061）：core/box 四件 + ui/box 四件 + BoxModule；DesktopSurfaceHost 通用面板宿主（SHM v3 六槽位）；标签间距根修（QFontMetrics 定位 + kCellH 112）+ 悬停按语义目标重绘 + 脏矩形提交。需求单 requirements/20261116-desktop-box.md
- T-062 夜间批次（用户睡眠授权：先提交→逐模块卫生→每模块一提交→只代码审查不做视觉验收）：9 模块 ×（R1 审查→F 修改→R2 复审→[F2→R3] 直到 PASS）串行子代理流水线；审查报告全档 temp_scripts/hygiene/*.md；每模块构建+ctest 全绿后才提交
- **真机验收重点**（T-062 行为面修复，全带 `T-062 修复:` 标注）：①设置对话框二次打开不再崩溃（B-1 连接时序）②截图"过小选区"不再吞掉已画标注、武装态右键退出恢复生效、取色悬停残影消失③划词翻译按住 Alt/Shift 也能成功复制④按住 F3 一秒只出一张贴图⑤Goody 白名单词边界（gotrojan 类命令会被拦）⑥退出 PopTools 后桌面注入正确解除（不再残留）
- **收纳盒验收清单**：①标签与图标有清晰间距；②滑过格子高亮逐格即时跟随、离开面板即消；③单击固定选中、双击启动、翻页/圆点/管理对话框正常；④盒子加高后自动行数/翻页/调整拖拽正常；⑤日历悬停按钮/翻页无回归
- 异常排查：`%TEMP%\PopToolsCalendarHook.log`（注入侧）+ build/Release/crash/（崩溃自诊）
