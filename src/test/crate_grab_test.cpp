// Tests for the config-gated GRAB sidebar feature (wave-6, slice G2).
//
// No real network: every client/view test runs against a tiny in-process HTTP
// stub (QTcpServer serving canned contract JSON). The stub records the requests
// it receives so we can assert the client sends the right method/path/body/token.

#include <gtest/gtest.h>

#include <QAbstractItemModel>
#include <QApplication>
#include <QByteArray>
#include <QElapsedTimer>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QString>
#include <QTableView>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QVector>

#include "crate/grab/grabclient.h"
#include "crate/grab/grabfeature.h"
#include "crate/grab/grabmodels.h"
#include "crate/grab/wgrabview.h"
#include "preferences/usersettings.h"
#include "widget/wlibrary.h"

namespace {

using crate::GrabClient;
using crate::GrabQueueItem;
using crate::GrabQueueModel;
using crate::GrabQueueState;
using crate::GrabResult;
using crate::GrabResultsModel;

// Spin the event loop until `pred` is true or the timeout elapses.
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

UserSettingsPointer makeConfig(QTemporaryDir& dir) {
    return UserSettingsPointer(
            new UserSettings(dir.filePath(QStringLiteral("grab_test.cfg"))));
}

QJsonObject makeResultJson(const QString& key,
        const QString& display,
        const QString& ext,
        qint64 size,
        bool freeSlot,
        int queueLen,
        int sourceCount) {
    QJsonObject obj;
    obj.insert(QStringLiteral("key"), key);
    obj.insert(QStringLiteral("display"), display);
    obj.insert(QStringLiteral("ext"), ext);
    obj.insert(QStringLiteral("size"), static_cast<double>(size));
    obj.insert(QStringLiteral("user"), QStringLiteral("peer42"));
    obj.insert(QStringLiteral("free_slot"), freeSlot);
    obj.insert(QStringLiteral("queue_len"), queueLen);
    obj.insert(QStringLiteral("source_count"), sourceCount);
    obj.insert(QStringLiteral("path"), QString(QStringLiteral("/music/") + display));
    obj.insert(QStringLiteral("artist_dir"), QStringLiteral("/music/artist"));
    return obj;
}

QJsonObject makeQueueJson(const QString& key,
        const QString& display,
        const QString& state,
        const QString& detail,
        const QString& dest) {
    QJsonObject obj;
    obj.insert(QStringLiteral("key"), key);
    obj.insert(QStringLiteral("display"), display);
    obj.insert(QStringLiteral("state"), state);
    obj.insert(QStringLiteral("detail"), detail);
    obj.insert(QStringLiteral("dest"), dest);
    return obj;
}

// ---- The stub server --------------------------------------------------------

class GrabStubServer : public QTcpServer {
  public:
    explicit GrabStubServer(QObject* pParent = nullptr)
            : QTcpServer(pParent) {
        connect(this, &QTcpServer::newConnection, this, &GrabStubServer::onNewConnection);
    }

    bool start() {
        return listen(QHostAddress::LocalHost, 0);
    }

    QString baseUrl() const {
        return QStringLiteral("http://127.0.0.1:%1").arg(serverPort());
    }

    // Canned responses (public so tests can tune them).
    QString searchId = QStringLiteral("srch-1");
    int pollsBeforeDone = 1; // first N polls return "searching", then "done"
    QJsonArray results;
    QJsonArray queue;
    bool pingOk = true;
    bool pingSlskd = true;
    bool enforcePingToken = false;
    QByteArray expectedToken;

    // Recorded requests.
    QString lastMethod;
    QString lastPath;
    QByteArray lastToken;
    QByteArray lastSearchBody;
    QByteArray lastDownloadBody;
    int searchPostCount = 0;
    int searchPollCount = 0;
    int downloadCount = 0;
    int queueCount = 0;
    int pingCount = 0;

