#include "crate/prefs/dlgprefcrate.h"

#include <QCheckBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QProcess>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

#include "library/trackcollection.h"
#include "crate/cull/cullclient.h"
#include "crate/grab/grabclient.h"
#include "library/trackcollectionmanager.h"
#include "moc_dlgprefcrate.cpp"

namespace {
const ConfigKey kSidecarDir("[Crate]", "sidecar_dir");
const ConfigKey kMusicRoot("[Crate]", "music_root");
const ConfigKey kGrabUrl("[Crate]", "grab_service_url");
const ConfigKey kGrabToken("[Crate]", "grab_service_token");
const ConfigKey kAutoAnalyze("[Crate]", "auto_analyze");
const ConfigKey kAnalyzerThreads("[Crate]", "analyzer_threads");
const ConfigKey kGalaxyTrail("[Crate]", "galaxy_trail");
const ConfigKey kGalaxyHalos("[Crate]", "galaxy_halos");

QLabel* note(const QString& text) {
    auto* pLabel = new QLabel(text);
    pLabel->setWordWrap(true);
    return pLabel;
}
} // namespace

DlgPrefCrate::DlgPrefCrate(QWidget* pParent,
        UserSettingsPointer pConfig,
        TrackCollectionManager* pTrackCollectionManager)
        : DlgPreferencePage(pParent),
          m_pConfig(std::move(pConfig)),
          m_pTrackCollectionManager(pTrackCollectionManager) {
    auto* pLayout = new QVBoxLayout(this);

    auto* pLibrary = new QGroupBox(tr("Library data"), this);
    auto* pLibraryLayout = new QFormLayout(pLibrary);
    m_pSidecarDir = addDirectoryRow(tr("Analysis sidecars"), QStringLiteral("sidecarDir"));
    m_pMusicRoot = addDirectoryRow(tr("Music library"), QStringLiteral("musicRoot"));
    pLibraryLayout->addRow(tr("Analysis sidecars"), m_pSidecarDir->parentWidget());
    pLibraryLayout->addRow(tr("Music library"), m_pMusicRoot->parentWidget());
    pLibraryLayout->addRow(note(tr("Where the analysis sidecars and music live.")));
    pLayout->addWidget(pLibrary);

    auto* pGrab = new QGroupBox(tr("Grab"), this);
    auto* pGrabLayout = new QFormLayout(pGrab);
    m_pGrabUrl = new QLineEdit(pGrab);
    m_pGrabUrl->setObjectName(QStringLiteral("grabServiceUrl"));
    m_pGrabToken = new QLineEdit(pGrab);
    m_pGrabToken->setObjectName(QStringLiteral("grabServiceToken"));
    m_pGrabToken->setEchoMode(QLineEdit::Password);
    pGrabLayout->addRow(tr("Service address"), m_pGrabUrl);
    pGrabLayout->addRow(tr("Token"), m_pGrabToken);
    pGrabLayout->addRow(note(tr("Leave empty to hide GRAB from the sidebar (restart applies).")));
    pLayout->addWidget(pGrab);

    auto* pTrash = new QGroupBox(tr("Trash"), this);
    auto* pTrashLayout = new QHBoxLayout(pTrash);
    m_pTrashStatus = note(tr("Checking trash..."));
    m_pTrashStatus->setObjectName(QStringLiteral("trashStatus"));
    m_pRestoreTrash = new QPushButton(tr("Restore..."), pTrash);
    m_pRestoreTrash->setObjectName(QStringLiteral("restoreTrash"));
    m_pEmptyTrash = new QPushButton(tr("Empty Trash"), pTrash);
    m_pEmptyTrash->setObjectName(QStringLiteral("emptyTrash"));
    m_pRestoreTrash->setEnabled(false);
    m_pEmptyTrash->setEnabled(false);
    pTrashLayout->addWidget(m_pTrashStatus, 1);
    pTrashLayout->addWidget(m_pRestoreTrash);
    pTrashLayout->addWidget(m_pEmptyTrash);
    connect(m_pRestoreTrash, &QPushButton::clicked, this, &DlgPrefCrate::restoreTrash);
    connect(m_pEmptyTrash, &QPushButton::clicked, this, &DlgPrefCrate::emptyTrash);
    pLayout->addWidget(pTrash);

    auto* pAnalysis = new QGroupBox(tr("Analysis"), this);
    auto* pAnalysisLayout = new QFormLayout(pAnalysis);
    m_pAutoAnalyze = new QCheckBox(
            tr("Analyze new and missing tracks in the background"), pAnalysis);
    m_pAutoAnalyze->setObjectName(QStringLiteral("autoAnalyze"));
    m_pAnalyzerThreads = new QSpinBox(pAnalysis);
    m_pAnalyzerThreads->setObjectName(QStringLiteral("analyzerThreads"));
    m_pAnalyzerThreads->setRange(0, 32);
    m_pAnalyzerThreads->setSpecialValueText(tr("Automatic (half the CPU cores)"));
    pAnalysisLayout->addRow(m_pAutoAnalyze);
    pAnalysisLayout->addRow(tr("Background workers"), m_pAnalyzerThreads);
    auto* pAnalyzeLibrary = new QPushButton(tr("Analyze library"), pAnalysis);
    pAnalyzeLibrary->setObjectName(QStringLiteral("analyzeLibrary"));
    m_pAnalysisStatus = note(QString());
    m_pAnalysisStatus->setObjectName(QStringLiteral("analysisStatus"));
    pAnalysisLayout->addRow(pAnalyzeLibrary);
    pAnalysisLayout->addRow(m_pAnalysisStatus);
    connect(pAnalyzeLibrary, &QPushButton::clicked, this, &DlgPrefCrate::launchAnalysis);
    pLayout->addWidget(pAnalysis);

    auto* pMap = new QGroupBox(tr("Map"), this);
    auto* pMapLayout = new QVBoxLayout(pMap);
    m_pGalaxyTrail = new QCheckBox(tr("Show the played-tracks trail"), pMap);
    m_pGalaxyTrail->setObjectName(QStringLiteral("galaxyTrail"));
    m_pGalaxyHalos = new QCheckBox(tr("Show the plexus"), pMap);
    m_pGalaxyHalos->setObjectName(QStringLiteral("galaxyHalos"));
    pMapLayout->addWidget(m_pGalaxyTrail);
    pMapLayout->addWidget(m_pGalaxyHalos);
    pLayout->addWidget(pMap);
    pLayout->addStretch();

    setScrollSafeGuardForAllInputWidgets(this);
    slotUpdate();
    QTimer::singleShot(0, this, &DlgPrefCrate::refreshTrash);
}

