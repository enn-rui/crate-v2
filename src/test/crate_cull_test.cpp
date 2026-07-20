// Tests for the CULL flow shared by the galaxy right-click and the track-table
// "Cull to trash" menu. Root-agnostic relpath resolution plus a tiny in-process
// HTTP stub (QTcpServer) that lets us assert the client posts the right relpath
// and maps each failure to a distinct, plain-language message.

#include <gtest/gtest.h>

#include <QByteArray>
#include <QElapsedTimer>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>

#include "crate/cull/cullclient.h"

namespace crate {

TEST(CrateCullClient, ResolvesMappedDriveAgainstUncRoot) {
    EXPECT_EQ(QStringLiteral("music/dj/Artist/Track.flac"),
            CullClient::relpathForLocation(
                    QStringLiteral("Z:/music/dj/Artist/Track.flac"),
                    QStringLiteral("//music-box/media")));
}

TEST(CrateCullClient, ResolvesUncAgainstMappedDriveRoot) {
    EXPECT_EQ(QStringLiteral("music/dj/Artist/Track.flac"),
            CullClient::relpathForLocation(
                    QStringLiteral("//music-box/media/music/dj/Artist/Track.flac"),
                    QStringLiteral("Z:/")));
}

TEST(CrateCullClient, RejectsLocationWithoutRelativeTail) {
    EXPECT_TRUE(CullClient::relpathForLocation(
            QStringLiteral("Z:/"), QStringLiteral("Z:/")).isEmpty());
}

namespace {

template<typename Pred>
bool waitFor(Pred pred, int timeoutMs = 4000) {
    QElapsedTimer timer;
    timer.start();
    while (!pred()) {
        if (timer.elapsed() > timeoutMs) {
            return pred();
        }
        QTest::qWait(5);
    }
    return true;
}

// Minimal HTTP stub: returns a configurable status for any request and records
// the last method/path/body/token so tests can assert the cull payload.
class CullStubServer : public QTcpServer {
  public:
    CullStubServer() {
        connect(this, &QTcpServer::newConnection, this, &CullStubServer::onNewConnection);
    }

    bool start() {
        return listen(QHostAddress::LocalHost, 0);
    }
    QString baseUrl() const {
        return QStringLiteral("http://127.0.0.1:%1").arg(serverPort());
    }

    int responseStatus = 200;
    QString lastMethod;
    QString lastPath;
    QByteArray lastBody;
    QByteArray lastToken;