  private:
    // Per-connection state. Its lifetime is tied to the socket's destruction
    // (via deleteLater), never to the disconnected signal, so a synchronous
    // close inside writeResponse() can't leave the readyRead lambda touching
    // freed memory.
    struct Conn {
        QByteArray buffer;
        bool responded = false;
    };

    void onNewConnection() {
        while (hasPendingConnections()) {
            QTcpSocket* pSocket = nextPendingConnection();
            auto* pConn = new Conn();
            connect(pSocket, &QObject::destroyed, this, [pConn]() { delete pConn; });
            connect(pSocket, &QTcpSocket::readyRead, this, [this, pSocket, pConn]() {
                if (pConn->responded) {
                    pSocket->readAll();
                    return;
                }
                pConn->buffer.append(pSocket->readAll());
                const int headerEnd = pConn->buffer.indexOf("\r\n\r\n");
                if (headerEnd < 0) {
                    return; // headers not complete yet
                }
                const QByteArray header = pConn->buffer.left(headerEnd);
                int contentLength = 0;
                const QList<QByteArray> lines = header.split('\n');
                for (const QByteArray& raw : lines) {
                    const QByteArray line = raw.trimmed();
                    const QByteArray lower = line.toLower();
                    if (lower.startsWith("content-length:")) {
                        contentLength = line.mid(line.indexOf(':') + 1).trimmed().toInt();
                    } else if (lower.startsWith("x-grab-token:")) {
                        lastToken = line.mid(line.indexOf(':') + 1).trimmed();
                    }
                }
                const QByteArray body = pConn->buffer.mid(headerEnd + 4);
                if (body.size() < contentLength) {
                    return; // body not complete yet
                }
                const QByteArray requestLine = header.left(header.indexOf('\r'));
                const QList<QByteArray> parts = requestLine.split(' ');
                const QByteArray method = parts.value(0);
                const QByteArray path = parts.value(1);
                lastMethod = QString::fromUtf8(method);
                lastPath = QString::fromUtf8(path);
                pConn->responded = true;
                writeResponse(pSocket, route(method, path, body));
            });
            connect(pSocket, &QTcpSocket::disconnected, pSocket, &QObject::deleteLater);
        }
    }

    QByteArray route(const QByteArray& method, const QByteArray& path, const QByteArray& body) {
        QJsonObject obj;
        if (method == "POST" && path == "/api/search") {
            ++searchPostCount;
            lastSearchBody = body;
            obj.insert(QStringLiteral("id"), searchId);
        } else if (method == "GET" && path.startsWith("/api/search/")) {
            ++searchPollCount;
            if (searchPollCount <= pollsBeforeDone) {
                obj.insert(QStringLiteral("state"), QStringLiteral("searching"));
            } else {
                obj.insert(QStringLiteral("state"), QStringLiteral("done"));
                obj.insert(QStringLiteral("results"), results);
            }
        } else if (method == "POST" && path == "/api/download") {
            ++downloadCount;
            lastDownloadBody = body;
            const QJsonObject request = QJsonDocument::fromJson(body).object();
            obj.insert(QStringLiteral("accepted"), request.value(QStringLiteral("keys")).toArray());
            obj.insert(QStringLiteral("rejected"), QJsonArray());
        } else if (method == "GET" && path == "/api/queue") {
            ++queueCount;
            return QJsonDocument(queue).toJson(QJsonDocument::Compact);
        } else if (method == "GET" && path == "/api/ping") {
            ++pingCount;
            if (enforcePingToken && lastToken != expectedToken) {
                m_responseStatus = 401;
                obj.insert(QStringLiteral("error"), QStringLiteral("unauthorized"));
                return QJsonDocument(obj).toJson(QJsonDocument::Compact);
            }
            obj.insert(QStringLiteral("ok"), pingOk);
            obj.insert(QStringLiteral("slskd"), pingSlskd);
        } else {
            obj.insert(QStringLiteral("error"), QStringLiteral("not found"));
        }
        return QJsonDocument(obj).toJson(QJsonDocument::Compact);
    }