void DlgPrefCrate::refreshTrash() {
    if (m_pCullClient) {
        m_pCullClient->deleteLater();
    }
    const QString url = m_pGrabUrl->text().trimmed();
    if (url.isEmpty()) {
        m_pTrashStatus->setText(tr("Trash service is not configured."));
        m_pRestoreTrash->setEnabled(false);
        m_pEmptyTrash->setEnabled(false);
        return;
    }
    m_pTrashStatus->setText(tr("Checking trash..."));
    m_pCullClient = new crate::CullClient(url, m_pGrabToken->text(), this);
    connect(m_pCullClient, &crate::CullClient::trashReady, this,
            [this](const QVector<crate::TrashEntry>& entries, qint64 totalSize) {
                m_trashEntries = entries;
                m_pTrashStatus->setText(tr("%1 files, %2")
                        .arg(entries.size()).arg(QLocale().formattedDataSize(totalSize)));
                m_pRestoreTrash->setEnabled(!entries.isEmpty());
                m_pEmptyTrash->setEnabled(!entries.isEmpty());
            });
    connect(m_pCullClient, &crate::CullClient::trashFailed, this,
            [this](const QString& error) {
                m_pTrashStatus->setText(error);
                m_pRestoreTrash->setEnabled(false);
                m_pEmptyTrash->setEnabled(false);
            });
    connect(m_pCullClient, &crate::CullClient::restoreSucceeded,
            this, [this](const QString&) { refreshTrash(); });
    connect(m_pCullClient, &crate::CullClient::restoreFailed,
            this, [this](const QString& error) { QMessageBox::warning(this, tr("Restore"), error); });
    connect(m_pCullClient, &crate::CullClient::emptySucceeded,
            this, [this](int) { refreshTrash(); });
    connect(m_pCullClient, &crate::CullClient::emptyFailed,
            this, [this](const QString& error) { QMessageBox::warning(this, tr("Empty Trash"), error); });
    m_pCullClient->requestTrash();
}

