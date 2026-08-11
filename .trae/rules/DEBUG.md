# DEBUG 记录

## 1. Qt 6.12.0 无法用 aqt 3.3.0 安装
- **现象**：`aqt install-qt windows desktop 6.12.0` 报错 `Failed to locate XML data for Qt version '6.12.0'`，实际 URL 指向 `qt6_6120/qt6_6120/Updates.xml`（404）
- **根因**：Qt 6.12 起官方仓库改为新目录布局：`qt6_6120/` 根目录 + `qt6_6120_msvc2022_64/` 架构子目录（目录名含完整补丁号）。aqt 3.3.0 仍按旧布局 `qt6_612/` 拼接 URL
- **解决**：改用 Qt 6.8.3 LTS（旧布局，aqt 3.3.0 兼容，LTS 稳定，适合个人项目）
- **教训**：aqt 与最新 Qt 版本存在兼容滞后；使用 LTS 版本更稳妥

## 2. 命令行直接运行 cl.exe 报错
- **现象**：PowerShell 中 `cl` 无法识别
- **根因**：MSVC 工具链不在 PATH，需通过 `vcvarsall.bat x64` 或 CMake 自动配置环境变量（INCLUDE/LIB/PATH）
- **解决**：本项目全程用 CMake 配置 MSVC，无需手动 vcvars

## 3. F 盘消失，Qt 需要重新安装（aqt）
- **现象**：CMakeLists 中 `F:/Qt/6.8.3/msvc2022_64` 不存在，`find_package(Qt6)` 失败；`-m qtbase qtsvg` 报 `The packages ['qtbase','qtsvg'] were not found`
- **根因**：① 原 Qt 装在 F 盘但该盘已不存在；② aqt 的 `-m` 只接受"附加模块"，`qtbase` 是基础包会自动安装；③ Qt 6.8 起 `qtsvg` 已并入 qtbase（`aqt list-qt --modules` 中不再有 qtsvg 条目）
- **解决**：`aqt install-qt windows desktop 6.8.3 win64_msvc2022_64 -O D:\Qt`（不指定 -m）装到 `D:\Qt\6.8.3\msvc2022_64`
- **经验**：Qt 路径从 CMakeLists 硬编码改为环境变量 `QT_ROOT`（env > -DQT_ROOT > 默认路径三级回退），并用 `[Environment]::SetEnvironmentVariable(..., "User")` 持久化

## 4. CMake 4.2.1 需用 VS2026 生成器
- **现象**：README 中的 `-G "Visual Studio 17 2022"` 与机器上的 MSVC 2026（VS18）不符
- **解决**：改用 `-G "Visual Studio 18 2026" -A x64`，编译通过

## 5. 工具栏图标全部消失 / 选中后所有按钮变成同一图标
- **现象**：未选工具时（选择模式）9 个工具按钮图标全部不显示；点选某工具后所有按钮图标都变成该工具
- **根因**：`ToolBar::setTool` 中循环给每个按钮重设图标时，图标名取自**当前工具参数 `t`**（同一个值）而不是按钮自身的 tool 属性；当 `t == None` 时 switch 返回空串，`QSvgRenderer` 加载失败 → 空图标
- **解决**：图标名改为取 `btn->property("tool")` 对应的工具（`btnTool`），每个按钮恢复各自图标，仅高亮色（白/主题色）随选中状态变化
- **教训**：批量刷新按钮状态时，图标/文案等按按钮本身属性生成，不要复用全局当前值

## 6. 矩形/椭圆向左上角拖动时起始点随鼠标漂移
- **现象**：向右下角拖动正常；向左上角拖动时矩形/椭圆的"起始点"跟随鼠标一起往上跑
- **根因**：`continueDraw` 用 `QRectF(m_draft.rect.topLeft(), pos).normalized()` 更新草稿，锚点取的是**当前规范化后矩形**的 topLeft；当鼠标越过锚点的 x/y（反向拖动）时，锚点会漂移到新的 min 角，逐次累加
- **解决**：新增 `m_draftOrigin`（按下点）作为固定锚点，`QRectF(m_draftOrigin, pos).normalized()`
- **教训**：拖拽图形必须以"按下点"为固定锚点，不能依赖每次归一化后的矩形角

## 7. 三个线宽档位区分度不足 + 铅笔/涂鸦/马赛克共用同一套线宽
- **现象**：三个粗细画出的矩形/椭圆/线条粗细看不出区别；线宽选项的圆点大小也应不同
- **根因**：原档位 1.5/3.0/6.0 太接近；线宽图标为横向线条，1.5px 与 3px 几乎无差别；且所有工具共用一套线宽
- **解决**：
  - 线宽档位改为按工具区分：普通工具 {2,5,12}、铅笔 {1,2,4}、涂鸦 {6,12,20}、马赛克（笔刷直径）{12,20,32}；铅笔最粗(4) 仍细于涂鸦最细(6)
  - 线宽图标改为**圆点**，半径按档位内相对值缩放（2..7px），档位间清晰可辨
  - `ToolBar::setTool` 切换工具时刷新线宽档位，保持相对档位 `m_widthIndex`，并 emit widthSelected 同步画布
- **教训**：UI 数值档位要拉开差距，图标要能体现相对大小