    void writeResponse(QTcpSocket* pSocket, const QByteArray& payload) {
        QByteArray response;
        response += (m_responseStatus == 401 ? "HTTP/1.1 401 Unauthorized\r\n"
                                            : "HTTP/1.1 200 OK\r\n");
        response += "Content-Type: application/json\r\n";
        response += "Content-Length: " + QByteArray::number(payload.size()) + "\r\n";
        response += "Connection: close\r\n\r\n";
        response += payload;
        pSocket->write(response);
        pSocket->flush();
        pSocket->disconnectFromHost();
        m_responseStatus = 200;
    }

    int m_responseStatus = 200;
};

// ---- Model / helper tests (no network) --------------------------------------

TEST(CrateGrabModel, HumanSizeFormatting) {
    EXPECT_EQ(crate::grabHumanSize(0), QStringLiteral("0 B"));
    EXPECT_EQ(crate::grabHumanSize(512), QStringLiteral("512 B"));
    EXPECT_EQ(crate::grabHumanSize(1023), QStringLiteral("1023 B"));
    EXPECT_EQ(crate::grabHumanSize(1024), QStringLiteral("1.0 KB"));
    EXPECT_EQ(crate::grabHumanSize(1536), QStringLiteral("1.5 KB"));
    EXPECT_EQ(crate::grabHumanSize(static_cast<qint64>(5) * 1024 * 1024),
            QStringLiteral("5.0 MB"));
    EXPECT_EQ(crate::grabHumanSize(static_cast<qint64>(3) * 1024 * 1024 * 1024),
            QStringLiteral("3.0 GB"));
    EXPECT_EQ(crate::grabHumanSize(-1), QStringLiteral("?"));
}

TEST(CrateGrabModel, QueueStateMapping) {
    EXPECT_EQ(crate::grabParseQueueState(QStringLiteral("queued")), GrabQueueState::Queued);
    EXPECT_EQ(crate::grabParseQueueState(QStringLiteral("DOWNLOADING")),
            GrabQueueState::Downloading);
    EXPECT_EQ(crate::grabParseQueueState(QStringLiteral(" Landed ")), GrabQueueState::Landed);
    EXPECT_EQ(crate::grabParseQueueState(QStringLiteral("failed")), GrabQueueState::Failed);
    EXPECT_EQ(crate::grabParseQueueState(QStringLiteral("weird")), GrabQueueState::Unknown);
    EXPECT_EQ(crate::grabQueueStateLabel(GrabQueueState::Queued), QStringLiteral("queued"));
    EXPECT_EQ(crate::grabQueueStateLabel(GrabQueueState::Landed), QStringLiteral("landed"));
    EXPECT_EQ(crate::grabQueueStateLabel(GrabQueueState::Unknown), QStringLiteral("unknown"));
}

TEST(CrateGrabModel, ResultsModelPopulatesFromJson) {
    QJsonArray arr;
    arr.append(makeResultJson(QStringLiteral("k1"),
            QStringLiteral("Aphex Twin - Xtal"),
            QStringLiteral("flac"),
            static_cast<qint64>(30) * 1024 * 1024,
            true,
            0,
            3));
    arr.append(makeResultJson(QStringLiteral("k2"),
            QStringLiteral("Aphex Twin - Ageispolis"),
            QStringLiteral("mp3"),
            static_cast<qint64>(8) * 1024 * 1024,
            false,
            5,
            1));

    GrabResultsModel model;
    model.setResults(crate::grabParseResults(arr));

    ASSERT_EQ(model.rowCount(), 2);
    EXPECT_EQ(model.columnCount(), static_cast<int>(GrabResultsModel::ColumnCount));
    EXPECT_EQ(model.index(0, GrabResultsModel::ColumnName).data().toString(),
            QStringLiteral("Aphex Twin - Xtal"));
    EXPECT_EQ(model.index(0, GrabResultsModel::ColumnExt).data().toString(),
            QStringLiteral("flac"));
    EXPECT_EQ(model.index(0, GrabResultsModel::ColumnSize).data().toString(),
            QStringLiteral("30.0 MB"));
    EXPECT_EQ(model.index(0, GrabResultsModel::ColumnFreeSlot).data().toString(),
            QStringLiteral("yes"));
    EXPECT_EQ(model.index(1, GrabResultsModel::ColumnFreeSlot).data().toString(),
            QStringLiteral("no"));
    EXPECT_EQ(model.index(1, GrabResultsModel::ColumnQueue).data().toInt(), 5);
    EXPECT_EQ(model.index(0, GrabResultsModel::ColumnSources).data().toInt(), 3);
    EXPECT_EQ(model.keyAt(0), QStringLiteral("k1"));
    EXPECT_EQ(model.keyAt(1), QStringLiteral("k2"));

    model.clearResults();
    EXPECT_EQ(model.rowCount(), 0);
    EXPECT_TRUE(model.keyAt(0).isEmpty());
}

TEST(CrateGrabModel, QueueModelStatesAndLandedNote) {
    QJsonArray arr;
    arr.append(makeQueueJson(QStringLiteral("k1"),
            QStringLiteral("Track A"),
            QStringLiteral("downloading"),
            QStringLiteral("42%"),
            QString()));
    arr.append(makeQueueJson(QStringLiteral("k2"),
            QStringLiteral("Track B"),
            QStringLiteral("landed"),
            QString(),
            QStringLiteral("/srv/media/incoming/Track B.flac")));
    arr.append(makeQueueJson(QStringLiteral("k3"),
            QStringLiteral("Track C"),
            QStringLiteral("failed"),
            QStringLiteral("no free slot"),
            QString()));

    GrabQueueModel model;
    model.setQueue(crate::grabParseQueue(arr));

    ASSERT_EQ(model.rowCount(), 3);
    EXPECT_EQ(model.index(0, GrabQueueModel::ColumnState).data().toString(),
            QStringLiteral("downloading"));
    EXPECT_EQ(model.index(1, GrabQueueModel::ColumnState).data().toString(),
            QStringLiteral("landed"));
    // Failed rows surface their error text plainly.
    EXPECT_TRUE(model.index(2, GrabQueueModel::ColumnState)
                        .data()
                        .toString()
                        .contains(QStringLiteral("no free slot")));
    // The "landed" tooltip carries the honest ~15-minute note.
    const QString tip = model.index(1, GrabQueueModel::ColumnName)
                                .data(Qt::ToolTipRole)
                                .toString();
    EXPECT_TRUE(tip.contains(QStringLiteral("15 minutes")));
}

// ---- Feature gating ---------------------------------------------------------

TEST(CrateGrabFeature, AbsentWhenUrlUnsetPresentWhenSet) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    UserSettingsPointer pConfig = makeConfig(dir);

