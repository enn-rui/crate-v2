#include "crate/galaxy/wcratemapcontrols.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include "control/controlobject.h"
#include "control/controlpushbutton.h"

namespace {

class ElidingLabel final : public QLabel {
  public:
    using QLabel::QLabel;

  protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setPen(palette().color(foregroundRole()));
        painter.drawText(rect(), alignment(),
                fontMetrics().elidedText(text(), Qt::ElideRight, width()));
    }
};

constexpr int kScatter = 0;
constexpr int kKeyWheel = 1;
constexpr int kBpmSerpentine = 2;
constexpr int kArtist = 3;

constexpr int kCluster = 0;
constexpr int kKey = 1;
constexpr int kTempo = 2;
constexpr int kEnergy = 3;

int savedLayout(const UserSettingsPointer& pConfig) {
    const QString value = pConfig->getValue(
            ConfigKey("[Crate]", "galaxy_layout"), QStringLiteral("scatter"));
    if (value == QStringLiteral("key")) return kKeyWheel;
    if (value == QStringLiteral("bpm")) return kBpmSerpentine;
    if (value == QStringLiteral("artist")) return kArtist;
    return kScatter;
}

int savedColor(const UserSettingsPointer& pConfig) {
    const QString value = pConfig->getValue(
            ConfigKey("[Crate]", "galaxy_color_mode"), QStringLiteral("cluster"));
    if (value == QStringLiteral("key")) return kKey;
    if (value == QStringLiteral("tempo")) return kTempo;
    if (value == QStringLiteral("energy")) return kEnergy;
    return kCluster;
}

} // namespace