## 8. 马赛克由"框选区域"改为"笔刷涂抹"
- **现象/需求**：马赛克应是笔刷涂抹效果，而非拖拽框选整个矩形区域像素化
- **实现**：马赛克改为折线笔迹（`points` + 笔刷直径 `width`），`buildMosaicImage` 截取笔迹外接矩形源图整体像素化后，用圆头笔刷足迹 mask（`CompositionMode_DestinationIn`）只保留沿线区域，其余透明；拖动时实时重建
- **连带**：马赛克命中测试从"外接矩形"改为"到笔迹折线距离"（`distanceToItem`）

## 9. 涂鸦笔（Doodle）需半透明体现高亮
- **实现**：`startDraw` 中 Doodle 颜色加 alpha=100，铅笔保持不透明；两者底层均为折线绘制

## 10. 未填充矩形/椭圆内部显示为黑色（应为透明）
- **现象**：关闭填充后画矩形/椭圆，内部是黑色而非透出截图
- **根因（正解）**：`AnnotationCanvas::drawOne` 中 `p.setBrush(it.filled ? it.color : Qt::NoBrush)` —— C++ 三元运算符把 `it.color`（QColor）与 `Qt::NoBrush`（无作用域枚举，值 0）统一为 `QColor` 类型，`Qt::NoBrush` 经 `QColor(int)` 隐式转换为 **QColor(0) = 纯黑画刷**，于是未填充矩形实际上用黑色画刷 fill。此前的 setClipRect 方案被证伪：数据、蒙版、打底均正常，黑色只产生于 paintEvent 管线内
- **验证**：插桩日志（drawOne 参数全对，brushStyle=0）→ m_screen 原图内部 0 黑点 vs 实时渲染内部 2553 黑点（锁定 paintEvent）→ `fillRect(Qt::green)` 被盖掉（确有黑画刷）→ `setBrush(Qt::green)` 显示绿色（setBrush 生效）→ 最终锁定三元运算符类型强制转换
- **解决**：改为显式 QBrush：`p.setBrush(it.filled ? QBrush(it.color) : QBrush(Qt::NoBrush));`（与 ToolBar.cpp 图标绘制一致）。修复后矩形内部与选区无标注处像素一致（rgb 223,213,201 vs 226,216,200），自动化复现判定 TRANSPARENT_OK
- **连带发现（低优先级）**：`confirm()/onCopy()` 中 `setClipRect(sel)` 用全局坐标，随后 `translate(-crop.topLeft())` 把原点移到屏幕原点，clip 退化为单点，导致结果图中矩形被整条裁剪掉；结果图无矩形（非黑色问题，未一并修复）
- **教训**：条件运算符混用 `Qt::NoBrush/NoPen` 等枚举与 `QColor/QPen` 时，必须显式 `QBrush(...)` 包装，否则枚举被隐式转成 QColor(0) 纯黑/纯色

## 11. 马赛克绘制出现"黑色填充矩形 + 内部透明路径"
- **现象**：用马赛克笔刷涂抹时，笔迹 AABB 包围盒内出现一个黑色填充矩形，矩形内部笔迹路径处几乎透明
- **根因**：`buildMosaicImage` 中 `region.toImage()` 在 Windows 返回 **Format_RGB32（无 alpha 通道）**。对无 alpha 的图像执行 `CompositionMode_DestinationIn` 时，Qt 在预乘空间按源（mask）的 alpha 乘 RGB 通道：mask 透明处（框内非笔迹区域）RGB 被乘成 **0 = 纯黑**，而 RGB32 无 alpha 永不透明 → 黑色填充矩形；笔迹处 alpha=255，RGB 保留 → 看似"透明路径"。mask 本身完全正确
- **验证**：插桩保存中间产物：src(mode=RGB 内容正常 0 黑点)、mask(RGBA 正确：笔迹白线+其余透明)、mosaic(**mode=RGB 全不透明 84% 纯黑**) → 锁定合成目标格式错误
- **解决**：合成前转换格式：`small.scaled(...).convertToFormat(QImage::Format_ARGB32_Premultiplied)`。修复后 AABB 内黑点 22792 → 0，四角恢复原始内容色
- **经验**：`QPixmap::toImage()` 在 Windows 返回 RGB32（无 alpha）；凡是后续要做 alpha 合成（DestinationIn/Out 等）的图像，必须先 `convertToFormat(Format_ARGB32_Premultiplied)`

## 12. 确认/复制/保存后结果图丢失全部标注
- **现象**：画完标注后按 Enter 确认（或点复制/保存），结果图/剪贴板图里没有标注（此前矩形测试"结果图无红无黑"）
- **根因**：`confirm()/onSave()/onCopy()` 中 `p.translate(-crop.topLeft())`。result 像素 (0,0) ↔ m_screen 坐标 crop.topLeft()，标注（选区局部坐标 p）的 m_screen 坐标为 crop.topLeft()+p → 正确结果像素是 **p**；translate 后画到结果像素 p-crop.topLeft()（负坐标）→ 整条被裁剪掉
- **解决**：删除三处 `p.translate(-crop.topLeft())`。验证：红色矩形确认后结果图红色 bbox x[65,658] y[65,350]（起点 65≈预期 67），修复前 0 红像素
- **教训**：合成结果图时先推坐标映射：目标像素 ↔ 源图像素 ↔ 标注坐标三者关系；result 是选区裁剪，标注已是选区局部坐标，无需再平移