    // Unset -> the single gate Library branches on is false, so the feature is
    // never constructed/registered.
    EXPECT_FALSE(crate::GrabFeature::isConfigured(pConfig));

    pConfig->setValue(ConfigKey("[Crate]", "grab_service_url"),
            QStringLiteral("http://192.168.5.203:8078"));
    EXPECT_TRUE(crate::GrabFeature::isConfigured(pConfig));

    // Whitespace-only is treated as unset.
    pConfig->setValue(ConfigKey("[Crate]", "grab_service_url"), QStringLiteral("   "));
    EXPECT_FALSE(crate::GrabFeature::isConfigured(pConfig));
}

TEST(CrateGrabFeature, ExposesTitleAndSidebarWhenConfigured) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    UserSettingsPointer pConfig = makeConfig(dir);
    pConfig->setValue(ConfigKey("[Crate]", "grab_service_url"),
            QStringLiteral("http://127.0.0.1:1"));

    crate::GrabFeature feature(nullptr, pConfig);
    EXPECT_FALSE(feature.title().toString().isEmpty());
    EXPECT_NE(feature.sidebarModel(), nullptr);
}

// ---- Client against the stub ------------------------------------------------

TEST(CrateGrabClient, SearchFlowArtistModePopulatesResults) {
    GrabStubServer stub;
    ASSERT_TRUE(stub.start());
    stub.pollsBeforeDone = 1; // exercise the searching -> done transition
    stub.results.append(makeResultJson(QStringLiteral("k1"),
            QStringLiteral("Aphex Twin - Xtal"), QStringLiteral("flac"),
            1024, true, 0, 2));
    stub.results.append(makeResultJson(QStringLiteral("k2"),
            QStringLiteral("Aphex Twin - Pulsewidth"), QStringLiteral("mp3"),
            2048, false, 1, 1));

    GrabClient client(stub.baseUrl(), QStringLiteral("secret-token"), nullptr);
    client.setPollIntervalMs(15);
    client.setTimeoutMs(3000);

    bool sawSearching = false;
    QObject::connect(&client, &GrabClient::searchStillRunning,
            [&]() { sawSearching = true; });
    QVector<GrabResult> got;
    bool done = false;
    QObject::connect(&client, &GrabClient::resultsReady,
            [&](const QVector<GrabResult>& r) {
                got = r;
                done = true;
            });

    client.startSearch(QStringLiteral("Aphex Twin"), QStringLiteral("artist"));
    ASSERT_TRUE(waitFor([&]() { return done; }));

    EXPECT_TRUE(sawSearching); // the "searching" state was observed first
    ASSERT_EQ(got.size(), 2);
    EXPECT_EQ(got.at(0).display, QStringLiteral("Aphex Twin - Xtal"));
    EXPECT_EQ(got.at(0).sourceCount, 2);

    // The POST carried the query + mode, and the token header was sent.
    const QJsonObject searchBody = QJsonDocument::fromJson(stub.lastSearchBody).object();
    EXPECT_EQ(searchBody.value(QStringLiteral("query")).toString(),
            QStringLiteral("Aphex Twin"));
    EXPECT_EQ(searchBody.value(QStringLiteral("mode")).toString(),
            QStringLiteral("artist"));
    EXPECT_EQ(stub.lastToken, QByteArrayLiteral("secret-token"));
}