void DlgPrefCrate::restoreTrash() {
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Restore from Trash"));
    auto* layout = new QVBoxLayout(&dialog);
    auto* list = new QListWidget(&dialog);
    for (const auto& entry : std::as_const(m_trashEntries)) {
        auto* item = new QListWidgetItem(entry.relpath, list);
        item->setData(Qt::UserRole, entry.relpath);
    }
    layout->addWidget(list);
    auto* restore = new QPushButton(tr("Restore Selected"), &dialog);
    layout->addWidget(restore);
    connect(restore, &QPushButton::clicked, &dialog, &QDialog::accept);
    if (dialog.exec() == QDialog::Accepted && list->currentItem() && m_pCullClient) {
        m_pCullClient->restore(list->currentItem()->data(Qt::UserRole).toString());
    }
}

void DlgPrefCrate::emptyTrash() {
    if (!m_pCullClient || QMessageBox::warning(this, tr("Empty Trash"),
                tr("Permanently delete every file in Trash? This cannot be undone."),
                QMessageBox::Cancel | QMessageBox::Yes, QMessageBox::Cancel) != QMessageBox::Yes) {
        return;
    }
    m_pCullClient->emptyTrash();
}

QString DlgPrefCrate::findAnalysisScript() const {
    const QString relativePath = QStringLiteral("tools/analysis/analyze.ps1");
    const QDir resourceDir(m_pConfig->getResourcePath());
    const QStringList candidates{
            resourceDir.absoluteFilePath(relativePath),
            QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(relativePath),
            resourceDir.absoluteFilePath(QStringLiteral("../tools/analysis/analyze.ps1")),
    };
    for (const QString& candidate : candidates) {
        const QFileInfo info(candidate);
        if (info.isFile()) {
            return info.absoluteFilePath();
        }
    }
    return QString();
}

bool DlgPrefCrate::prepareAnalysisDirectories(
        QString* pMusicRoot, QString* pSidecarDir) {
    *pMusicRoot = m_pMusicRoot->text().trimmed();
    if (pMusicRoot->isEmpty() && m_pTrackCollectionManager) {
        const QStringList libraryDirectories =
                m_pTrackCollectionManager->internalCollection()->getRootDirStrings();
        if (!libraryDirectories.isEmpty()) {
            *pMusicRoot = libraryDirectories.first();
        }
    }
    if (pMusicRoot->isEmpty()) {
        m_pAnalysisStatus->setText(tr("add a music folder first"));
        return false;
    }

    *pSidecarDir = m_pConfig->getValue(kSidecarDir, QString()).trimmed();
    if (pSidecarDir->isEmpty()) {
        *pSidecarDir = QDir(*pMusicRoot).filePath(QStringLiteral(".crate"));
        m_pConfig->setValue(kSidecarDir, *pSidecarDir);
        m_pSidecarDir->setText(*pSidecarDir);
    }
    return true;
}

void DlgPrefCrate::launchAnalysis() {
    const QString script = findAnalysisScript();
    if (script.isEmpty()) {
        m_pAnalysisStatus->setText(tr("analysis tools not found"));
        return;
    }

    QString musicRoot;
    QString sidecarDir;
    if (!prepareAnalysisDirectories(&musicRoot, &sidecarDir)) {
        return;
    }

    const QDir analysisDir = QFileInfo(script).absoluteDir();
    const QString setupScript = analysisDir.absoluteFilePath(QStringLiteral("setup.ps1"));
    const bool needsSetup =
            !analysisDir.exists(QStringLiteral(".venv-analysis/Scripts/python.exe"));
    const auto quotePowerShell = [](QString value) {
        return QStringLiteral("'") +
                value.replace(QLatin1Char('\''), QStringLiteral("''")) +
                QStringLiteral("'");
    };
    QString command;
    if (needsSetup) {
        command = QStringLiteral("& %1; if (-not $?) { exit 1 }; ")
                          .arg(quotePowerShell(setupScript));
    }
    command += QStringLiteral("& %1 -Root %2 -Out %3")
                       .arg(quotePowerShell(script),
                               quotePowerShell(musicRoot),
                               quotePowerShell(sidecarDir));
    const QStringList arguments{
            QStringLiteral("-NoExit"),
            QStringLiteral("-ExecutionPolicy"),
            QStringLiteral("Bypass"),
            QStringLiteral("-Command"),
            command,
    };

    if (QProcess::startDetached(QStringLiteral("powershell.exe"), arguments)) {
        m_pAnalysisStatus->setText(tr(
                "analysis running in a separate window - the map refreshes itself "
                "when it finishes."));
    } else {
        m_pAnalysisStatus->setText(tr("analysis could not be started"));
    }
}

