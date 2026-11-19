# T-068 鼠标拖动「卡顿→瞬移」取证与根治（2026-11-19）

## 现象与病理

PopTools 开启后拖动中鼠标偶发卡一下再瞬移；关 PopTools 无此现象。病理：三个低级钩子（WH_MOUSE_LL 共享 / WH_KEYBOARD_LL 监控 / 裸键优先）全装 Qt 主线程——RIT 派发低级钩子要等安装线程泵出回调并应答，主线程停摆即全系统输入排队（光标冻结→缓过来一次性放行=瞬移）。

## 取证方法（双路，不动鼠标、不注入事件）

- **进程内探针** `src/core/monitor/MainThreadProbe.{h,cpp}`：钩子回调 RAII 打点（事件戳→回调延迟=泵停摆时长）+ 主线程 10ms 心跳 + 2s 换出 worker 线程落盘（异常行 + 上下文窗前 2s 后 1s + 周期统计行）；阈值钩子 40ms/泵 50ms，同源 500ms 合并；日志 `%LOCALAPPDATA%\PopTools\PopTools\probe\`
- **系统侧监听** `temp_scripts/mouse_stall_monitor/`（Raw Input + 光标轮询 ~770Hz + 候选进程 CPU）：v1 判定"冻结期输入仍在到达"全程零事件——钩子等待期 RIT 连 Raw Input 一并停摆，预设不成立；v2 改"冻结前 0.3s 有输入"，rawAlsoStalled 标志区分形态
- **对时**：双方 meta 记 QPC/GetTickCount/墙钟三时钟起点（qpcFreq=10MHz），日志按 QPC 精确对齐

## 根因（探针 2h 455 条事件实锤）

1. **30s 壁纸定时器**（CalendarPanel/BoxPanel 各一）主线程全量重算（磁盘整图解码+1/4 降采样+BoxBlur 两遍+放大回全尺寸）~158-172ms；20s 缓存 30s 刷新=每轮必算——主犯（事件时刻严格 30s 间隔实录：12:05:05→:35→06:05→06:35）
2. **IME 快照兜底截屏**（startOcrCapture → ScreenCapturer::capture = QScreen::grabWindow + QPixmap 拼接 + toImage）主线程数十 ms，打字时段密集（同刻 RapidOCR-json CPU 27~35%）
3. **开机月报 31 天文件拼接**（MonthlyRecordJob）主线程 ~380ms（冷缓存启动实录）

## 修复要点（ctest 100% 绿，2 提交 9ffa3b4 + 2cb237b）

- DesktopWallpaperProvider：wallpaperSignature 签名免重算（stat 级零解码，未变只续 20s 时基）+ computeBlurred 静态无状态（线程池可调）+ adoptFrom 主线程收成品；双面板 30s 定时器改 QThreadPool 异步（QPointer 交付 + m_wallpaperBusy 重入护栏）
- ScreenCapturer::captureVirtualDesktop：纯 GDI BitBlt（CAPTUREBLT 含分层窗口）直出 QImage，任意线程可调；startOcrCapture 整段进池——密码复查紧贴截屏的 T-047-FIX(P2-1) 时序不变式随迁池内；PasswordFieldGuard g_cache 加 QMutex（isPasswordWindow 自此双侧调用）
- MonthlyRecordJob：buildMonthDigest 纯函数进池，sendMonthlyRequest 主线程续段（done 回调随链路带回）；m_busy 闸门扩到聚合窗口

## 验证

新进程探针 95s 覆盖两个定时器周期：**30s 停摆归零**；残余仅启动首帧壁纸同步计算 400ms 一次（blurredWallpaper 缓存 miss 同步路径为 T-059 视觉语义保留）。perf 基线 2026-11-19：启动 324ms/主进程 159.2MB（旧快照 54~66ms/35MB 为 T-047 时代数字；增量主要为日历+收纳盒共享画布等后续功能累计，非本轮引入——待后续轮次复核）。

## 遗留

- 启动首帧 400ms：根治需改首帧视觉（先灰玻璃再异步补壁纸），待用户拍板
- 探针随验收期保留运行；用户确认不再复现后整体删除（MainThreadProbe + 三钩子打点 + MainWindow 一行 + 监听器目录）
- 发版：v0.5.1，Release id 451052105，资产 PopTools-Setup-0.5.1-20261119_1618.exe（56,294,615 B），tag v0.5.1