TEST(CrateGrabClient, SearchFlowTrackMode) {
    GrabStubServer stub;
    ASSERT_TRUE(stub.start());
    stub.pollsBeforeDone = 0; // resolve on the first poll
    stub.results.append(makeResultJson(QStringLiteral("t1"),
            QStringLiteral("Windowlicker"), QStringLiteral("flac"),
            4096, true, 0, 1));

    GrabClient client(stub.baseUrl(), QString(), nullptr);
    client.setPollIntervalMs(15);

    QVector<GrabResult> got;
    bool done = false;
    QObject::connect(&client, &GrabClient::resultsReady,
            [&](const QVector<GrabResult>& r) {
                got = r;
                done = true;
            });

    client.startSearch(QStringLiteral("Windowlicker"), QStringLiteral("track"));
    ASSERT_TRUE(waitFor([&]() { return done; }));
    ASSERT_EQ(got.size(), 1);
    EXPECT_EQ(got.at(0).display, QStringLiteral("Windowlicker"));

    const QJsonObject searchBody = QJsonDocument::fromJson(stub.lastSearchBody).object();
    EXPECT_EQ(searchBody.value(QStringLiteral("mode")).toString(), QStringLiteral("track"));
}

TEST(CrateGrabClient, DownloadPostsSelectedKeysAndQueueReflectsStates) {
    GrabStubServer stub;
    ASSERT_TRUE(stub.start());
    stub.pollsBeforeDone = 0;
    stub.results.append(makeResultJson(QStringLiteral("k1"),
            QStringLiteral("A"), QStringLiteral("flac"), 1024, true, 0, 1));
    stub.queue.append(makeQueueJson(QStringLiteral("k1"),
            QStringLiteral("A"), QStringLiteral("queued"), QString(), QString()));
    stub.queue.append(makeQueueJson(QStringLiteral("k2"),
            QStringLiteral("B"), QStringLiteral("downloading"),
            QStringLiteral("10%"), QString()));

    GrabClient client(stub.baseUrl(), QString(), nullptr);
    client.setPollIntervalMs(15);

    bool searchDone = false;
    QObject::connect(&client, &GrabClient::resultsReady,
            [&](const QVector<GrabResult>&) { searchDone = true; });
    client.startSearch(QStringLiteral("A"), QStringLiteral("artist"));
    ASSERT_TRUE(waitFor([&]() { return searchDone; }));

    // Download a specific pair of keys.
    QStringList accepted;
    bool downloadDone = false;
    QObject::connect(&client, &GrabClient::downloadResult,
            [&](const QStringList& a, const QStringList&) {
                accepted = a;
                downloadDone = true;
            });
    client.requestDownload({QStringLiteral("k1"), QStringLiteral("k7")});
    ASSERT_TRUE(waitFor([&]() { return downloadDone; }));

    const QJsonObject downloadBody =
            QJsonDocument::fromJson(stub.lastDownloadBody).object();
    EXPECT_EQ(downloadBody.value(QStringLiteral("search_id")).toString(),
            QStringLiteral("srch-1"));
    const QJsonArray keys = downloadBody.value(QStringLiteral("keys")).toArray();
    ASSERT_EQ(keys.size(), 2);
    EXPECT_EQ(keys.at(0).toString(), QStringLiteral("k1"));
    EXPECT_EQ(keys.at(1).toString(), QStringLiteral("k7"));
    EXPECT_EQ(accepted.size(), 2);

    // The queue view reflects the canned queue states.
    QVector<GrabQueueItem> queue;
    bool queueDone = false;
    QObject::connect(&client, &GrabClient::queueReady,
            [&](const QVector<GrabQueueItem>& q) {
                queue = q;
                queueDone = true;
            });
    client.refreshQueue();
    ASSERT_TRUE(waitFor([&]() { return queueDone; }));
    ASSERT_EQ(queue.size(), 2);
    EXPECT_EQ(queue.at(0).state, GrabQueueState::Queued);
    EXPECT_EQ(queue.at(1).state, GrabQueueState::Downloading);
}

