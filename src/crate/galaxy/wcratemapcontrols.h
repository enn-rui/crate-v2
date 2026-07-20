#pragma once

#include <QWidget>

#include <memory>

#include "preferences/usersettings.h"

class ControlObject;
class ControlPushButton;
class QComboBox;
class QLabel;
class QPushButton;

namespace crate {

class WCrateMapControls final : public QWidget {
    Q_OBJECT
  public:
    explicit WCrateMapControls(QWidget* pParent, UserSettingsPointer pConfig);
    ~WCrateMapControls() override;

  private:
    void syncLayout(double value);
    void syncColor(double value);
    void sync3d(double value);
    void syncHalos(double value);
    void syncKnob(double value);
    void syncLayoutStatus(double value);

    UserSettingsPointer m_pConfig;
    QComboBox* m_pLayoutCombo;
    QComboBox* m_pColorCombo;
    QPushButton* m_p3dButton;
    QPushButton* m_pHaloButton;
    QPushButton* m_pKnobButton;
    QLabel* m_pLayoutStatus;
    std::unique_ptr<ControlObject> m_pLayoutCO;
    std::unique_ptr<ControlObject> m_pColorCO;
    std::unique_ptr<ControlPushButton> m_p3dCO;
    std::unique_ptr<ControlPushButton> m_pHaloCO;
    std::unique_ptr<ControlPushButton> m_pKnobCO;
    std::unique_ptr<ControlObject> m_pLayoutDegradedCO;
};

} // namespace crate
