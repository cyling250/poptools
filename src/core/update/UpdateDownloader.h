#pragma once

#include <QFile>
#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;

namespace poptools {

// ============================================================
// UpdateDownloader（T-077）：PopTools 安装包下载 + 静默安装。
//
// 流程：browser_download_url 下载 PopTools-Setup-*.exe 到 %TEMP%（增量
// 写盘，readyRead 流式）→ 校验大小 → 启动 Inno /SILENT
// /CLOSEAPPLICATIONS /RESTARTAPPLICATIONS（安装器等当前进程退出后覆盖
// 安装并自动重启 PopTools）→ installerLaunched 信号（接线侧退出主程序）。
//
// 安装形态门控：applicationDirPath 无 unins000.exe = 开发树形态
//（build/Release），自更新会破坏源码树产物 → canAutoInstall()=false，
// 上层只提示跳转下载页。下载异步（QNetworkAccessManager，进度 MB/总量）；
// 失败清理残留临时文件。
// ============================================================
class UpdateDownloader : public QObject
{
    Q_OBJECT
public:
    explicit UpdateDownloader(QObject *parent = nullptr);

    static bool canAutoInstall(); // exe 同目录有 Inno 卸载器 = 安装器形态

    void downloadAndInstall(const QString &installerUrl);

signals:
    void downloadProgress(qint64 received, qint64 total);
    void installerLaunched(const QString &path); // 安装器已启动（接线侧应退出主程序）
    void failed(const QString &reason);

private:
    QNetworkAccessManager *m_nam = nullptr;
    QNetworkReply *m_reply = nullptr;
    QFile m_downloadFile;
    QString m_tempPath;
};

} // namespace poptools
