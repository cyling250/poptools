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
//
// T-077 自动更新：updateAvailable 附带安装包直链（assets 里
// PopTools-Setup-*.exe 的 browser_download_url，供 UpdateDownloader
// 下载静默安装）；Release 无安装包资产时直链为空（仅提示跳转页）。
// ============================================================
class UpdateChecker : public QObject
{
    Q_OBJECT
public:
    explicit UpdateChecker(QObject *parent = nullptr);

    void checkForUpdates(bool interactive);

signals:
    void updateAvailable(const QString &version, const QString &releaseUrl,
                         const QString &installerUrl);
    void upToDate();
    void checkFailed(const QString &reason);

private:
    QNetworkAccessManager *m_nam = nullptr;
};

} // namespace poptools
