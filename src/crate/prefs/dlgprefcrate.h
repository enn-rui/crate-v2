#pragma once

#include "preferences/dialog/dlgpreferencepage.h"
#include "preferences/usersettings.h"

class QCheckBox;
class QLineEdit;
class QLabel;
class QSpinBox;

class DlgPrefCrate final : public DlgPreferencePage {
    Q_OBJECT

  public:
    DlgPrefCrate(QWidget* pParent, UserSettingsPointer pConfig);

  public slots:
    void slotUpdate() override;
    void slotApply() override;
    void slotResetToDefaults() override;

  private:
    QLineEdit* addDirectoryRow(const QString& label, const QString& objectName);
    QString findAnalysisScript() const;
    void launchAnalysis();

    UserSettingsPointer m_pConfig;
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
