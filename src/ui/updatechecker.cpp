#include "updatechecker.h"

#include "core/version.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSettings>

namespace {

const char* kLatestRelease = "https://api.github.com/repos/wangchaozhi/Jimaku/releases/latest";

} // namespace

UpdateChecker::UpdateChecker(QObject* parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
}

void UpdateChecker::check(bool quiet)
{
    QSettings settings;
    const QDateTime last = settings.value(QStringLiteral("update/lastCheck")).toDateTime();
    if (quiet && last.isValid() && last.secsTo(QDateTime::currentDateTime()) < 24 * 3600)
        return;
    settings.setValue(QStringLiteral("update/lastCheck"), QDateTime::currentDateTime());

    QNetworkRequest req(QUrl(QString::fromLatin1(kLatestRelease)));
    req.setRawHeader("Accept", "application/vnd.github+json");
    req.setRawHeader("User-Agent", "Jimaku");
    req.setTransferTimeout(15000);
    QNetworkReply* reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, quiet] {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 404) { // 还没有发布过版本
            if (!quiet)
                emit upToDate();
            return;
        }
        if (reply->error() != QNetworkReply::NoError) {
            if (!quiet)
                emit failed(reply->errorString());
            return;
        }
        const QJsonObject o = QJsonDocument::fromJson(reply->readAll()).object();
        const QString tag = o.value(QStringLiteral("tag_name")).toString();
        if (!tag.isEmpty() && compareVersions(QStringLiteral(APP_VERSION), tag) < 0)
            emit updateAvailable(tag, o.value(QStringLiteral("html_url")).toString());
        else if (!quiet)
            emit upToDate();
    });
}
