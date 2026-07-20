#pragma once

#include <gtest/gtest_prod.h>

#include "preferences/dialog/dlgpreferencepage.h"
#include "preferences/usersettings.h"

class QCheckBox;
class QLineEdit;
class QLabel;
class QSpinBox;
class TrackCollectionManager;

class DlgPrefCrate final : public DlgPreferencePage {
    Q_OBJECT

  public:
    DlgPrefCrate(QWidget* pParent,
            UserSettingsPointer pConfig,
            TrackCollectionManager* pTrackCollectionManager = nullptr);

  public slots:
    void slotUpdate() override;
    void slotApply() override;
    void slotResetToDefaults() override;

  private:
    QLineEdit* addDirectoryRow(const QString& label, const QString& objectName);
    QString findAnalysisScript() const;
    bool prepareAnalysisDirectories(QString* pMusicRoot, QString* pSidecarDir);
    void launchAnalysis();

    FRIEND_TEST(CratePrefsTest, AnalysisDefaultsSidecarWithoutClobbering);
    FRIEND_TEST(CratePrefsTest, AnalysisPreservesConfiguredSidecar);

    UserSettingsPointer m_pConfig;
    TrackCollectionManager* m_pTrackCollectionManager;
    QLineEdit* m_pSidecarDir;
    QLineEdit* m_pMusicRoot;
    QLineEdit* m_pGrabUrl;
    QLineEdit* m_pGrabToken;
    QCheckBox* m_pAutoAnalyze;
    QSpinBox* m_pAnalyzerThreads;
    QLabel* m_pAnalysisStatus;
    QCheckBox* m_pGalaxyTrail;
    QCheckBox* m_pGalaxyHalos;
};
