#pragma once

#include <QHash>
#include <QObject>
#include <QRect>
#include <QString>
#include <functional>

class QAction;
class QMenu;

namespace poptools {

class GlobalHotKey;
class PaddleOcrService;

// ============================================================
// 应用级共享服务与贡献点（组合根装配，注入各功能模块）。
//
// 模块只依赖本结构与 core 服务；模块之间互不引用。
// 跨模块交互的两种形式：
//   * 能力槽/共享态（captureActive / captureIntercept / lastCaptureRect）：
//     提供方模块在 init 中写入，消费方模块读取；
//   * 数据流：经组合根用 Qt 信号撮合（模块各自暴露信号，组合根连接）。
// ============================================================
class AppServices : public QObject
{
    Q_OBJECT
public:
    explicit AppServices(QObject *parent = nullptr);

    // ---- 共享服务（组合根在 initAll 前注入并持有所有权） ----
    GlobalHotKey *hotkey = nullptr;   // 全局热键注册器
    PaddleOcrService *ocr = nullptr;  // 共享 OCR 引擎（子进程昂贵，全局唯一）
    QMenu *trayMenu = nullptr;        // 托盘菜单（模块经 addTrayAction 贡献菜单项）

    // ---- 跨模块能力槽与共享态（提供方：CaptureModule） ----
    // 截图窗口当前是否可见
    std::function<bool()> captureActive;
    // 截图进行中：复制当前截图（含标注）到剪贴板并关闭截图模式，
    // 返回本次选区（全局逻辑矩形）；空闲状态返回空矩形
    std::function<QRect()> captureIntercept;
    // 最近一次确认截图的选区（全局逻辑坐标；CaptureModule 写，PinModule 读）
    QRect lastCaptureRect;

    // ---- 贡献点 ----
    // 向托盘菜单追加动作（模块在 init 中调用；组合根的系统项在其后追加，
    // 故菜单顺序 = 模块注册顺序）
    QAction *addTrayAction(const QString &text, std::function<void()> triggered);

    // 注册全局热键并绑定回调（热键 id 由本类内部分配并自动路由；
    // 回调在主线程经 activated 信号触发，GUI 操作请自行延迟到事件循环）
    void registerHotKey(quint32 vk, quint32 modifiers, std::function<void()> triggered);

private slots:
    void onHotKeyActivated(int id);

private:
    QHash<int, std::function<void()>> m_hotkeyCallbacks;
    int m_nextHotKeyId = 1;
    bool m_hotkeyWired = false;
};

} // namespace poptools