## 13. 截屏掉分辨率：应始终输出物理分辨率（DPI 缩放）
- **现象**：任意机器上截屏都应按原始分辨率输出；本机 2560×1600 物理屏（144% 缩放）只截到 1707×1067
- **根因**：`startCapture()` 用 `QScreen::geometry()`（**逻辑**坐标）合并虚拟桌面后 `QPixmap(m_desktop.size())` 创建 **逻辑尺寸** 画布；而 `grabWindow(0)` 返回 **物理像素** 图（带 dpr），`drawPixmap` 按 dpr 压缩进逻辑画布 → m_screen 只剩逻辑分辨率
- **解决**：
  - `startCapture()` 按物理像素拼接：`screenPhysicalRect(s)` = 逻辑几何 × devicePixelRatio（Qt 6.8 的 QScreen 无 `nativeGeometry()`），grabWindow 结果 `setDevicePixelRatio(1.0)` 后按物理偏移 1:1 画入物理尺寸 m_screen
  - 新增 `toPhysical()/selectionToPhysical()`：逻辑全局点 → 物理像素（按点所在屏幕 dpr 换算）
  - `onSave()/onCopy()/confirm()`：从 m_screen 按物理矩形裁剪，标注用 `p.scale(k,k)` 等比放大（k=物理宽/逻辑宽）
  - `buildMosaicImage()`：源区域与笔迹 mask 均改物理像素（mask 与 mosaic 同尺寸，画笔/坐标按 dpr 缩放）；`drawOne()` 中 mosaic 用 `drawImage(it.rect, image)` 缩放到逻辑矩形
  - `paintEvent` 加 `SmoothPixmapTransform`（物理原图缩放到逻辑窗口时平滑），尺寸提示显示物理分辨率
- **验证**：自动化 GUI 测试全屏框选 → 结果 2461×1501（期望≈2460×1500；修复前约 1640×1000）；红色矩形 bbox 按 dpr 等比放大正确（误差 ±4px）；马赛克无黑框回归（最大连通黑块占比 5.6%）
- **教训**：Qt 全屏截图存在两个坐标系——**逻辑坐标**（窗口/鼠标/选区）与**物理像素**（grabWindow 原图），输出时必须在边界转换；坐标换算统一按"屏幕逻辑原点×dpr + 屏幕内偏移×dpr"，且 `QRect` 右/下边界含端点，换算用 (right+1, bottom+1) 避免丢像素

## 14. 工具栏底衬丢失（命名空间导致样式表类型选择器失配）
- **现象**：重构引入 `namespace snipaste` 后，工具栏灰白/深灰背景与边框消失
- **根因**：moc 生成的 `QMetaObject::className()` 为 `"snipaste::ToolBar"`，样式表类型选择器 `ToolBar { ... }` 不再匹配（Qt 样式表类型选择器按 className 匹配，`: 与 ::` 有特殊含义无法直接写限定名）
- **解决**：`setObjectName("annotationToolBar")` + 样式表改用 ID 选择器 `#annotationToolBar { ... }`，与命名空间解耦
- **验证**：自动化 GUI 测试（F1 → 框选 → 截屏检测）检测到工具栏背景色像素（浅 6564 / 深 16080 采样点 > 阈值）→ TOOLBAR_BG_OK
- **教训**：命名空间内的 QObject 子类（Q_OBJECT）若用样式表设置自身外观，不能用类型选择器，须用 objectName ID 选择器；Qt 全局类（QToolButton 等）不受影响

## 15. "撤销栈顺序颠倒"Issue 验证为误报
- **现象（误报）**：`addItem` 被指"先 push_back(items) 快照再添加新项，顺序颠倒，撤销后应保留新项"
- **验证**：① 静态分析：`addItem` 顺序为 `m_undoStack.push_back(items)`（保存不含新项快照）→ `items.push_back(item)`，语义正确；用户提供的"修复建议"代码与现有一致，无实际差异 ② 运行时测试（链接 snipaste_core，`scripts/undo_test/test_undo.cpp`）：add A+B → undo 回到 [A]（新项被移除）→ 二次 undo 空 → redo 恢复 [A]，PASS
- **结论**：无需修复。误报排查成本较高，特此记录避免重复验证

## 16. 鼠标悬停工具栏仍显示十字光标（应为箭头）
- **现象**：进入编辑态后，鼠标移动到标注工具栏上光标仍是十字（或选区尺寸/移动光标），不是正常箭头
- **根因**：`ToolBar` 是 `CaptureWindow`（全屏窗口）的子 widget，构造时未设置自身光标。Qt 光标解析规则：鼠标悬停于某 widget 时，沿其祖先链向上找**第一个显式设置光标（WA_SetCursor）**的 widget —— 找到了父窗口的 `CrossCursor`；且 `CaptureWindow::mouseMoveEvent` 在编辑态未按下时持续 `setCursor(...)` 覆盖整窗，工具栏因无自身光标一并继承
- **解决**：`ToolBar` 构造函数开头 `setCursor(Qt::ArrowCursor)`，工具栏自身携带 WA_SetCursor，悬停其上 Qt 即用箭头光标，不受父窗口光标影响
- **教训**：子 widget 若与父窗口光标需求不同，必须在子控件上显式 `setCursor`；依赖"继承父光标"在 QWidget 上是沿祖先链找显式设置项，父窗口频繁 setCursor 会波及所有未设置光标的子控件