namespace crate {

WCrateMapControls::WCrateMapControls(QWidget* pParent, UserSettingsPointer pConfig)
        : QWidget(pParent),
          m_pConfig(std::move(pConfig)),
          m_pLayoutCombo(new QComboBox(this)),
          m_pColorCombo(new QComboBox(this)),
          m_p3dButton(new QPushButton(QStringLiteral("3D"), this)),
          m_pHaloButton(new QPushButton(QStringLiteral("PLEXUS"), this)),
          m_pTrailButton(new QPushButton(QStringLiteral("TRAIL"), this)),
          m_pKnobButton(new QPushButton(this)),
          m_pLayoutStatus(new ElidingLabel(this)),
          m_pLayoutCO(std::make_unique<ControlObject>(
                  ConfigKey("[Crate]", "galaxy_layout_control"))),
          m_pColorCO(std::make_unique<ControlObject>(
                  ConfigKey("[Crate]", "galaxy_color_control"))),
          m_p3dCO(std::make_unique<ControlPushButton>(
                  ConfigKey("[Crate]", "galaxy_3d"))),
          m_pHaloCO(std::make_unique<ControlPushButton>(
                  ConfigKey("[Crate]", "galaxy_halos"))),
          m_pTrailCO(std::make_unique<ControlPushButton>(
                  ConfigKey("[Crate]", "galaxy_trail"))),
          m_pKnobCO(std::make_unique<ControlPushButton>(
                  ConfigKey("[Crate]", "knob_focus"))),
          m_pLayoutDegradedCO(std::make_unique<ControlObject>(
                  ConfigKey("[Crate]", "galaxy_layout_degraded_count"))) {
    setObjectName(QStringLiteral("CrateMapControls"));
    setMinimumWidth(150);

    auto* pMain = new QVBoxLayout(this);
    pMain->setContentsMargins(8, 6, 8, 7);
    pMain->setSpacing(4);
    auto* pHeader = new QLabel(QStringLiteral("MAP"), this);
    pHeader->setObjectName(QStringLiteral("CrateMapHeader"));
    pMain->addWidget(pHeader);

    const auto addComboRow = [this, pMain](const QString& label, QComboBox* pCombo) {
        auto* pLabel = new QLabel(label, this);
        pLabel->setObjectName(QStringLiteral("CrateMapFieldLabel"));
        pCombo->setFocusPolicy(Qt::NoFocus);
        pCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        pMain->addWidget(pLabel);
        pMain->addWidget(pCombo);
    };

    m_pLayoutCombo->setObjectName(QStringLiteral("CrateMapLayout"));
    m_pLayoutCombo->addItems({QStringLiteral("Scatter"), QStringLiteral("Key wheel"),
            QStringLiteral("BPM serpentine"), QStringLiteral("Artist")});
    addComboRow(QStringLiteral("LAYOUT"), m_pLayoutCombo);
    m_pColorCombo->setObjectName(QStringLiteral("CrateMapColor"));
    m_pColorCombo->addItems({QStringLiteral("Cluster"), QStringLiteral("Key"),
            QStringLiteral("Tempo"), QStringLiteral("Energy")});
    addComboRow(QStringLiteral("COLOR"), m_pColorCombo);

    // Compact toggle rows: [3D][PLEXUS], then [TRAIL], then KNOB.
    // row (three-in-a-row clipped the knob label to "OB:TAB" at sidebar width).
    auto* pButtons = new QHBoxLayout();
    pButtons->setContentsMargins(0, 0, 0, 0);
    pButtons->setSpacing(4);
    for (QPushButton* pButton : {m_p3dButton, m_pHaloButton, m_pTrailButton, m_pKnobButton}) {
        pButton->setCheckable(true);
        pButton->setFocusPolicy(Qt::NoFocus);
    }
    pButtons->addWidget(m_p3dButton);
    pButtons->addWidget(m_pHaloButton);
    m_p3dButton->setObjectName(QStringLiteral("CrateMap3d"));
    m_pHaloButton->setObjectName(QStringLiteral("CrateMapHalo"));
    m_pTrailButton->setObjectName(QStringLiteral("CrateMapTrail"));
    m_pKnobButton->setObjectName(QStringLiteral("CrateMapKnob"));
    m_p3dButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_pHaloButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_pTrailButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_pKnobButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    pMain->addLayout(pButtons);
    pMain->addWidget(m_pTrailButton);
    pMain->addWidget(m_pKnobButton);
    m_pLayoutStatus->setObjectName(QStringLiteral("CrateMapLayoutStatus"));
    m_pLayoutStatus->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    m_pLayoutStatus->setFixedHeight(fontMetrics().height());
    m_pLayoutStatus->hide();
    pMain->addWidget(m_pLayoutStatus);

    m_p3dCO->setButtonMode(mixxx::control::ButtonMode::Toggle);
    m_pHaloCO->setButtonMode(mixxx::control::ButtonMode::Toggle);
    m_pTrailCO->setButtonMode(mixxx::control::ButtonMode::Toggle);
    m_pKnobCO->setButtonMode(mixxx::control::ButtonMode::Toggle);
    m_pLayoutCO->set(savedLayout(m_pConfig));
    m_pColorCO->set(savedColor(m_pConfig));
    m_p3dCO->set(m_pConfig->getValue(ConfigKey("[Crate]", "galaxy_3d"), 0));
    m_pHaloCO->set(m_pConfig->getValue(ConfigKey("[Crate]", "galaxy_halos"), 1));
    m_pTrailCO->set(m_pConfig->getValue(ConfigKey("[Crate]", "galaxy_trail"), 1));
    m_pKnobCO->set(m_pConfig->getValue(ConfigKey("[Crate]", "knob_focus"), 0));

    connect(m_pLayoutCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int index) { m_pLayoutCO->set(index); });
    connect(m_pColorCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int index) { m_pColorCO->set(index); });
    connect(m_p3dButton, &QPushButton::clicked,
            this, [this](bool checked) { m_p3dCO->set(checked ? 1.0 : 0.0); });
    connect(m_pHaloButton, &QPushButton::clicked,
            this, [this](bool checked) { m_pHaloCO->set(checked ? 1.0 : 0.0); });
    connect(m_pTrailButton, &QPushButton::clicked,
            this, [this](bool checked) { m_pTrailCO->set(checked ? 1.0 : 0.0); });
    connect(m_pKnobButton, &QPushButton::clicked,
            this, [this](bool checked) {
                syncKnob(checked ? 1.0 : 0.0);
                m_pKnobCO->set(checked ? 1.0 : 0.0);
            });
    connect(m_pLayoutCO.get(), &ControlObject::valueChanged,
            this, &WCrateMapControls::syncLayout);
    connect(m_pColorCO.get(), &ControlObject::valueChanged,
            this, &WCrateMapControls::syncColor);
    connect(m_p3dCO.get(), &ControlObject::valueChanged,
            this, &WCrateMapControls::sync3d);
    connect(m_pHaloCO.get(), &ControlObject::valueChanged,
            this, &WCrateMapControls::syncHalos);
    connect(m_pTrailCO.get(), &ControlObject::valueChanged,
            this, &WCrateMapControls::syncTrail);
    connect(m_pKnobCO.get(), &ControlObject::valueChanged,
            this, &WCrateMapControls::syncKnob);
    connect(m_pLayoutDegradedCO.get(), &ControlObject::valueChanged,
            this, &WCrateMapControls::syncLayoutStatus);
    syncLayout(m_pLayoutCO->get());
    syncColor(m_pColorCO->get());
    sync3d(m_p3dCO->get());
    syncHalos(m_pHaloCO->get());
    syncTrail(m_pTrailCO->get());
    syncKnob(m_pKnobCO->get());
    syncLayoutStatus(m_pLayoutDegradedCO->get());
}

