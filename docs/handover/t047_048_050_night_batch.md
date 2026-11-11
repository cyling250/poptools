# 夜间批次细档：T-049(R6) / T-047 / T-048 / T-050（2026-11-12 夜 · Agent_20261112003400）

> 本档是四个任务「怎么做的」工程记录；任务级结论见主档条目。夜间采用母代理 + 子代理流水线（开发/复审分代理，并发额度 2，只读探查可与构建型代理并行）。

## §0 R5 结算注记（并入 T-046，提交 d1eeaf2）

- 独立核对：7/7 项落码且与交接描述一致；R5-B3/B4/C4 = temp_scripts/review/round5_findings.md 按序清单中的**有意跳过项**（B3 贴图窗数量上限 P3、B4 F3 无图热键路径静默 P3、C4 盒子静默失败反馈），非半途而废；代码全文无 R5-B3/B4 痕迹。
- 复审发现 P1-1：R5-C2 装载过滤把既有单测 chat_session_roundtrip 的测试数据判为孤儿 tool 丢弃 → **交接计划文档「ctest 已实测全绿」记载与实测不符**（→P-038）。复审修正 6 项：测试数据合法化 + 新增 chat_session_corrupt_backup（.bad 两代轮换 .bad.old）、role 白名单（system/user/assistant/tool）、messages 非数组并入损坏路径、断组保守注释、托盘文案取 GlobalHotKey lastError。

## §1 T-049 / R6 批量修复轮（be9c1a8）

- 探查四单（只读 Explore 并行）：A【DPI 扫描】与 B【QSS/主题】两面零新发现（全项目唯一 P-035 违例就是已知 R6-B1；P-022/P-023/设置广播全覆盖合规）；D【性能红线】出 8 条（无 P0/P1）；E【测试缺口】出 18 条（C1~C14 直测、C15/C16 小重构、C17/C18 需大重构缓议）。
- 落码 9 条要点：R6-B1 底图 m_basePixmap（失效=捕获变更‖逻辑尺寸变更‖DPR 变化）+ 放大镜 DPR 点绘；R6-B2 m_pending 置位移入 busy() 检查后（对齐 OcrController 先例）；R6-B3 GoodyWindow 关键颜色内联+三组调色板；R6-B4 OcrPanel/TranslateCard showEvent QSignalBlocker 重读 targetLang；R6-C1 fs.list 全异步（QThreadPool+QPointer+Queued，QDirIterator 前 500 带属性、余下纯计数，目录序非名称序为已接受取舍）；R6-C2 MCP 懒启动（仅 openChat 触发——mcp__* 工具预启动不可达）；C3 BoxWindow stat 缓存 / C4 GlobalKeyboardHook 运行期防重入 / C5 finalize 死代码删除。
- 测试补齐 16 例（60→77 绿）+ ToolText.{h,cpp} 抽取（summarizeToolArgs/Result）+ SnapshotOcrPipeline::consumeBaselineAdded 抽取 + makeFsTools 去 nullptr 默认值。
- 复审：通过（1 P2 + 4 P3）→ **R6-B1a**：混合 DPR 多屏（物理图尺寸 ≠ rect×窗口 DPR）回退旧 scaled 路径（physMatch ±2px 容差，回退分支不挂 DPR），随 T-047 提交。
- 缓议重构：C17 McpStdioClient 协议核心（抽 ProtocolCore + 注入 transport + setRequestTimeoutMs，~100 行）；C18 工具回合循环状态机 ToolRoundLoop（~150 行迁移）——建议 Goody UI 阶段三后再动，避免与 UI 重构冲突。

## §2 T-047 深度输入监控升级（864b57d）

