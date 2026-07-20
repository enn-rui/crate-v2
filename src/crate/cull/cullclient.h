#pragma once

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QObject>
#include <QString>
#include <QVector>

class QNetworkReply;
class QJsonObject;

namespace crate {

struct TrashEntry {
    QString relpath;
    double timestamp = 0.0;
    qint64 size = 0;
};

class CullClient final : public QObject {
    Q_OBJECT
  public:
    CullClient(QString baseUrl, QString token, QObject* pParent = nullptr);
    void setTimeoutMs(int timeoutMs);

    static QString relpathForLocation(
            const QString& location, const QString& configuredMusicRoot);
    static QString errorMessage(QNetworkReply* pReply, const QString& operation);

    void cull(const QString& relpath);
    void requestTrash();
    void restore(const QString& relpath);
    void emptyTrash();

  signals:
    void cullSucceeded(const QString& relpath);
    void cullFailed(const QString& message);
    void trashReady(const QVector<crate::TrashEntry>& entries, qint64 totalSize);
    void trashFailed(const QString& message);
    void restoreSucceeded(const QString& relpath);
    void restoreFailed(const QString& message);
    void emptySucceeded(int removed);
    void emptyFailed(const QString& message);

  private:
    QNetworkRequest request(const QString& path) const;
    QNetworkReply* post(const QString& path, const QJsonObject& body);

    QNetworkAccessManager m_network;
    QString m_baseUrl;
    QString m_token;
    int m_timeoutMs = 8000;
};

} // namespace crate