## 17. 标注控制点编辑增强：镜像缩放 + 线段端点控制（含 OOP 重构）
- **现象/需求**：
  - 矩形/椭圆在选择模式下拖动边控制点，超过对侧起始边后被 clamp 住无法继续拖动（`resizeRectF` 用 `qMin(pos, right-minSize)` 硬限制），期望 Visio 式"拖过对侧 → 镜像翻转"
  - 直线/箭头选中态被 8 个控制点的外接矩形包围，期望只显示起点/终点两个控制点，可自由拖动改变长度与指向
  - 工具交互集中在一个巨型 switch（startDraw/continueDraw/finishDraw），期望面向对象 + 设计模式化
- **解决**：
  1. `AnnotationPainter` 新增 `Handle` 枚举（TL..L 8 角/边中点 + **P0/P1 线段端点**）与 `handlesFor()` / `handlePos()` / `resizedItem()`：
     - 矩形类 8 控制点镜像缩放：`resizeAxis()` 按"被拖动边跟随鼠标 + 越过对侧起始边（anchor±minSize）即翻转（left/right 互换）"处理，每轴独立判定 xFlip/yFlip
     - `scaledItem()` 增加 xFlip/yFlip 参数，翻转轴按"新边界反方向映射"折线点，产生镜像
     - 线段类 P0/P1 直接改端点
  2. `CaptureWindow` 选择模式改用统一控制点接口：选中态绘制/命中/光标均按 `handlesFor` 类型化（线段类不画外接框，仅两个端点方块）
  3. 工具体系 OOP 化（策略模式 + 简单工厂）：新增 `ui/capture/tools/` —— `ToolBase` 抽象基类（onPress/onDrag/onRelease/paintDraft）+ `ShapeTool`(Rect/Ellipse) + `SegmentTool`(Line/Arrow) + `FreehandTool`(Pencil/Doodle) + `MosaicTool` + `TextTool` + `EraserTool` + `ToolFactory::create(Tool, ToolContext*)`；工具通过 `ToolContext`（画板/屏幕/工具栏 + 回调）与窗口解耦，替换原 switch 分发
  4. `ToolBar` 单例化（`ToolBar::create(parent)` / `ToolBar::instance()`）
- **验证**：核心逻辑单元测试（`scripts/tool_test/test_tools.cpp` 链接 snipaste_core）：拖 L 越过右边界 left=110→right=150（翻转）、未越界保持锚定、TL 双轴翻转、折线镜像点映射（原最左→新最右）、线段 P0/P1 端点编辑、handlesFor 类型化 → ALL TOOL TESTS PASSED
- **教训**：① 缩放的"镜像翻转"必须基于**起始矩形快照 + 固定拖动边方向**判定，不能基于当前矩形逐帧 clamp（否则翻转后无法继续拖）；② 控制点体系应按标注类型抽象（handlesFor/handlePos），窗口交互与标注类型解耦；③ 翻转后内部点映射要沿翻转轴反向，不能只缩放 newBounds