WCrateMapControls::~WCrateMapControls() = default;

void WCrateMapControls::syncLayout(double value) {
    const QSignalBlocker blocker(m_pLayoutCombo);
    m_pLayoutCombo->setCurrentIndex(qBound(0, qRound(value), 3));
    syncLayoutStatus(m_pLayoutDegradedCO->get());
}

void WCrateMapControls::syncColor(double value) {
    const QSignalBlocker blocker(m_pColorCombo);
    m_pColorCombo->setCurrentIndex(qBound(0, qRound(value), 3));
}

void WCrateMapControls::sync3d(double value) {
    const QSignalBlocker blocker(m_p3dButton);
    m_p3dButton->setChecked(value != 0.0);
}

void WCrateMapControls::syncLayoutStatus(double value) {
    const int count = qMax(0, qRound(value));
    QString status;
    if (count > 0) {
        switch (m_pLayoutCombo->currentIndex()) {
        case kKeyWheel:
            status = QStringLiteral("%1 tracks missing key - piled at edge").arg(count);
            break;
        case kBpmSerpentine:
            status = QStringLiteral("%1 tracks missing bpm - piled at edge").arg(count);
            break;
        case kArtist:
            status = QStringLiteral("artist positions missing for %1 tracks").arg(count);
            break;
        default:
            break;
        }
    }
    m_pLayoutStatus->setText(status);
    m_pLayoutStatus->setVisible(!status.isEmpty());
    layout()->invalidate();
    setFixedHeight(layout()->minimumSize().height());
}

void WCrateMapControls::syncHalos(double value) {
    const QSignalBlocker blocker(m_pHaloButton);
    m_pHaloButton->setChecked(value != 0.0);
}

void WCrateMapControls::syncTrail(double value) {
    const QSignalBlocker blocker(m_pTrailButton);
    m_pTrailButton->setChecked(value != 0.0);
}

void WCrateMapControls::syncKnob(double value) {
    const QSignalBlocker blocker(m_pKnobButton);
    const bool map = value != 0.0;
    m_pKnobButton->setChecked(map);
    m_pKnobButton->setText(map ? QStringLiteral("KNOB:MAP") : QStringLiteral("KNOB:TABLE"));
}

} // namespace crate

#include "moc_wcratemapcontrols.cpp"
