#include "UpdateDownloader.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QStandardPaths>
#include <QUrl>

namespace poptools {

namespace {
constexpr int kTimeoutMs = 600000;    // 安装包 ~60MB，10 分钟上限
constexpr qint64 kMinInstallerBytes = 5 * 1024 * 1024; // <5MB 视为残包拒装
} // namespace

bool UpdateDownloader::canAutoInstall()
{
    // Inno 安装形态 exe 旁必有卸载器；开发树（build/Release）没有——
    // 自更新会直接覆盖源码树构建产物，必须门控
    return QFileInfo::exists(QCoreApplication::applicationDirPath()
                             + QStringLiteral("/unins000.exe"));
}

UpdateDownloader::UpdateDownloader(QObject *parent)
    : QObject(parent)
{
}

void UpdateDownloader::downloadAndInstall(const QString &installerUrl)
{
    if (!m_nam)
        m_nam = new QNetworkAccessManager(this);
    if (m_reply) // 下载中重入保护
        return;
    QNetworkRequest req{QUrl(installerUrl)};
    req.setTransferTimeout(kTimeoutMs);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    m_tempPath = QStandardPaths::writableLocation(QStandardPaths::TempLocation)
                 + QStringLiteral("/PopTools-Setup-update.exe");
    m_downloadFile.setFileName(m_tempPath);
    if (!m_downloadFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        emit failed(QStringLiteral("临时文件创建失败：%1").arg(m_tempPath));
        return;
    }
    m_reply = m_nam->get(req);
    connect(m_reply, &QNetworkReply::downloadProgress, this,
            &UpdateDownloader::downloadProgress);
    // 增量写盘（安装包大，不整段驻留内存）
    connect(m_reply, &QNetworkReply::readyRead, this, [this] {
        m_downloadFile.write(m_reply->readAll());
    });
    connect(m_reply, &QNetworkReply::finished, this, [this] {
        QNetworkReply *r = m_reply;
        m_reply = nullptr;
        r->deleteLater();
        m_downloadFile.close();
        const QString path = m_tempPath;
        m_tempPath.clear();
        const auto cleanup = [path] { QFile::remove(path); };
        if (r->error() != QNetworkReply::NoError) {
            cleanup();
            emit failed(QStringLiteral("下载失败：%1").arg(r->errorString()));
            return;
        }
        if (QFileInfo(path).size() < kMinInstallerBytes) {
            cleanup();
            emit failed(QStringLiteral("下载的安装包不完整，已取消自动安装"));
            return;
        }
        // Inno 静默装：等当前进程退出释放锁 → 覆盖安装 → RESTARTAPPLICATIONS
        // 自动重启 PopTools（主程序退出由 installerLaunched 接线侧负责）
        const bool ok = QProcess::startDetached(
            QStandardPaths::findExecutable(QStringLiteral("cmd.exe")),
            { QStringLiteral("/c"), QStringLiteral("start"), QStringLiteral(""),
              QDir::toNativeSeparators(path),
              QStringLiteral("/SILENT"), QStringLiteral("/CLOSEAPPLICATIONS"),
              QStringLiteral("/RESTARTAPPLICATIONS") });
        if (!ok) {
            cleanup();
            emit failed(QStringLiteral("安装器启动失败"));
            return;
        }
        emit installerLaunched(path);
    });
}

} // namespace poptools