## 18. Tesseract 打包方案：仓库内置裁剪版资源，构建/安装时直接复制
- **需求**：OCR 依赖 tesseract.exe，希望在开发中直接可用且随安装包一起发放；但 tesseract 依赖链巨大（UB-Mannheim 构建 libtesseract-5.dll≈97MB + libicu≈35MB 等，全量 ~200MB）
- **方案演变**（三次迭代，最终由用户拍板"直接下载好当资源"）：
  1. ~~随安装包分发~~：体积太大不划算
  2. ~~安装期联网安装到 `{exeDir}/tesseract/`~~：安装程序未实现，搁置；且 NSIS `/S /D=` 静默安装到指定目录**不可靠**
  3. **仓库内置资源（最终）**：tesseract 官方安装包解包后，将 `tesseract.exe` + 全部依赖 DLL + `tessdata\`（中英文语言包）放入 `tools/tesseract/`，CMake POST_BUILD `copy_directory` 复制到 `$<TARGET_FILE_DIR>/tesseract`，开发/安装同布局
- **实施细节**：
  - 解包：7-Zip 可直接打开 NSIS 安装包（`7z x tesseract-setup.exe`）；UB-Mannheim v5.4.0.20240606
  - 语言包：fast 版 `chi_sim`(2.35MB) + `eng`(3.92MB) 足够（`-l chi_sim+eng --psm 6`），无需 osd/pdf 输出
  - 依赖：**全量复制解包目录 DLL（51 个）** 最稳妥——此前 `prepare_tesseract.py` 的 PE 导入表裁剪解析失败（0 个 DLL），放弃裁剪路线
  - 运行时：`OcrService` 固定从 `{exeDir}/tesseract/tesseract.exe` 加载（不允许用户改路径，`Settings::tesseractPath()` 已移除）；`TESSDATA_PREFIX` 优先 `{exeDir}/tesseract/tessdata`
  - 验证：`tesseract.exe --version` 正常加载全部依赖；真实 OCR 冒烟测试 `Hello 你好世界 2026` 一字不差；构建后 `build\Release\tesseract\` 完整就位
- **清理**：卸载 Program Files 误装、删除临时解包目录（`_pkg`/`_t7z`）；CMake 复制规则按需启用
- **教训**：① NSIS `/S /D=` 静默安装到指定目录**不可靠**，7-Zip 解包 NSIS 安装包是便携方案；② 引入体积大的第三方二进制先评估"随包分发 vs 安装期联网 vs 内置资源"三条路线，内置资源对个人项目最简单可靠；③ 纯 Python 解析 PE 导入表做依赖裁剪容易踩坑（RVA→offset 映射、section 对齐），失败时全量复制 DLL 是可靠回退

## 19. 【文】点击报错 "Can not read input file" + 识别结果永远为空（两个叠加 bug）
- **现象**：编辑态点工具栏【文】按钮，弹出 tesseract 报错 `Can not read input file xxx`；修复该问题后又出现"识别永远为空/退出码 0"
- **Bug A：QTemporaryDir 生命周期** —— `OcrService::start()` 中 `QTemporaryDir dir;` 是**局部变量**，start() 返回即被析构删除目录；QProcess 是异步的，tesseract 实际读取 input.png 时文件已被删除 → `Can not read input file`。同时中间文件落在系统临时目录，不符合"放软件目录 + 用完清理"
- **Bug B：TSV 行尾 CRLF** —— tesseract TSV 在 Windows 输出行尾为 `\r\n`，`split('\n')` 后 header 末列 `text` 变成 `text\r`，`header.indexOf("text")` 返回 **-1** → 解析函数直接返回空 → `ok=false` 且 stderr 为空 → 报"退出码 0"。该 bug 一直潜伏，此前被 Bug A（文件先没了）掩盖，修复 A 后才暴露
- **解决**：
  - Bug A：废除局部 QTemporaryDir，改为成员 `m_workDir`（`{exeDir}/ocr_tmp/<uuid>`）保存路径，识别结束后（onFinished 两分支 / errorOccurred）经 `cleanupWorkDir()` 递归删除，并顺手 rmdir 空壳父目录 ocr_tmp
  - Bug B：解析前 `QString::fromUtf8(raw).remove('\r').split('\n')`
- **验证**：集成冒烟测试 `scripts/ocr_test/test_ocr.cpp`（链接 snipaste_core，输出到 build/Release 复用 tesseract）：`OCR_OK boxes=1 text="Hello你好世界2026"`（连成一段一个框）+ `WORK_DIR_CLEAN_OK`（exe 旁无 ocr_tmp 残留）→ 全 PASS
- **教训**：① 异步 QProcess + 临时目录，目录/文件生命周期必须跨异步调用存活（成员变量或 RAII 延长到 finished）；② 解析文本协议（TSV/CSV）在 Windows 必须先处理 CRLF，用 `split('\n')` + 精确 `indexOf` 匹配列名极易踩坑；③ 表面 bug 修复后可能暴露被掩盖的下一层 bug，冒烟测试要在真实环境（含输出编码/行尾差异）回归

## 20. OCR 覆盖文字字号与原文字匹配（QFontMetricsF 实测逼近）
- **需求**：【文】识别的文本框覆盖原图时，覆盖字体大小应与原始文字大小相当
- **问题**：原实现 `pixelSize = 0.8 × 行框高度` 是纯经验系数，不同字体/行高（含 leading）下偏差大，无法保证"差不多"
- **解决**：`CaptureWindow::onOcrBoxes` 改用实测迭代：
  1. 粗估 `pixelSize ≈ 0.8 × local.height()`，用 `QFontMetricsF(f).height()` 实测
  2. 比例修正一次：`pixelSize *= 目标高度 / 实测高度`（字体高度与 pixelSize 近似线性，一次即入 ±2px）
  3. 精调循环（上限 20 次）：实测高度 > 目标则减 1px，比目标小 1px 以上则加 1px，落在 `[目标-1, 目标]` 区间——覆盖文字既不明显大于也不明显小于原字
- **验证**：Release 构建通过；效果需实机 F1 → 框选 → 点【文】观察覆盖文字与背景原字的大小对比
- **关联**：`AnnotationPainter::scaledItem` 缩放 Text 时用 `pointSizeF`（若字号是 pixelSize 设置，pointSizeF() 为 -1，缩放会退回 4pt）——像素字号体系与点字号缩放混用的已知隐患，未在本轮处理

## 21. 文字工具输入框改造：QLineEdit → 多行 QTextEdit（透明底虚线框 + 内容自适应 + 点击框外结束）
- **需求**：① 文本框始终 长=最长行宽、高=总行高；② 输入中无白底、透明 + 虚线标注输入区域（初始给初始长宽，输入后贴合内容）；③ 多行文本框，回车/Shift+回车均换行，输入结束方式为点击框外而非回车
- **实现**（`CaptureWindow`）：
  - `m_textEdit` 由 `QLineEdit` 改为 `QTextEdit`：`NoWrap`（宽=最长行）+ 双滚动条 AlwaysOff + `setDocumentMargin(0)`（document size 即文本自然尺寸）；`textChanged` → `adjustTextEditSize()`
  - `adjustTextEditSize()`：`document()->size()` 取内容尺寸，`w=qMax(240, ceil(w)+6)`、`h=qMax(行高, ceil(h)+4)`——空文档时为初始长宽（240×单行高），一旦输入即贴合内容
  - 样式：`background: transparent; border: 1px dashed #0078ff; color=<标注色>`（去掉原白色底）
  - `commitTextEdit()`：尺寸改用 `document()->size()`（多行：宽=最长行，高=总行高），提交前先记录 content 与 font（clear 后 document 已空）
  - 结束方式：移除 `returnPressed` 连接；回车/Shift+回车由 QTextEdit 原生换行；点击框外经 `mousePressEvent` 的 `if(m_textEditing) commitTextEdit()` 提交（工具栏 undo/redo/取色/线宽/填充 各信号入口也补了提交，防状态悬挂）
  - 顺序陷阱：`beginTextEdit` 必须**先 setFont 再 setPlainText**，否则文档字符格式沿用旧默认字体
