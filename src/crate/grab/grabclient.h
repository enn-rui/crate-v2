#pragma once

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVector>

#include "crate/grab/grabmodels.h"
#include "preferences/usersettings.h"

class QNetworkReply;

// Async JSON client for the box grab-service contract (slice G1). Every request
// goes through QNetworkAccessManager and returns via signals; the search is
// polled on a timer because the backend takes ~45-60s to populate. Nothing here
// blocks. Timeouts are short so an unreachable service surfaces quickly instead
// of hanging the UI.
//
// Contract:
//   POST /api/search {"query","mode"} -> {"id"}
//   GET  /api/search/<id> -> {"state","error","results":[...]}
//   POST /api/download {"search_id","keys":[...]} -> {"accepted","rejected"}
//   GET  /api/queue -> [{"key","display","state","detail","dest"}]
//   GET  /api/ping -> {"ok":bool,"slskd":bool}
//
// Auth: base URL from [Crate],grab_service_url; token from
// [Crate],grab_service_token sent as the X-Grab-Token header.

namespace crate {

class GrabClient final : public QObject {
    Q_OBJECT
  public:
    GrabClient(const QString& baseUrl, const QString& token, QObject* pParent = nullptr);
    ~GrabClient() override;

    static QString configuredBaseUrl(const UserSettingsPointer& pConfig);
    static QString configuredToken(const UserSettingsPointer& pConfig);

    // Test/tuning hooks. Production defaults are set in the constructor.
    void setPollIntervalMs(int ms);
    void setTimeoutMs(int ms);

    void startSearch(const QString& query, const QString& mode);
    void requestDownload(const QStringList& keys);
    void refreshQueue();
    void ping();

    QString currentSearchId() const {
        return m_searchId;
    }
    bool searchInFlight() const {
        return m_searchActive;
    }

  signals:
    void searchStarted(const QString& id);
    // Emitted on each poll while the backend still reports "searching".
    void searchStillRunning();
    void resultsReady(const QVector<crate::GrabResult>& results);
    void searchFailed(const QString& error);
    void downloadResult(const QStringList& accepted, const QStringList& rejected);
    void downloadFailed(const QString& error);
    void queueReady(const QVector<crate::GrabQueueItem>& items);
    void queueFailed(const QString& error);
    void pingResult(bool reachable, bool slskdOnline);

  private:
    QNetworkRequest makeRequest(const QString& path) const;
    void pollSearch();

    QNetworkAccessManager m_network;
    QString m_baseUrl;
    QString m_token;
    QString m_searchId;
    QTimer m_searchPoll;
    int m_pollMs;
    int m_timeoutMs;
    bool m_searchActive;
};

} // namespace crate