QLineEdit* DlgPrefCrate::addDirectoryRow(
        const QString& label, const QString& objectName) {
    auto* pContainer = new QWidget(this);
    auto* pLayout = new QHBoxLayout(pContainer);
    pLayout->setContentsMargins(0, 0, 0, 0);
    auto* pEdit = new QLineEdit(pContainer);
    pEdit->setObjectName(objectName);
    auto* pBrowse = new QPushButton(tr("Browse..."), pContainer);
    pBrowse->setAccessibleName(tr("Choose %1 directory").arg(label));
    pLayout->addWidget(pEdit);
    pLayout->addWidget(pBrowse);
    connect(pBrowse, &QPushButton::clicked, this, [this, pEdit, label] {
        const QString path = QFileDialog::getExistingDirectory(this, label, pEdit->text());
        if (!path.isEmpty()) {
            pEdit->setText(path);
        }
    });
    return pEdit;
}

void DlgPrefCrate::slotUpdate() {
    m_pSidecarDir->setText(m_pConfig->getValue(kSidecarDir, QString()));
    m_pMusicRoot->setText(m_pConfig->getValue(kMusicRoot, QString()));
    m_pGrabUrl->setText(m_pConfig->getValue(kGrabUrl, QString()));
    m_pGrabToken->setText(m_pConfig->getValue(kGrabToken, QString()));
    m_pAutoAnalyze->setChecked(m_pConfig->getValue(kAutoAnalyze, 1) != 0);
    m_pAnalyzerThreads->setValue(
            m_pConfig->exists(kAnalyzerThreads) ? m_pConfig->getValue(kAnalyzerThreads, 1) : 0);
    m_pGalaxyTrail->setChecked(m_pConfig->getValue(kGalaxyTrail, 1) != 0);
    m_pGalaxyHalos->setChecked(m_pConfig->getValue(kGalaxyHalos, 1) != 0);
}

void DlgPrefCrate::slotApply() {
    m_pConfig->setValue(kSidecarDir, m_pSidecarDir->text().trimmed());
    m_pConfig->setValue(kMusicRoot, m_pMusicRoot->text().trimmed());
    m_pConfig->setValue(kGrabUrl, m_pGrabUrl->text().trimmed());
    m_pConfig->setValue(kGrabToken, m_pGrabToken->text());
    m_pConfig->setValue(kAutoAnalyze, m_pAutoAnalyze->isChecked() ? 1 : 0);
    if (m_pAnalyzerThreads->value() == 0) {
        m_pConfig->remove(kAnalyzerThreads);
    } else {
        m_pConfig->setValue(kAnalyzerThreads, m_pAnalyzerThreads->value());
    }
    m_pConfig->setValue(kGalaxyTrail, m_pGalaxyTrail->isChecked() ? 1 : 0);
    m_pConfig->setValue(kGalaxyHalos, m_pGalaxyHalos->isChecked() ? 1 : 0);
}

void DlgPrefCrate::slotResetToDefaults() {
    m_pSidecarDir->clear();
    m_pMusicRoot->clear();
    m_pGrabUrl->clear();
    m_pGrabToken->clear();
    m_pAutoAnalyze->setChecked(true);
    m_pAnalyzerThreads->setValue(0);
    m_pGalaxyTrail->setChecked(true);
    m_pGalaxyHalos->setChecked(true);
}