- **验证**：Release 构建通过；需实机：T → 点击输入 → 观察虚线框自适应与多行回车 → 点击框外保存
- **教训**：QTextEdit 尺寸自适应应以 `document()->size()`（无 wrap + margin 0）为准，它由与 QPainter::drawText 相同的 QTextLayout 引擎计算，与最终标注矩形天然一致

## 22. 文字多字号（字号栏）：AnnotationItem 富文本化 + 工具栏字号栏（pt）+ 字号实时联动
- **需求**：① 文本框不许调整长宽，长宽由内容自动计算（输入第一个字后实时跟随）；② 第二行工具栏新增文字大小栏（pt，多选下拉+手动输入，合法性验证）；③ 字号栏永远体现"当前所选中或所编辑文本的大小"：选中多个不同大小文字→不显示；统一大小→显示该值；修改提交后立即作用于选中文字；无选中→显示"接下来输入文字的字号"
- **数据层**：`AnnotationItem` 新增 `QString html`（富文本，多字号）；`text` 保留纯文本、`font` 保留默认字体（兼容旧数据/OCR 项，html 为空走纯文本路径）
- **渲染层**（`AnnotationPainter::drawOne` Text 分支）：
  - html 非空 → `QTextDocument`：`setDefaultFont(it.font)` + `setHtml` + `setDocumentMargin(0)` + `setTextWidth(-1)`（不换行，与输入框 NoWrap 所见一致）+ `documentLayout()->draw`（palette Text = it.color 兜底）
  - `handlesFor(Text)` 返回**空** → 文字标注不再显示 8 个缩放控制点（长宽由内容决定，不可手动调整；拖动框内仍可整体移动）
- **工具栏**（`ToolBar`）：
  - 第二行"字号"标签 + `QComboBox`（`setEditable(true)` + `NoInsert`，预设 8/9/10/12/14/16/18/24/36/48，`QDoubleValidator(1.0~500.0, 1 位小数)`）
  - 提交入口：`activated`（下拉选）+ `lineEdit()->returnPressed` + `lineEdit()->editingFinished`；非法输入不发射信号并把显示回退为当前有效值；`syncFontSizeDisplay()` 用 `m_updatingFontSize` 保护，程序性刷新不触发提交
- **交互层**（`CaptureWindow`）：
  - `updateFontSizeDisplay()`：输入态有选区→扫描选区片段字号（交集判定，多字号 `clearFontSize`/统一 `setFontSize`）；无选区→显示光标处当前格式（`currentCharFormat()`，即后续输入字号）；选中文字标注→扫描整条 html；其余→`m_defaultFontSize`
  - `onFontSizeSelected(pt)` 三态：输入态→`m_textEdit->setFontPointSize(pt)`（有选区立即改选中文字，无选区设后续输入字号）+ `adjustTextEditSize()`；选中文字标注→`applyFontSizeToItem`（QTextDocument 全选 mergeCharFormat → 重算框尺寸）+ 压撤销栈；无选中→仅更新 `m_defaultFontSize`
  - `commitTextEdit`：提交前记录 `toHtml()`（clear 后文档已空）；新建/重编辑均存 html，font 取 `document()->defaultFont()`
  - `adjustTextEditSize`：空框保持 240×单行高，输入后宽=最长行、高=总行高（去掉原 `qMax(240, …)` 下限，输入首个字后即贴合内容）
  - `beginTextEdit`：新建默认字号 = `m_defaultFontSize`；重编辑还原 html；**OCR 像素字号（px）换算为 pt（×72/96）** 后再进富文本
- **验证**（`scripts/qt_font_test` 实测 + 构建 + 核心单测 tool_test 无回归）：
  - `setTextWidth(-1)` = 默认 = 不换行（自然尺寸）；`setTextWidth(50)` 才换行
  - QTextEdit 样式表 `color` **不嵌入** `toHtml()`（body 无颜色）→ 富文本渲染可用 palette Text 兜底标注色
  - pt 字号 `toHtml/setHtml` 精确往返；`font-size:20px` → `pixelSize=20`；pt→px 固定 96 DPI