  private:
    void onNewConnection() {
        while (hasPendingConnections()) {
            QTcpSocket* pSocket = nextPendingConnection();
            auto* pBuffer = new QByteArray();
            connect(pSocket, &QObject::destroyed, this, [pBuffer] { delete pBuffer; });
            connect(pSocket, &QTcpSocket::readyRead, this, [this, pSocket, pBuffer] {
                pBuffer->append(pSocket->readAll());
                const int headerEnd = pBuffer->indexOf("\r\n\r\n");
                if (headerEnd < 0) {
                    return;
                }
                const QByteArray header = pBuffer->left(headerEnd);
                int contentLength = 0;
                for (const QByteArray& raw : header.split('\n')) {
                    const QByteArray line = raw.trimmed();
                    const QByteArray lower = line.toLower();
                    if (lower.startsWith("content-length:")) {
                        contentLength = line.mid(line.indexOf(':') + 1).trimmed().toInt();
                    } else if (lower.startsWith("x-grab-token:")) {
                        lastToken = line.mid(line.indexOf(':') + 1).trimmed();
                    }
                }
                const QByteArray body = pBuffer->mid(headerEnd + 4);
                if (body.size() < contentLength) {
                    return;
                }
                const QByteArray requestLine = header.left(header.indexOf('\r'));
                const QList<QByteArray> parts = requestLine.split(' ');
                lastMethod = QString::fromUtf8(parts.value(0));
                lastPath = QString::fromUtf8(parts.value(1));
                lastBody = body;

                const QByteArray payload = QByteArrayLiteral("{}");
                QByteArray response;
                response += (responseStatus == 200
                                ? "HTTP/1.1 200 OK\r\n"
                                : responseStatus == 401
                                        ? "HTTP/1.1 401 Unauthorized\r\n"
                                        : "HTTP/1.1 404 Not Found\r\n");
                response += "Content-Type: application/json\r\n";
                response += "Content-Length: " + QByteArray::number(payload.size()) + "\r\n";
                response += "Connection: close\r\n\r\n";
                response += payload;
                pSocket->write(response);
                pSocket->flush();
                pSocket->disconnectFromHost();
            });
            connect(pSocket, &QTcpSocket::disconnected, pSocket, &QObject::deleteLater);
        }
    }
};

} // namespace

TEST(CrateCullClient, CullHappyPathPostsRelpathAndSucceeds) {
    CullStubServer stub;
    ASSERT_TRUE(stub.start());

    CullClient client(stub.baseUrl(), QStringLiteral("secret-token"), nullptr);
    client.setTimeoutMs(3000);

    QString succeeded;
    bool failed = false;
    QObject::connect(&client, &CullClient::cullSucceeded,
            [&](const QString& relpath) { succeeded = relpath; });
    QObject::connect(&client, &CullClient::cullFailed,
            [&](const QString&) { failed = true; });

    client.cull(QStringLiteral("dj/Artist/Track.flac"));
    ASSERT_TRUE(waitFor([&] { return !succeeded.isEmpty() || failed; }));

    EXPECT_FALSE(failed);
    EXPECT_EQ(succeeded, QStringLiteral("dj/Artist/Track.flac"));
    EXPECT_EQ(stub.lastMethod, QStringLiteral("POST"));
    EXPECT_EQ(stub.lastPath, QStringLiteral("/api/cull"));
    EXPECT_EQ(stub.lastToken, QByteArrayLiteral("secret-token"));
    const QJsonObject body = QJsonDocument::fromJson(stub.lastBody).object();
    EXPECT_EQ(body.value(QStringLiteral("relpath")).toString(),
            QStringLiteral("dj/Artist/Track.flac"));
}

TEST(CrateCullClient, TokenRejectedGivesDistinctMessageAndNoSuccess) {
    CullStubServer stub;
    ASSERT_TRUE(stub.start());
    stub.responseStatus = 401;

    CullClient client(stub.baseUrl(), QStringLiteral("wrong-token"), nullptr);
    client.setTimeoutMs(3000);

    QString error;
    bool succeeded = false;
    QObject::connect(&client, &CullClient::cullSucceeded,
            [&](const QString&) { succeeded = true; });
    QObject::connect(&client, &CullClient::cullFailed,
            [&](const QString& message) { error = message; });

    client.cull(QStringLiteral("dj/Artist/Track.flac"));
    ASSERT_TRUE(waitFor([&] { return !error.isEmpty() || succeeded; }));

    EXPECT_FALSE(succeeded); // nothing would be purged locally
    EXPECT_TRUE(error.contains(QStringLiteral("rejected the token")));
    EXPECT_FALSE(error.contains(QStringLiteral("could not be reached")));
}

TEST(CrateCullClient, NotFoundGivesDistinctMessageAndNoSuccess) {
    CullStubServer stub;
    ASSERT_TRUE(stub.start());
    stub.responseStatus = 404;

    CullClient client(stub.baseUrl(), QString(), nullptr);
    client.setTimeoutMs(3000);

    QString error;
    bool succeeded = false;
    QObject::connect(&client, &CullClient::cullSucceeded,
            [&](const QString&) { succeeded = true; });
    QObject::connect(&client, &CullClient::cullFailed,
            [&](const QString& message) { error = message; });

    client.cull(QStringLiteral("dj/Artist/Missing.flac"));
    ASSERT_TRUE(waitFor([&] { return !error.isEmpty() || succeeded; }));

    EXPECT_FALSE(succeeded);
    EXPECT_TRUE(error.contains(QStringLiteral("not found on the music box")));
}

TEST(CrateCullClient, UnreachableServiceGivesDistinctMessage) {
    quint16 deadPort = 0;
    {
        QTcpServer probe;
        ASSERT_TRUE(probe.listen(QHostAddress::LocalHost, 0));
        deadPort = probe.serverPort();
    }

    CullClient client(QStringLiteral("http://127.0.0.1:%1").arg(deadPort),
            QString(), nullptr);
    client.setTimeoutMs(1500);

    QString error;
    bool succeeded = false;
    QObject::connect(&client, &CullClient::cullSucceeded,
            [&](const QString&) { succeeded = true; });
    QObject::connect(&client, &CullClient::cullFailed,
            [&](const QString& message) { error = message; });

    client.cull(QStringLiteral("dj/Artist/Track.flac"));
    ASSERT_TRUE(waitFor([&] { return !error.isEmpty() || succeeded; }, 8000));

    EXPECT_FALSE(succeeded);
    EXPECT_TRUE(error.contains(QStringLiteral("could not be reached")));
}

} // namespace crate
