#include "crate/prefs/dlgprefcrate.h"

#include <QCheckBox>
#include <QCoreApplication>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QProcess>
#include <QSpinBox>
#include <QVBoxLayout>

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

DlgPrefCrate::DlgPrefCrate(QWidget* pParent, UserSettingsPointer pConfig)
        : DlgPreferencePage(pParent), m_pConfig(std::move(pConfig)) {
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
}

QString DlgPrefCrate::findAnalysisScript() const {
    const QString relativePath = QStringLiteral("tools/analysis/analyze.ps1");
    const QDir resourceDir(m_pConfig->getResourcePath());
    const QStringList candidates{
            resourceDir.absoluteFilePath(QStringLiteral("../") + relativePath),
            QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(relativePath),
            QDir(QCoreApplication::applicationDirPath())
                    .absoluteFilePath(QStringLiteral("../") + relativePath),
    };
    for (const QString& candidate : candidates) {
        const QFileInfo info(candidate);
        if (info.isFile()) {
            return info.absoluteFilePath();
        }
    }
    return QString();
}

void DlgPrefCrate::launchAnalysis() {
    const QString script = findAnalysisScript();
    if (script.isEmpty()) {
        m_pAnalysisStatus->setText(tr("analysis tools not found"));
        return;
    }

    QStringList arguments{
            QStringLiteral("-NoExit"),
            QStringLiteral("-ExecutionPolicy"),
            QStringLiteral("Bypass"),
            QStringLiteral("-File"),
            script,
    };
    const QString musicRoot = m_pMusicRoot->text().trimmed();
    if (!musicRoot.isEmpty()) {
        arguments.append(QStringLiteral("-Root"));
        arguments.append(musicRoot);
    } else {
        arguments = {
                QStringLiteral("-NoExit"),
                QStringLiteral("-Command"),
                tr("Write-Host 'Set Music library in Crate preferences, or run analyze.ps1 -Root <music folder>.'"),
        };
    }

    if (QProcess::startDetached(QStringLiteral("powershell.exe"), arguments)) {
        m_pAnalysisStatus->setText(tr(
                "analysis started in a separate window - use the MAP refresh "
                "(or restart) when it finishes."));
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