TEST(CrateGrabClient, PingReportsReachabilityAndSlskd) {
    GrabStubServer stub;
    ASSERT_TRUE(stub.start());
    stub.pingOk = true;
    stub.pingSlskd = false;

    GrabClient client(stub.baseUrl(), QString(), nullptr);
    bool pinged = false;
    bool reachable = false;
    bool slskd = true;
    QObject::connect(&client, &GrabClient::pingResult,
            [&](bool r, bool s) {
                pinged = true;
                reachable = r;
                slskd = s;
            });
    client.ping();
    ASSERT_TRUE(waitFor([&]() { return pinged; }));
    EXPECT_TRUE(reachable);
    EXPECT_FALSE(slskd);
}

TEST(CrateGrabClient, WrongTokenReportsAuthRejectedNotUnreachable) {
    GrabStubServer stub;
    ASSERT_TRUE(stub.start());
    stub.enforcePingToken = true;
    stub.expectedToken = QByteArrayLiteral("right-token");

    GrabClient client(stub.baseUrl(), QStringLiteral("wrong-token"), nullptr);
    bool rejected = false;
    bool pinged = false;
    QObject::connect(&client, &GrabClient::pingAuthRejected,
            [&]() { rejected = true; });
    QObject::connect(&client, &GrabClient::pingResult,
            [&](bool, bool) { pinged = true; });
    client.ping();
    ASSERT_TRUE(waitFor([&]() { return rejected; }));
    EXPECT_FALSE(pinged);
}

