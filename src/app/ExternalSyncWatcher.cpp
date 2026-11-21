#include "ExternalSyncWatcher.h"

#include <QDir>
#include <QFile>
#include <QFileSystemWatcher>
#include <QFileInfo>

namespace poptools {

ExternalSyncWatcher::ExternalSyncWatcher(const QString &dataRoot, QObject *parent)
    : QObject(parent)
{
    auto *watcher = new QFileSystemWatcher(this);
    const QDir root(dataRoot);
    if (root.exists())
        watcher->addPath(dataRoot); // 目录级监听：dirty 文件可能尚不存在

    m_heartbeatRevert.setSingleShot(true);
    m_heartbeatRevert.setInterval(10 * 1000); // 10s 无心跳 → 呼吸回静默
    connect(&m_heartbeatRevert, &QTimer::timeout, this,
            &ExternalSyncWatcher::mcpHeartbeatExpired);

    connect(watcher, &QFileSystemWatcher::directoryChanged, this, [this, dataRoot](const QString &) {
        // 目录事件不携带文件名：逐个探测标记文件（事件风暴期 QFile::exists
        // 为元数据探测，开销可忽略；dirty 触发频率 = 人一次对话一条）
        onDataChanged(dataRoot + QStringLiteral("/settings.dirty"));
        onDataChanged(dataRoot + QStringLiteral("/calendar.dirty"));
        onDataChanged(dataRoot + QStringLiteral("/box.dirty"));
        onDataChanged(dataRoot + QStringLiteral("/mcp.heartbeat"));
    });
}

void ExternalSyncWatcher::onDataChanged(const QString &path)
{
    QFileInfo info(path);
    if (!info.exists())
        return;
    const QString name = info.fileName();
    if (name == QLatin1String("mcp.heartbeat")) {
        emit mcpHeartbeat();
        m_heartbeatRevert.start(); // 重臂静默回退
        return;
    }
    // dirty 标记读完即删（防重复触发；删除失败仅多一次重入，幂等）
    QFile::remove(path);
    if (name == QLatin1String("settings.dirty"))
        emit settingsDirty();
    else if (name == QLatin1String("calendar.dirty"))
        emit calendarDirty();
    else if (name == QLatin1String("box.dirty"))
        emit boxDirty();
}

} // namespace poptools