- **OCR A/B**（temp_scripts/t047_ocr_ab，判定阈值写死于 run_ab.py judge 段）：v4 rec 小字组 CER 5.17%→2.59%（-50%）、时延 +11.1%、8 图全零回归 → 只切 `rec_ch_PP-OCRv4_infer.onnx`（+166KB）；v4 det 反而劣化（img02 翻倍）不采用；2x 上采样增益全在标点、有引入新错字案例（屏蔽→屏莓/屏蒋），不采用（二期可选）。
- **UiaTextReader**：STA worker（无 parent + moveToThread + read 内线程身份运行期验证 + 惰性 COM 会话 + finished→cleanup 先 Release 后 CoUninitialize）；取文链 TextPattern（选区非退化直取 / 光标退化 Clone 收拢 + Start 端点左移 2000 取 [caret-2000, caret]）→ GetVisibleRanges → ValuePattern；IsPassword 经 GetCurrentPropertyValue；**进程号对号**（UIA_ProcessIdPropertyId vs 前台 pid；浏览器/Electron NativeWindowHandle=0 时靠 Result.elPid 兜底：topHwnd==m_reqHwnd ‖ elPid==m_reqPid）。
- **双基线** m_lastText(ocr) / m_lastUiaText(uia) 隔离（文本粒度不同，混用会产生假增量）+ diffUiaAdded 滑动窗口补解（旧文本最长后缀 = 新文本前缀，最大重叠降序试起，宁缺毋滥）。
- **ClipboardListener**：message-only 窗口 + AddClipboardFormatListener（非钩子）；wndproc 零耗时（记 seq + 重启 300ms 防抖）→ busy 门卫 → QThreadPool 提取（OpenClipboard 5×15ms 重试、GlobalSize 校验、文本头尾 200 字符 + SHA256 前 64KB+总长、图片仅 BITMAPINFOHEADER 40 字节、HDROP≤5）→ QPointer 回 GUI 去重落盘；**OwnWriteGuard** 8 槽 seq 环形缓存、9 个写入点埋点；ClipboardDeduper repeat 冲账（内容变化时先冲 {"kind":"repeat","count":N} 再落新条目；stopMonitor flush 兜底）。
- **密码三流共享**：PasswordFieldGuard::cacheUiaPassword 500ms TTL（**仅收正判**——负判不回写防 UIA 误开打码豁免）；OCR 回落入口 isPasswordFocused 复查（复审 P2-1，防 UIA 往返间隙焦点切进已开显示密码的密码框）。
- 复审修复要点：析构 wait(2000) 超时放弃 join（QThread 堆上无 parent + finished→deleteLater；**绝不 delete 运行中 QThread**，超时即有意泄漏数 KB 对象换退出有界）；线程身份验证失败 emit 空 Result 即 return（release 下不得跨套间调 UIA）。
- 已知限制：UIA 无超时 API（IsHungAppWindow 预检 + busy 门卫 + 专用 worker 隔离，挂起应用该窗口走 OCR 直至恢复）；UIA↔OCR 基线不共享 → 回落时首拍一条较长 ocr 行（设计如此）；TextPattern 不支持的旧应用回落 ValuePattern→OCR。

## §3 T-048 桌面件驻带（311d282）

- **实验**（temp_scripts/t048_probe）：①**先 SetParent 进带、后 CreateTargetForHwnd+Commit——硬顺序，反序必隐形**（P-031 同款失败签名，实验 Run1/Run2 对照实证）；②内容更新只走 BeginDraw→QI(IID_ID3D11Texture2D)→UpdateSubresource(bits, bytesPerLine)→EndDraw→Commit，**GDI GetDC 路径隔次静默丢帧（hr=0 但不上屏）**；③真实鼠标永远落图标层（WindowFromPoint→SysListView32），钩子转发 PostMessage 可达；④WE 自适应带内新窗口（120s 稳定、无重建无报错）；⑤0x052C 幂等、本机分裂已存在（WE 触发过）；⑥本机 band 为 Progman 子级形态，归属判定用 **GA_PARENT** 而非 GA_ROOT。
- **DesktopBandHost**：band 定位（Progman→DefView→WorkerW；0x052C 幂等触发 + 3×200ms 重探 + 10s 失败冷却）→ WS_POPUP+WS_EX(NOREDIRECTIONBITMAP|TOOLWINDOW|NOACTIVATE) → SetParent → D3D11 硬件设备 + WARP 兜底 → DComp 链（target→visual→SetRoot→Commit）；上传合并（单事件循环至多一次 Commit，UploadCoalescer 纯状态机）；5s 自纠定时器（GA_PARENT 归属 + 带内 Z 序 stable_sort，漂移才动）；Explorer 重建 → 整链重建按 wasShown 恢复；任一环节失败 → deactivate + bandLost → 调用方降级 DesktopStack（旧贴底+守护）。
- **BandMouseRouter**：共享 WH_MOUSE_LL 新增 addSwallowFilter 谓词通道；回调零耗时（classify + GetAsyncKeyState 按压放行[含 VK_XBUTTON1/2] + GUI_INMENUMODE/INMOVESIZE + Shell 弹层类名白名单 #32768/CoreWindow/XamlIsland；**WindowFromPoint 禁入回调**）；命中→入队（Move 合并）→drain→坐标换算→PostMessage→宿主 WndProc 合成 QMouseEvent/QWheelEvent→childAt 派发（交互逻辑单一来源）；**按下广播在吞噬判定之前无条件执行**（监控/划词不受影响，复审核实）；滚轮路由（mouseData delta 全链符号保持）；双击用 SM_CXDOUBLECLK/SM_CYDOUBLECLK 系统度量；重叠矩形按带内 Z 序取优先。
- **三窗接入**：固定模式 = 真窗隐藏 + presentWidget（grab→ARGB32_Premultiplied→上传）；编辑（✏️）/打字挂起 = 收带 + 真窗可见激活（resume 时复查 bandMode）；**真隐藏 = deactivate**（复审 P1-1 幽灵复活修复的不变量：**activate 只建链、showBand 统一管显示** + degradeFromBand 呈现门按 bandVisible+isVisible 取样）。
- 逃生口：calendar/bandMode、box/bandMode（ini 默认 true，settingsChanged 后 glueIfFixed 重估载体）。
- 已知限制：**盒子 OLE 拖入失效**（带内收不到拖放，未做任何吞 button-up 的危险 hack；待用户决策：保留带形态 / ini 回退 / 折中方案）、带模式无 hover 手型光标、列表拖拽重排无效（用右键上移/下移）、Delete 键失效（无键盘焦点，删除走右键）、迷你条标题拖动不可用、混合 DPI 副屏摆放近似。
- 复审闭环：P1×3（幽灵复活/悬停吞噬冻结拖拽/重绑僵尸）+ P2×3（外来弹层误吞两段式豁免、滚轮路由、探测 3×200ms）+ P3×8 全修；结构冒烟（temp_scripts/t048_smoke_run.py）两次 PASS（GA_PARENT=band、150% 几何精确、零降级误报）。