TEST(CrateGrabClient, UnreachableServiceReportsNotReachableWithoutHang) {
    // Bind a server, capture its port, then close it -> a port with no listener.
    quint16 deadPort = 0;
    {
        QTcpServer probe;
        ASSERT_TRUE(probe.listen(QHostAddress::LocalHost, 0));
        deadPort = probe.serverPort();
    }

    GrabClient client(QStringLiteral("http://127.0.0.1:%1").arg(deadPort),
            QString(), nullptr);
    client.setTimeoutMs(1500);
    client.setPollIntervalMs(15);

    bool pinged = false;
    bool reachable = true;
    QObject::connect(&client, &GrabClient::pingResult,
            [&](bool r, bool) {
                pinged = true;
                reachable = r;
            });
    client.ping();
    ASSERT_TRUE(waitFor([&]() { return pinged; }));
    EXPECT_FALSE(reachable);

    // A search against the dead service fails cleanly (no crash, no hang).
    bool failed = false;
    QObject::connect(&client, &GrabClient::searchFailed,
            [&](const QString&) { failed = true; });
    client.startSearch(QStringLiteral("x"), QStringLiteral("artist"));
    EXPECT_TRUE(waitFor([&]() { return failed; }));
    EXPECT_FALSE(client.searchInFlight());
}

// ---- View against the stub --------------------------------------------------

TEST(CrateGrabView, SearchFillsTableAndGrabQueuesSelection) {
    GrabStubServer stub;
    ASSERT_TRUE(stub.start());
    stub.pollsBeforeDone = 0; // resolve immediately so the view test is fast
    stub.results.append(makeResultJson(QStringLiteral("k1"),
            QStringLiteral("Aphex Twin - Xtal"), QStringLiteral("flac"),
            static_cast<qint64>(30) * 1024 * 1024, true, 0, 3));
    stub.results.append(makeResultJson(QStringLiteral("k2"),
            QStringLiteral("Aphex Twin - Ageispolis"), QStringLiteral("mp3"),
            static_cast<qint64>(8) * 1024 * 1024, false, 2, 1));
    stub.queue.append(makeQueueJson(QStringLiteral("k1"),
            QStringLiteral("Aphex Twin - Xtal"), QStringLiteral("queued"),
            QString(), QString()));

    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    UserSettingsPointer pConfig = makeConfig(dir);
    pConfig->setValue(ConfigKey("[Crate]", "grab_service_url"), stub.baseUrl());

    crate::WGrabView view(nullptr, pConfig);
    view.show();
    QApplication::processEvents();

    auto* pQuery = view.findChild<QLineEdit*>(QStringLiteral("GrabQuery"));
    auto* pSearch = view.findChild<QPushButton*>(QStringLiteral("GrabSearch"));
    auto* pGrab = view.findChild<QPushButton*>(QStringLiteral("GrabDownload"));
    auto* pResults = view.findChild<QTableView*>(QStringLiteral("GrabResults"));
    auto* pQueue = view.findChild<QTableView*>(QStringLiteral("GrabQueue"));
    ASSERT_NE(pQuery, nullptr);
    ASSERT_NE(pSearch, nullptr);
    ASSERT_NE(pGrab, nullptr);
    ASSERT_NE(pResults, nullptr);
    ASSERT_NE(pQueue, nullptr);

    // The initial ping refreshes the queue.
    ASSERT_TRUE(waitFor([&]() { return pQueue->model()->rowCount() > 0; }));

    // Type a query and search; the results table fills from the canned JSON.
    pQuery->setText(QStringLiteral("Aphex Twin"));
    QTest::mouseClick(pSearch, Qt::LeftButton);
    ASSERT_TRUE(waitFor([&]() { return pResults->model()->rowCount() == 2; }));
    EXPECT_EQ(pResults->model()
                      ->index(0, GrabResultsModel::ColumnSize)
                      .data()
                      .toString(),
            QStringLiteral("30.0 MB"));

    // Select the first row and click GRAB -> a download POST fires with its key.
    pResults->selectRow(0);
    const int downloadsBefore = stub.downloadCount;
    QTest::mouseClick(pGrab, Qt::LeftButton);
    ASSERT_TRUE(waitFor([&]() { return stub.downloadCount > downloadsBefore; }));
    const QJsonObject body = QJsonDocument::fromJson(stub.lastDownloadBody).object();
    const QJsonArray keys = body.value(QStringLiteral("keys")).toArray();
    ASSERT_EQ(keys.size(), 1);
    EXPECT_EQ(keys.at(0).toString(), QStringLiteral("k1"));
}

