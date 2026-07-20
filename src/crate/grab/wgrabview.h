#pragma once

#include <QElapsedTimer>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QWidget>

#include "crate/grab/grabmodels.h"
#include "library/libraryview.h"
#include "preferences/usersettings.h"

class QLabel;
class QLineEdit;
class QModelIndex;
class QPushButton;
class QTableView;
class QTimer;

// The GRAB view (wave-6, slice G2). Fills the library table area when the GRAB
// sidebar item is activated: a query line + ARTIST/TRACK toggle + SEARCH, a
// results table, and a compact queue strip. All copy is plain-language and
// honest about the ~45-60s populate time and the thin-ness of exact-track
// Soulseek searches. Never blocks the app: an unreachable service shows a quiet
// state and disables the controls.

namespace crate {

class GrabClient;

// WLibrary::registerView() only mounts widgets that implement LibraryView (it
// dynamic_casts and silently drops anything else), so the interface is
// load-bearing: without it the sidebar click switches to a view that was never
// added and nothing happens.
class WGrabView final : public QWidget, public LibraryView {
    Q_OBJECT
  public:
    WGrabView(QWidget* pParent, UserSettingsPointer pConfig);
    ~WGrabView() override;

    // Called when the sidebar item is (re)activated: re-checks reachability and
    // refreshes the queue.
    void onActivated();

    // LibraryView
    void onShow() override {
        onActivated();
    }
    bool hasFocus() const override {
        return QWidget::hasFocus();
    }

  private slots:
    void onSearchClicked();
    void onModeChanged();
    void onSearchStarted(const QString& id);
    void onSearchStillRunning();
    void onResultsReady(const QVector<crate::GrabResult>& results);
    void onSearchFailed(const QString& error);
    void onResultActivated(const QModelIndex& index);
    void onDownloadClicked();
    void onDownloadResult(const QStringList& accepted, const QStringList& rejected);
    void onDownloadFailed(const QString& error);
    void onQueueReady(const QVector<crate::GrabQueueItem>& items);
    void onQueueFailed(const QString& error);
    void onPingResult(bool reachable, bool slskdOnline);
    void onPingAuthRejected();
    void tickElapsed();

  private:
    QString currentMode() const;
    void beginSearch();
    void setControlsEnabled(bool enabled);
    void setStatus(const QString& text);
    void setHint(const QString& text);
    void downloadSelectedRows();

    UserSettingsPointer m_pConfig;
    GrabClient* m_pClient;
    GrabResultsModel* m_pResultsModel;
    GrabQueueModel* m_pQueueModel;

    QLineEdit* m_pQuery;
    QPushButton* m_pArtistMode;
    QPushButton* m_pTrackMode;
    QPushButton* m_pSearch;
    QPushButton* m_pDownload;
    QLabel* m_pStatus;
    QLabel* m_pHint;
    QLabel* m_pQueueHeader;
    QTableView* m_pResults;
    QTableView* m_pQueue;

    QTimer* m_pElapsedTimer;
    QTimer* m_pQueuePoll;
    QElapsedTimer m_elapsed;
    bool m_reachable;
    bool m_searching;
};

} // namespace crate
