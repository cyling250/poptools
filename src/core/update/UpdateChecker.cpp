#include "UpdateChecker.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QUrl>

namespace poptools {

namespace {
constexpr char kLatestUrl[] =
    "https://api.github.com/repos/cyling250/poptools/releases/latest";
constexpr int kTimeoutMs = 10000;

// 三段数值比较：容错 "v" 前缀与 "0.5" / "0.5.0" 段数差；解析失败（预发布标签等）
// 一律视为不比当前新（宁可不提示也不误报）
bool isNewerThan(const QString &tag, const QString &current)
{
    const auto parse = [](const QString &raw) {
        QString t = raw.trimmed();
        if (t.startsWith(QLatin1Char('v')) || t.startsWith(QLatin1Char('V')))
            t.remove(0, 1);
        QList<int> out;
        const QStringList parts = t.split(QLatin1Char('.'));
        out.reserve(parts.size());
        for (const QString &p : parts) {
            bool ok = false;
            const int v = p.toInt(&ok);
            if (!ok)
                return QList<int>();
            out.append(v);
        }
        return out;
    };
    const QList<int> a = parse(tag);
    const QList<int> b = parse(current);
    if (a.isEmpty() || b.isEmpty())
        return false;
    const int n = qMax(a.size(), b.size());
    for (int i = 0; i < n; ++i) {
        const int x = i < a.size() ? a.at(i) : 0;
        const int y = i < b.size() ? b.at(i) : 0;
        if (x != y)
            return x > y;
    }
    return false;
}
} // namespace

UpdateChecker::UpdateChecker(QObject *parent)
    : QObject(parent)
{
}

void UpdateChecker::checkForUpdates(bool interactive)
{
    if (!m_nam)
        m_nam = new QNetworkAccessManager(this);
    QNetworkRequest req{QUrl(QLatin1String(kLatestUrl))}; // 大括号：避开 vexing parse
    req.setTransferTimeout(kTimeoutMs);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply *reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, interactive] {
        reply->deleteLater();
        const QString reason =
            QStringLiteral("无法获取 GitHub Release 信息（请检查网络连接）");
        if (reply->error() != QNetworkReply::NoError) {
            if (interactive)
                emit checkFailed(reason);
            return;
        }
        const QJsonObject root = QJsonDocument::fromJson(reply->readAll()).object();
        const QString tag = root.value(QLatin1String("tag_name")).toString();
        const QString url = root.value(QLatin1String("html_url")).toString();
        if (tag.isEmpty() || url.isEmpty()) {
            if (interactive)
                emit checkFailed(reason);
            return;
        }
        if (!isNewerThan(tag, QLatin1String(POPTOOLS_VERSION))) {
            if (interactive)
                emit upToDate();
            return;
        }
        // T-077 自动更新：assets 里找安装包直链（PopTools-Setup-*.exe）；
        // 无资产（源码-only 发布）直链留空——上层仅提示跳转下载页
        QString installerUrl;
        const QJsonArray assets = root.value(QLatin1String("assets")).toArray();
        const QRegularExpression setupRe(
            QStringLiteral("PopTools-Setup-.*\\.exe$"),
            QRegularExpression::CaseInsensitiveOption);
        for (const QJsonValue &a : assets) {
            const QJsonObject asset = a.toObject();
            const QString name = asset.value(QLatin1String("name")).toString();
            const QString dl = asset.value(QLatin1String("browser_download_url")).toString();
            if (!dl.isEmpty() && setupRe.match(name).hasMatch()) {
                installerUrl = dl;
                break;
            }
        }
        emit updateAvailable(tag, url, installerUrl);
    });
}

} // namespace poptools
