#include "crate/grab/grabclient.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QUrl>

#include "moc_grabclient.cpp"

namespace {

constexpr int kDefaultPollMs = 1500;
constexpr int kDefaultTimeoutMs = 8000;

QString normalizeBaseUrl(const QString& raw) {
    QString url = raw.trimmed();
    while (url.endsWith(QLatin1Char('/'))) {
        url.chop(1);
    }
    return url;
}

} // namespace

namespace crate {

GrabClient::GrabClient(const QString& baseUrl, const QString& token, QObject* pParent)
        : QObject(pParent),
          m_baseUrl(normalizeBaseUrl(baseUrl)),
          m_token(token.trimmed()),
          m_pollMs(kDefaultPollMs),
          m_timeoutMs(kDefaultTimeoutMs),
          m_searchActive(false) {
    m_searchPoll.setSingleShot(false);
    connect(&m_searchPoll, &QTimer::timeout, this, &GrabClient::pollSearch);
}

GrabClient::~GrabClient() = default;

QString GrabClient::configuredBaseUrl(const UserSettingsPointer& pConfig) {
    return pConfig->getValue(ConfigKey("[Crate]", "grab_service_url"), QString())
            .trimmed();
}

QString GrabClient::configuredToken(const UserSettingsPointer& pConfig) {
    return pConfig->getValue(ConfigKey("[Crate]", "grab_service_token"), QString())
            .trimmed();
}

void GrabClient::setPollIntervalMs(int ms) {
    if (ms > 0) {
        m_pollMs = ms;
    }
}

void GrabClient::setTimeoutMs(int ms) {
    if (ms > 0) {
        m_timeoutMs = ms;
    }
}

QNetworkRequest GrabClient::makeRequest(const QString& path) const {
    QNetworkRequest request(QUrl(m_baseUrl + path));
    request.setHeader(QNetworkRequest::ContentTypeHeader,
            QStringLiteral("application/json"));
    if (!m_token.isEmpty()) {
        request.setRawHeader(QByteArrayLiteral("X-Grab-Token"), m_token.toUtf8());
    }
    // Never let a dead service hang the UI.
    request.setTransferTimeout(m_timeoutMs);
    return request;
}

void GrabClient::startSearch(const QString& query, const QString& mode) {
    m_searchPoll.stop();
    m_searchId.clear();
    m_searchActive = true;

    QJsonObject body;
    body.insert(QStringLiteral("query"), query);
    body.insert(QStringLiteral("mode"), mode);
    QNetworkReply* pReply = m_network.post(makeRequest(QStringLiteral("/api/search")),
            QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(pReply, &QNetworkReply::finished, this, [this, pReply]() {
        pReply->deleteLater();
        if (pReply->error() != QNetworkReply::NoError) {
            m_searchActive = false;
            emit searchFailed(pReply->errorString());
            return;
        }
        const QJsonObject obj =
                QJsonDocument::fromJson(pReply->readAll()).object();
        const QString id = obj.value(QStringLiteral("id")).toString();
        if (id.isEmpty()) {
            m_searchActive = false;
            emit searchFailed(QStringLiteral(
                    "the grab service did not start a search"));
            return;
        }
        m_searchId = id;
        emit searchStarted(id);
        // Poll once right away, then on the timer, until done/failed.
        m_searchPoll.start(m_pollMs);
        pollSearch();
    });
}

void GrabClient::pollSearch() {
    if (m_searchId.isEmpty()) {
        m_searchPoll.stop();
        return;
    }
    QNetworkReply* pReply = m_network.get(
            makeRequest(QStringLiteral("/api/search/") + m_searchId));
    connect(pReply, &QNetworkReply::finished, this, [this, pReply]() {
        pReply->deleteLater();
        if (pReply->error() != QNetworkReply::NoError) {
            m_searchPoll.stop();
            m_searchActive = false;
            emit searchFailed(pReply->errorString());
            return;
        }
        const QJsonObject obj =
                QJsonDocument::fromJson(pReply->readAll()).object();
        const QString state = obj.value(QStringLiteral("state")).toString();
        if (state == QLatin1String("done")) {
            m_searchPoll.stop();
            m_searchActive = false;
            emit resultsReady(
                    grabParseResults(obj.value(QStringLiteral("results")).toArray()));
        } else if (state == QLatin1String("failed")) {
            m_searchPoll.stop();
            m_searchActive = false;
            const QString error = obj.value(QStringLiteral("error")).toString();
            emit searchFailed(error.isEmpty()
                            ? QStringLiteral("the search failed")
                            : error);
        } else {
            // Still "searching" (or an unrecognized in-progress state).
            emit searchStillRunning();
        }
    });
}

void GrabClient::requestDownload(const QStringList& keys) {
    QJsonObject body;
    body.insert(QStringLiteral("search_id"), m_searchId);
    body.insert(QStringLiteral("keys"), QJsonArray::fromStringList(keys));
    QNetworkReply* pReply = m_network.post(makeRequest(QStringLiteral("/api/download")),
            QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(pReply, &QNetworkReply::finished, this, [this, pReply]() {
        pReply->deleteLater();
        if (pReply->error() != QNetworkReply::NoError) {
            emit downloadFailed(pReply->errorString());
            return;
        }
        const QJsonObject obj =
                QJsonDocument::fromJson(pReply->readAll()).object();
        QStringList accepted;
        for (const QJsonValue& value : obj.value(QStringLiteral("accepted")).toArray()) {
            accepted.append(value.toString());
        }
        QStringList rejected;
        for (const QJsonValue& value : obj.value(QStringLiteral("rejected")).toArray()) {
            rejected.append(value.toString());
        }
        emit downloadResult(accepted, rejected);
    });
}

void GrabClient::refreshQueue() {
    QNetworkReply* pReply = m_network.get(makeRequest(QStringLiteral("/api/queue")));
    connect(pReply, &QNetworkReply::finished, this, [this, pReply]() {
        pReply->deleteLater();
        if (pReply->error() != QNetworkReply::NoError) {
            emit queueFailed(pReply->errorString());
            return;
        }
        emit queueReady(grabParseQueue(
                QJsonDocument::fromJson(pReply->readAll()).array()));
    });
}

void GrabClient::ping() {
    QNetworkReply* pReply = m_network.get(makeRequest(QStringLiteral("/api/ping")));
    connect(pReply, &QNetworkReply::finished, this, [this, pReply]() {
        pReply->deleteLater();
        if (pReply->error() != QNetworkReply::NoError) {
            emit pingResult(false, false);
            return;
        }
        const QJsonObject obj =
                QJsonDocument::fromJson(pReply->readAll()).object();
        emit pingResult(obj.value(QStringLiteral("ok")).toBool(),
                obj.value(QStringLiteral("slskd")).toBool());
    });
}

} // namespace crate
