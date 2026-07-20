#include "crate/cull/cullclient.h"

#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

#include "moc_cullclient.cpp"

namespace crate {

CullClient::CullClient(QString baseUrl, QString token, QObject* pParent)
        : QObject(pParent),
          m_baseUrl(std::move(baseUrl).trimmed()),
          m_token(std::move(token).trimmed()) {
    while (m_baseUrl.endsWith(QLatin1Char('/'))) {
        m_baseUrl.chop(1);
    }
}

void CullClient::setTimeoutMs(int timeoutMs) {
    if (timeoutMs > 0) {
        m_timeoutMs = timeoutMs;
    }
}

QString CullClient::relpathForLocation(
        const QString& location, const QString& configuredMusicRoot) {
    const QString cleanLocation =
            QDir::cleanPath(QDir::fromNativeSeparators(location));
    const QString cleanRoot =
            QDir::cleanPath(QDir::fromNativeSeparators(configuredMusicRoot));
    // Do not compare drive/UNC prefixes. Match root path components where
    // possible, then fall back to removing only the volume/share spelling.
    const QStringList rootParts = cleanRoot.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    const QStringList locationParts = cleanLocation.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (int rootStart = 0; rootStart < rootParts.size(); ++rootStart) {
        const QString suffix = rootParts.mid(rootStart).join(QLatin1Char('/'));
        for (int i = 0; i < locationParts.size(); ++i) {
            if (locationParts.mid(i, rootParts.size() - rootStart)
                            .join(QLatin1Char('/')).toCaseFolded() ==
                    suffix.toCaseFolded()) {
                const QString rel = locationParts.mid(
                        i + rootParts.size() - rootStart).join(QLatin1Char('/'));
                if (!rel.isEmpty() && !rel.startsWith(QStringLiteral("../")) &&
                        !rel.contains(QStringLiteral("/../"))) {
                    return rel;
                }
            }
        }
    }
    int firstRelative = 0;
    if (!locationParts.isEmpty() && locationParts.first().endsWith(QLatin1Char(':'))) {
        firstRelative = 1;
    } else if (cleanLocation.startsWith(QStringLiteral("//")) &&
            locationParts.size() >= 2) {
        firstRelative = 2; // UNC server/share
    }
    const QString rel = locationParts.mid(firstRelative).join(QLatin1Char('/'));
    return rel.isEmpty() ? QString() : rel;
}

QNetworkRequest CullClient::request(const QString& path) const {
    QNetworkRequest req(QUrl(m_baseUrl + path));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    if (!m_token.isEmpty()) {
        req.setRawHeader(QByteArrayLiteral("X-Grab-Token"), m_token.toUtf8());
    }
    req.setTransferTimeout(m_timeoutMs);
    return req;
}

QNetworkReply* CullClient::post(const QString& path, const QJsonObject& body) {
    return m_network.post(request(path),
            QJsonDocument(body).toJson(QJsonDocument::Compact));
}

QString CullClient::errorMessage(QNetworkReply* pReply, const QString& operation) {
    const int status = pReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status == 401 || status == 403) {
        return QObject::tr("The trash service rejected the token. Check Crate preferences.");
    }
    if (status == 404) {
        return QObject::tr("The file was not found on the music box. Nothing was removed locally.");
    }
    if (pReply->error() != QNetworkReply::NoError) {
        return QObject::tr("The trash service could not be reached. Nothing was removed locally.");
    }
    return QObject::tr("The trash service could not %1.").arg(operation);
}

void CullClient::cull(const QString& relpath) {
    auto* reply = post(QStringLiteral("/api/cull"), {{QStringLiteral("relpath"), relpath}});
    connect(reply, &QNetworkReply::finished, this, [this, reply, relpath] {
        reply->deleteLater();
        if (reply->error() == QNetworkReply::NoError) emit cullSucceeded(relpath);
        else emit cullFailed(errorMessage(reply, tr("move the file to trash")));
    });
}

void CullClient::requestTrash() {
    auto* reply = m_network.get(request(QStringLiteral("/api/trash")));
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit trashFailed(errorMessage(reply, tr("read trash")));
            return;
        }
        const auto object = QJsonDocument::fromJson(reply->readAll()).object();
        QVector<TrashEntry> entries;
        for (const auto& value : object.value(QStringLiteral("entries")).toArray()) {
            const auto item = value.toObject();
            entries.append({item.value(QStringLiteral("relpath")).toString(),
                    item.value(QStringLiteral("ts")).toDouble(),
                    static_cast<qint64>(item.value(QStringLiteral("size")).toDouble())});
        }
        emit trashReady(entries,
                static_cast<qint64>(object.value(QStringLiteral("total_size")).toDouble()));
    });
}

void CullClient::restore(const QString& relpath) {
    auto* reply = post(QStringLiteral("/api/restore"), {{QStringLiteral("relpath"), relpath}});
    connect(reply, &QNetworkReply::finished, this, [this, reply, relpath] {
        reply->deleteLater();
        if (reply->error() == QNetworkReply::NoError) emit restoreSucceeded(relpath);
        else emit restoreFailed(errorMessage(reply, tr("restore the file")));
    });
}

void CullClient::emptyTrash() {
    auto* reply = post(QStringLiteral("/api/trash/empty"),
            {{QStringLiteral("confirm"), QStringLiteral("empty")}});
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit emptyFailed(errorMessage(reply, tr("empty trash")));
            return;
        }
        const auto object = QJsonDocument::fromJson(reply->readAll()).object();
        emit emptySucceeded(object.value(QStringLiteral("removed")).toInt());
    });
}

} // namespace crate
