#pragma once

#include <QObject>
#include <QString>

class QNetworkAccessManager;

namespace poptools {

// ============================================================
// GitHub Release 更新自查：GET /repos/cyling250/poptools/releases/latest，
// 取 tag_name 与编译期注入的 POPTOOLS_VERSION 做三段数值比较。
// interactive=true（关于对话框手动检查）：无更新/失败也提示；
// interactive=false（启动自动检查）：仅发现新版时回调，其余静默（不打扰）。
// 网络全程 QNetworkAccessManager 异步 + 10s 超时（GUI 线程红线：无阻塞调用）。
// ============================================================
class UpdateChecker : public QObject
{
    Q_OBJECT
public:
    explicit UpdateChecker(QObject *parent = nullptr);

    void checkForUpdates(bool interactive);

signals:
    void updateAvailable(const QString &version, const QString &releaseUrl);
    void upToDate();
    void checkFailed(const QString &reason);

private:
    QNetworkAccessManager *m_nam = nullptr;
};

} // namespace poptools
