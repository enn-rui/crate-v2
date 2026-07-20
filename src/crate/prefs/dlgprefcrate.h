#pragma once

#include "preferences/dialog/dlgpreferencepage.h"
#include "preferences/usersettings.h"

class QCheckBox;
class QLineEdit;
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

    UserSettingsPointer m_pConfig;
    QLineEdit* m_pSidecarDir;
    QLineEdit* m_pMusicRoot;
    QLineEdit* m_pGrabUrl;
    QLineEdit* m_pGrabToken;
    QCheckBox* m_pAutoAnalyze;
    QSpinBox* m_pAnalyzerThreads;
    QCheckBox* m_pGalaxyTrail;
    QCheckBox* m_pGalaxyHalos;
};
