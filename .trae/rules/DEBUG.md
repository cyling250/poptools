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
