#pragma once

#include <QObject>
#include <QString>

#include "ClipboardCodec.h"

class QTimer;

namespace poptools {

class ActivityStore;

// ============================================================
// 剪贴板监听器（T-047-CLIP）：AddClipboardFormatListener（窗口消息
// 监听，非系统钩子，不踩红线 2）→ clipboard.jsonl。
//
// 链路与线程纪律：
//   WM_CLIPBOARDUPDATE（GUI 线程，零耗时：只记 seq + 重启 300ms 防抖）
//   → 防抖到点：自身写入排除（OwnWriteGuard）→ QThreadPool worker
//   打开剪贴板提取载荷（文本只切片不整拷、图片只读 BITMAPINFOHEADER
//   40 字节头部，大图 DIB 不进 GUI 线程）
//   → 排队回 GUI 线程：去重状态机 → ActivityStore 落盘。
//
// 监听器随活动监控开关装卸（start=AddClipboardFormatListener，
// stop=RemoveClipboardFormatListener + 销毁窗口 + 冲账 repeat 行）。
// 已知限制：OpenClipboard 与其它进程竞争失败时放弃本条（下条更新
// 自然补上）；剪贴板被清空（EmptyClipboard）产生无内容更新，跳过。
// ============================================================
class ClipboardListener : public QObject
{
    Q_OBJECT
public:
    // store 仅在监听器生命周期内使用（模块保证先建 store 后建本对象）
    explicit ClipboardListener(ActivityStore *store, QObject *parent = nullptr);
    ~ClipboardListener() override;

    bool start(); // AddClipboardFormatListener；失败返回 false（告警由调用方给）
    void stop();  // 完全卸载 + flushPendingRepeat

    // 前台上下文（ForegroundTracker 转发；事件行 app/title 用）
    void setContext(const QString &app, const QString &title) { m_app = app; m_title = title; }

    // 冲账挂起的 repeat 行。T-047-FIX(P3-7) 注释如实化：仅在监控停止时由
    // stop() 调用（无日切补偿）；跨日连续复制的 repeat 行落入新日的
    // clipboard.jsonl，属已知行为
    void flushPendingRepeat();

    // WM_CLIPBOARDUPDATE 落点（零耗时：只记 seq + 重启防抖）。public 仅因
    // 消息窗口的静态 wndProc（自由函数）经 g_listener 调入；勿在别处使用
    void noteUpdate();

private slots:
    void onDebounce();

private:
    void handlePayload(const ClipboardCodec::ClipboardPayload &p, qint64 seq);

    ActivityStore *m_store = nullptr;
    QTimer *m_debounce = nullptr; // 300ms 单发：burst 合并
    qint64 m_pendingSeq = 0;      // 最近一次更新的剪贴板序列号
    bool m_extracting = false;    // worker 提取在途（busy 门卫：丢弃堆积）
    bool m_listening = false;
    bool m_stopped = false;       // T-047-FIX(P3-2)：stop() 置位，在途提取结果
                                  // 一律丢弃（写 stop()/start()，读 GUI 线程
                                  // 排队 lambda 与 handlePayload，无并发访问）
#ifdef Q_OS_WIN
    void *m_hwnd = nullptr; // message-only 原生窗口（HWND，避免头文件引 windows.h）
#endif

    QString m_app, m_title;
    ClipboardCodec::ClipboardDeduper m_deduper; // 去重/repeat 纯状态机
};

} // namespace poptools