## §4 T-050 Goody UI 阶段一 + 阶段二（1d85540 + 5a00f7e）

- **阶段一悬浮球**：GoodyBallLogic 纯逻辑（GestureClassifier 6px 曼哈顿阈值闩锁 / clampBallPos 多屏钳制 / ballPos 串往返）；GoodyFloatingBall 全自绘玻璃球（无 QSS，规避 P-022/P-023 通道问题）+ desktopstack::attach(Layer::Ball)（**需求单所写 desktoppin::glueToDesktop 已演进为 ui/shared/DesktopStack，按代码事实接入**）+ WM_MOUSEACTIVATE→MA_NOACTIVATE；GoodyModule::workingChanged(busy‖toolPhase 去重广播) → 1.2s 呼吸光晕（脏矩形，隐藏即停）；聊天窗 show→球 hide、windowHidden→球 show；assistant/ballPos ini **双形态兼容**（未引号逗号值裂变 QStringList → join 还原，→P-039）；设置页「桌面悬浮球」开关（assistant/ballEnabled 默认 true）。
- **阶段二聊天窗**：渲染管线 = contentDelta chunk → MarkdownRenderer.feed（首块免节流 + 150ms 节流线，QElapsedTimer 注入）→ 气泡 appendStreamText 纯文本增量（打字机粒度）→ finish 一次 setMarkdown 定稿 + CodeHighlighter（关键字正则按语言预编译；字符串/注释单遍裁决）+ 代码块悬浮复制钮；思考折叠区 = LlmStreamParser 聚合 reasoning_content（takeReasoningDelta）→ 窗口层**第二道 150ms 节流**（复审 P2-1）→ 折叠头实时 N 字；reasoning 持久化进会话 JSON + **buildChatRequestBody 剥离**（测试钉住）；ChatSession 升 **v2 多会话**（{version,active,sessions[]}，v1 无损迁移、.bad 两代保留、per-session 40 条 trim、truncateFrom/removeMessageAt 配对组语义 + load 期 filterLegalSequence 自愈兜底）；四件套 = 复制 / 编辑重发（截断+预填）/ 重新生成（回到最后一条 user 整体回退 + **m_toolRounds 复位**，复审 P2-2）/ 删除（assistant 连带 reasoning 与 tool 组）；气泡懒构建 + 贴底才自动跟随；工具确认卡片样式并入新设计、epoch 防护照旧；清空守卫 C-N2/B-2 用例零改动通过。
- 复审：阶段一通过（P3×2 顺手修：隐藏期呼吸空转守卫、拖放落点钳制）；阶段二 P2×2+P3×9 → 修 7 跳 2（P3-2 删首条 user 后序列以 assistant(tool_calls) 开头——OpenAI 兼容口径接受；P3-4 流式光标"▍"以真实文本插入——复制会带上；均记已知行为）。**阶段二修复轮未再做独立复审**（修法均为复审给定的一行级改动；晨起发现异常按普通缺陷报）。
- 过程抓虫：chat_session_truncate_and_remove 用例真抓到模块 regenerate 偏移 1 的 bug（当轮修复）。