- **坑**：① `QComboBox` 无 `editingFinished` 信号，须用 `lineEdit()->editingFinished()`；② 像素字号字体 `pointSizeF()` 为 -1，读取/回写前必须换算 pt；③ 富文本片段若无显式字号（继承文档默认）`fontPointSize()` 为 0，显示逻辑需回退到文档默认字体
- **Bug（修复）**：输入态无选区时改字号不生效于下一个文本框——`onFontSizeSelected` 输入态分支只 `setFontPointSize(pt)`（改 text edit 当前格式），未同步 `m_defaultFontSize`；而 `beginTextEdit` 新建文本框用 `m_defaultFontSize`，导致"文字工具→输入12→未退出直接改14→再次点击屏幕仍输入12"。修复：输入态**无选区**时同步 `m_defaultFontSize = pt`（有选区只改选中文字，不动默认字号）
- **Bug（修复）**：文字重编辑时原标注与编辑框双重显示——选择模式双击文字进入编辑后，`paintEvent` 仍绘制原标注项（绿色选中虚线框 + 文字），与蓝色编辑框（含相同文字）同位置重叠。修复：`paintEvent` 编辑态分支，当 `m_textEditing && m_textEditItem>=0` 时从 `m_canvas.items` 复制后 `removeAt(m_textEditItem)` 再绘制；同时跳过该选中项的绿色选中框绘制（`!(m_textEditing && m_textEditItem == m_selectedItem)`）。`renderResult` 无需处理（保存/复制/确认入口均已先 commitTextEdit）
- **Bug（修复）**：文字颜色的应用方式与字号不一致 + 改字号/颜色后无法继续输入——① `onColorSelected` 输入态会 `commitTextEdit()` 退出编辑（取色即结束输入），与字号（输入态只改格式不提交）不一致；② 输入态点字号/色块后焦点留在工具栏控件上，用户直接打字会输进 combo 而不是编辑框，误以为"编辑框退出"。修复：
  - 颜色与字号语义统一——`onColorSelected` 输入态**不提交**：`m_textEdit->setTextColor(c)`（有选区→立即改选中文字颜色；无选区→设为后续输入文字颜色）+ 同步 `m_canvas.color`
  - 改字号/颜色后 `m_textEdit->setFocus()` 把焦点还给编辑框，用户可立即继续输入
  - 新增 `m_textEditStartColor`（输入开始色）：样式表默认色与提交后渲染兜底色 `it.color` 均用开始色——输入期间改色不影响已输入文字（无显式色片段渲染用开始色）；显式色（setTextColor/选区改色）嵌入 html 精确还原。编辑中显示与提交后渲染完全一致
- **经验**：QTextEdit 样式表 `color` 是"默认色"，不嵌入 toHtml；`setTextColor`/mergeCharFormat 设置的**显式前景色**才嵌入 html → 天然支持"每段文字各自颜色"：开始色走 it.color 兜底、后续改色走显式色嵌入，无需额外规范化

## 23. 重构：CaptureWindow"上帝类"拆分为门面 + 控制器组件（组合模式 + 依赖注入）
- **问题**：`CaptureWindow.cpp` 1487 行 / 40 函数，承担窗口状态机、选区控制点几何、标注交互、文字编辑、OCR、输出合成 6 类职责，是典型上帝类；其余文件分层合理（core 纯逻辑 / ui / tools 策略模式）
- **方案（用户拍板）**：窗口只留一个接口（门面），功能在不同文件中实现并注册到接口
- **实现**（`ui/capture/` 扁平结构）：
  - `CaptureWindow` 瘦身为**门面 + 事件路由**（1487→569 行）：窗口状态机（Idle→Selecting→Editing）、鼠标/键盘事件分发、绘制编排、控制器装配；对外接口不变（`startCapture`/`captured`）
  - 新增 `CaptureContext`：共享状态（cap/canvas/toolbar/selection/selectedItem 指针）+ 窗口回调（requestUpdate/stateChanged/selectionChanged/exitRequested/beginTextEdit/hasDraft/paintDraft）——各控制器经此依赖注入，与窗口解耦
  - 新增 6 个控制器：`SelectionController`（框选/移动/控制点 + 几何纯函数 + 蒙版/选区绘制）、`AnnotationEditor`（标注选中/拖动/缩放/删除 + 文字双击重编辑）、`ToolDrawingController`（工具三阶段 + ToolContext 组装 + 马赛克）、`TextEditingController`（文字编辑全部 + 字号/颜色三态联动）、`OcrController`（OCR 流程 + 字体匹配）、`OutputController`（结果合成/保存/复制）
  - 构造顺序：窗口填 `m_ctx` 字段 → `make_unique` 注册控制器 → `init()` 创建输入框 → 连接工具栏信号路由到控制器
- **坑**：
  - `connect(...)` 是 `QObject` 静态成员，在**非 QObject 类**（各 Controller）中必须写 `QObject::connect`，否则 C3861
  - `CaptureWindow.h` 若成员含 `CaptureContext` 值对象，必须 include 其头文件（前向声明不够，C2079）
  - 文字提交删除分支（空文本删标注）也要 `stateChanged()` 同步撤销按钮，否则删除后 undo 按钮状态不更新
- **验证**：Release 构建通过；核心单测 `tool_test` ALL PASSED；行为为纯代码搬移（不做功能变更），交互效果需实机回归

