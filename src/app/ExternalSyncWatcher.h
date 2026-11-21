#pragma once

#include <QObject>
#include <QTimer>

class QFileSystemWatcher;

namespace poptools {

// ============================================================
// ExternalSyncWatcher：跨进程生效协议（T-074 M2.5）。
//
// poptools_mcp_server（dsh 拉起的独立进程）代表 agent 改写 PopTools 的
// 数据文件（PopTools.ini / calendar.json / boxes.json）后，在数据目录
// touch 对应的 dirty 标记文件；本 watcher 监听数据目录，按文件名分发：
//   settings.dirty → Settings::syncExternal() + notifySettingsChanged 广播
//                    （各模块经既有 onSettingsChanged 路径重应用）
//   calendar.dirty → calendarApplySettings 能力槽（宿主重载重绘）
//   box.dirty      → notifySettingsChanged（收纳盒重应用；目录结构变更
//                    v1 经重启兜底，M3 加实时重载）
//   mcp.heartbeat  → heartbeat() 信号（GoodyModule 绑悬浮球呼吸光效；
//                    每次 MCP tools/call 触碰一次，10s 无心跳回静默）
// ============================================================
class ExternalSyncWatcher : public QObject
{
    Q_OBJECT
public:
    explicit ExternalSyncWatcher(const QString &dataRoot, QObject *parent = nullptr);

signals:
    void settingsDirty();
    void calendarDirty();
    void boxDirty();
    void mcpHeartbeat();     // 每次 tools/call 触碰心跳文件
    void mcpHeartbeatExpired(); // 10s 无心跳（呼吸回静默）

private:
    void onDataChanged(const QString &path);
    QTimer m_heartbeatRevert; // 心跳静默回退（10s 无 beat → 呼吸停）
};

} // namespace poptools
