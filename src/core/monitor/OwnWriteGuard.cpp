#include "OwnWriteGuard.h"

#include <QMutex>
#include <QMutexLocker>
#include <QVector>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace poptools {

namespace OwnWriteGuard {

namespace {
// 最近 8 次自身写入的 seq（同刻多个写入点竞争时序号严格递增，环形足够；
// 序列号会话内单调递增且 64 位不可能回绕，无需考虑陈旧匹配）
constexpr int kRingSize = 8;
QMutex g_mux;
QVector<qint64> g_selfSeqs;
} // namespace

void noteSelfWrite()
{
#ifdef Q_OS_WIN
    const qint64 seq = static_cast<qint64>(GetClipboardSequenceNumber());
    const QMutexLocker lock(&g_mux);
    g_selfSeqs.append(seq);
    while (g_selfSeqs.size() > kRingSize)
        g_selfSeqs.removeFirst();
#endif
}

bool isSelfWrite(qint64 seq)
{
    if (seq <= 0)
        return false;
    const QMutexLocker lock(&g_mux);
    return g_selfSeqs.contains(seq);
}

void reset()
{
    const QMutexLocker lock(&g_mux);
    g_selfSeqs.clear();
}

} // namespace OwnWriteGuard

} // namespace poptools