## 24. F3 贴图功能实现（PinWindow 完整化 + 默认保存文件夹设置）
- **需求**：F3 将最近一次截图（带批注）贴到屏幕顶层——初始位置 = 截图选区位置；拖动移动；右键菜单"清除"（仅清屏幕）/ "销毁"（清屏幕 + 删文件）；滚轮缩放；左下小阴影悬浮效果；**张贴同时自动保存到设置中的默认文件夹（用户自定义）**
- **实现**：
  - `Settings` 新增 `pinSaveDir()`/`setPinSaveDir()`（默认 `图片/MySnipaste`）；`SettingsDialog` 新增"贴图保存文件夹"行（QLineEdit + 浏览按钮）
  - `PinWindow` 完整化：`pin(pixmap, globalTopLeft)`——保存落盘（时间戳文件名 `Pin_yyyyMMdd_hhmmss_zzz.png`）+ 按选区逻辑坐标定位 + 置顶显示；窗口逻辑尺寸 = 物理像素 / 所在屏幕 dpr；拖动（frameGeometry 偏移）、滚轮缩放（0.2x~5x，基于 baseSize 非累加）、右键菜单清除/销毁（`QFile::remove`）、`QGraphicsDropShadowEffect` 左下阴影（offset(-6,6)）+ `WA_TranslucentBackground`
  - `CaptureWindow::captured` 信号扩展为携带选区全局逻辑矩形（F3 定位用）；`MainWindow` 记录 `m_lastPixmap`/`m_lastRect`，F3 热键回调 `m_pin->pin(...)`
- **坑**：`QSize / qreal` 返回 `QSize`（整数除法）无 `toSize()`，须先 `QSizeF(...)` 再运算；`WA_DeleteOnClose` 与外部持有时会 double delete（PinWindow 由 MainWindow 持有，故不用）
- **验证**：Release 构建通过；需实机 F1 截图 → F3 贴图 → 拖动/滚轮/右键清除/销毁 → 检查默认文件夹落盘
- **Bug（修复）**：Enter 不再弹结果面板 + F3 逻辑重构为"贴剪贴板" + 疑似 F3 闪退：
  - **Enter = 仅复制**：`CaptureWindow::confirm` 改为 render → `QGuiApplication::clipboard()->setPixmap` → exitCapture → emit captured；`MainWindow::onCaptured` 只记录 `m_lastPixmap`/`m_lastRect`（供 F3 定位），**不再 show ResultDialog**（该面板不再自动弹出）
  - **F3 = 贴剪贴板最新图片**：`onPinHotKey`——截图模式先 `m_capture->copyToClipboardAndClose()`（复制当前截图+关闭截图模式），再从 `clipboard()->pixmap()` 读图（剪贴板为文字则 pixmap 空 → 无响应），贴到 `pinPosition()`（上次截图位置，未截图则屏幕中心）
  - **F3 闪退排查**：写 `scripts/pin_test`（链接 PinWindow/Settings/GlobalHotKey）验证——独立 pin()、全局热键回调中 pin() 均不崩；主程序伪造截图数据/截图模式场景实测均不崩。未能复现，做**防御性修复**：热键回调改为 `QTimer::singleShot(0)` 延迟执行（WM_HOTKEY 在 nativeEventFilter 中同步 emit，直接在此上下文 show 窗口/操作 UI 存在时序风险），且 F3 不再依赖 m_lastPixmap
- **提醒**：结果面板（ResultDialog）不再自动弹出后，OCR/翻译按钮暂无入口（面板类保留未删），后续如需可把入口移到别处
- **Bug（修复）**：贴图多实例 + 位置规则 + 缩放步进——① 原来单个 PinWindow 实例重复 F3 覆盖前一张；改为每次 F3 `new PinWindow()`（`WA_DeleteOnClose`，右键清除/销毁/双击 → `close()` 自动销毁），多张贴图并存互不影响 ② 位置规则：截图模式 F3 → `copyToClipboardAndClose()` 返回**本次选区位置**（截图所在位置即张贴区域）；非截图模式 → 最近确认截图位置；两者皆无 → **鼠标当前位置**（`QCursor::pos()`）作为贴图左上角 ③ 缩放跨度：原 `angleDelta()/120` 每滚一格 m_scale ±1.0（1x→2x 翻倍），改为 `×0.1` 每格 ±0.1，0.2x~5x，以左上角为锚点（resize 保持 topLeft）
- **Bug（修复）**：贴图阴影改为**内容下边与左边外侧手绘渐变**（光从右上照射的投影感）——弃用 QGraphicsDropShadowEffect（效果不可控/不明显），`paintEvent` 用 `QLinearGradient` 在内容左侧（宽 kShadow=14）与下侧（高 kShadow）画渐变（贴近内容最深 alpha130 → 外缘透明）；内容左上角固定于窗口内 `(kShadow, 0)`，窗口 `move(globalTopLeft - QPoint(kShadow, 0))` 保证**内容左上角 = 传入位置**；窗口尺寸 = 内容 + kShadow 边距（右/上无阴影，透出桌面）
- **坑（pin_test 复现）**：`WA_DeleteOnClose` 的 widget 若在**栈上**创建，QApplication 析构会自动删除顶层窗口 → **double delete 堆损坏（0xC0000374）**；测试程序必须堆上 `new`。曾误判为阴影绘制问题，二分（注释阴影仍崩）后锁定根因