TEST(CrateGrabView, WrongTokenShowsRejectedTokenMessage) {
    GrabStubServer stub;
    ASSERT_TRUE(stub.start());
    stub.enforcePingToken = true;
    stub.expectedToken = QByteArrayLiteral("right-token");

    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    UserSettingsPointer pConfig = makeConfig(dir);
    pConfig->setValue(ConfigKey("[Crate]", "grab_service_url"), stub.baseUrl());
    pConfig->setValue(ConfigKey("[Crate]", "grab_service_token"),
            QStringLiteral("wrong-token"));
    crate::WGrabView view(nullptr, pConfig);
    auto* pStatus = view.findChild<QLabel*>(QStringLiteral("GrabStatus"));
    ASSERT_NE(pStatus, nullptr);
    ASSERT_TRUE(waitFor([&]() {
        return pStatus->text().contains(QStringLiteral("rejected the token"));
    }));
    EXPECT_FALSE(pStatus->text().contains(QStringLiteral("not reachable")));
    auto* pSearch = view.findChild<QPushButton*>(QStringLiteral("GrabSearch"));
    ASSERT_NE(pSearch, nullptr);
    EXPECT_FALSE(pSearch->isEnabled());
}

TEST(CrateGrabView, UnreachableServiceShowsQuietStateNoCrash) {
    quint16 deadPort = 0;
    {
        QTcpServer probe;
        ASSERT_TRUE(probe.listen(QHostAddress::LocalHost, 0));
        deadPort = probe.serverPort();
    }

    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    UserSettingsPointer pConfig = makeConfig(dir);
    pConfig->setValue(ConfigKey("[Crate]", "grab_service_url"),
            QStringLiteral("http://127.0.0.1:%1").arg(deadPort));

    crate::WGrabView view(nullptr, pConfig);
    view.show();

    auto* pQuery = view.findChild<QLineEdit*>(QStringLiteral("GrabQuery"));
    ASSERT_NE(pQuery, nullptr);
    // After the failed ping, the query control is disabled (quiet not-reachable
    // state); the app is never blocked and nothing crashes. The wait budget
    // exceeds the client's default transfer timeout, since discovering an
    // unreachable host takes as long as that timeout on this platform.
    ASSERT_TRUE(waitFor([&]() { return !pQuery->isEnabled(); }, 12000));
    EXPECT_FALSE(pQuery->isEnabled());
}

// ---- Feature mounting -------------------------------------------------------

// Regression: WLibrary::registerView() silently rejects widgets that don't
// implement LibraryView, so a WGrabView that is "just a QWidget" never mounts
// and the sidebar GRAB click does nothing. This test goes through the real
// mounting path (bindLibraryWidget on a real WLibrary) and asserts the view
// actually becomes the current library page.
TEST(CrateGrabFeature, ViewMountsInWLibraryAndActivates) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    UserSettingsPointer pConfig = makeConfig(dir);
    pConfig->setValue(ConfigKey("[Crate]", "grab_service_url"),
            QStringLiteral("http://127.0.0.1:1")); // never reached; ping is async

    WLibrary wlib(nullptr);
    crate::GrabFeature feature(nullptr, pConfig);
    feature.bindLibraryWidget(&wlib, nullptr);

    wlib.switchToView(QStringLiteral("CrateGrab"));
    auto* pMounted = qobject_cast<crate::WGrabView*>(wlib.currentWidget());
    ASSERT_NE(pMounted, nullptr)
            << "GRAB view was not registered/mounted in WLibrary";
    EXPECT_NE(wlib.getActiveView(), nullptr);
    // The mounted page is a fully built view (search box present).
    EXPECT_NE(pMounted->findChild<QLineEdit*>(QStringLiteral("GrabQuery")),
            nullptr);
}

} // namespace
