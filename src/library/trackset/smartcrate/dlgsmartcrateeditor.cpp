#include "library/trackset/smartcrate/dlgsmartcrateeditor.h"

#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonValue>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include "moc_dlgsmartcrateeditor.cpp"

namespace {

// Selectable fields: display label + spec key. Order is intentional (the numeric
// fields first, then key, then the text fields, then free text).
struct FieldDef {
    const char* label;
    const char* key;
};
const QList<FieldDef> kFields = {
        {"BPM", "bpm"},
        {"Rating", "rating"},
        {"Year", "year"},
        {"Duration (s)", "duration"},
        {"Key", "key"},
        {"Artist", "artist"},
        {"Title", "title"},
        {"Album", "album"},
        {"Comment", "comment"},
        {"Any text", "text"},
};

struct OpDef {
    const char* label;
    const char* key;
};

// Ops per field type -- must stay in lockstep with smartcratespec.cpp.
QList<OpDef> opsForType(const QString& type) {
    if (type == QLatin1String("num")) {
        return {{"is (=)", "is"},
                {"at least (>=)", ">="},
                {"at most (<=)", "<="},
                {"between", "between"}};
    }
    if (type == QLatin1String("key")) {
        return {{"is", "is"}, {"harmonic (~)", "harmonic"}};
    }
    if (type == QLatin1String("free")) {
        return {{"contains", "contains"}};
    }
    // text
    return {{"contains", "contains"},
            {"is", "is"},
            {"doesn't contain", "not_contains"}};
}

void selectComboData(QComboBox* pCombo, const QString& data) {
    const int idx = pCombo->findData(data);
    if (idx >= 0) {
        pCombo->setCurrentIndex(idx);
    }
}

// Turn a JSON scalar into the value-edit text (inverse of parseValueText).
QString jsonScalarToText(const QJsonValue& value) {
    if (value.isDouble()) {
        return QString::number(value.toDouble());
    }
    return value.toString();
}

} // namespace

DlgSmartCrateEditor::DlgSmartCrateEditor(QWidget* pParent)
        : QDialog(pParent),
          m_pName(new QLineEdit(this)),
          m_pMatch(new QComboBox(this)),
          m_pRowsLayout(new QVBoxLayout()),
          m_pAddRow(new QPushButton(tr("+ Add rule"), this)),
          m_pSave(new QPushButton(tr("Save"), this)),
          m_pCancel(new QPushButton(tr("Cancel"), this)) {
    setObjectName(QStringLiteral("SmartCrateEditor"));
    setWindowTitle(tr("Smart Crate"));
    setModal(true);

    m_pName->setObjectName(QStringLiteral("SmartCrateName"));
    m_pName->setPlaceholderText(tr("smart crate name"));

    m_pMatch->setObjectName(QStringLiteral("SmartCrateMatch"));
    m_pMatch->addItem(tr("Match ALL rules"), QStringLiteral("all"));
    m_pMatch->addItem(tr("Match ANY rule"), QStringLiteral("any"));

    m_pAddRow->setObjectName(QStringLiteral("SmartCrateAddRow"));
    m_pAddRow->setFocusPolicy(Qt::NoFocus);
    m_pSave->setObjectName(QStringLiteral("SmartCrateSave"));
    m_pSave->setDefault(true);
    m_pCancel->setObjectName(QStringLiteral("SmartCrateCancel"));
    m_pCancel->setFocusPolicy(Qt::NoFocus);

    // --- Header form (name + match) ------------------------------------------
    auto* pForm = new QFormLayout();
    pForm->setContentsMargins(0, 0, 0, 0);
    pForm->setSpacing(6);
    pForm->addRow(new QLabel(tr("Name"), this), m_pName);
    pForm->addRow(new QLabel(tr("Match"), this), m_pMatch);

    // --- Rules area ----------------------------------------------------------
    m_pRowsLayout->setContentsMargins(0, 0, 0, 0);
    m_pRowsLayout->setSpacing(4);

    auto* pRulesHeader = new QLabel(tr("RULES"), this);
    pRulesHeader->setObjectName(QStringLiteral("SmartCrateRulesHeader"));

    auto* pAddRowRow = new QHBoxLayout();
    pAddRowRow->setContentsMargins(0, 0, 0, 0);
    pAddRowRow->addWidget(m_pAddRow);
    pAddRowRow->addStretch(1);

    // --- Buttons -------------------------------------------------------------
    auto* pButtonRow = new QHBoxLayout();
    pButtonRow->setContentsMargins(0, 0, 0, 0);
    pButtonRow->addStretch(1);
    pButtonRow->addWidget(m_pCancel);
    pButtonRow->addWidget(m_pSave);

    // --- Assemble ------------------------------------------------------------
    auto* pMain = new QVBoxLayout(this);
    pMain->setContentsMargins(14, 14, 14, 14);
    pMain->setSpacing(8);
    pMain->addLayout(pForm);
    pMain->addWidget(pRulesHeader);
    pMain->addLayout(m_pRowsLayout);
    pMain->addLayout(pAddRowRow);
    pMain->addStretch(1);
    pMain->addLayout(pButtonRow);

    connect(m_pAddRow, &QPushButton::clicked, this, &DlgSmartCrateEditor::addConditionRow);
    connect(m_pSave, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_pCancel, &QPushButton::clicked, this, &QDialog::reject);

    setMinimumWidth(460);

    // Start with a single blank rule so the dialog is never empty.
    appendRow();
}

DlgSmartCrateEditor::Row* DlgSmartCrateEditor::appendRow() {
    auto* pRow = new Row();
    pRow->container = new QWidget(this);
    pRow->container->setObjectName(QStringLiteral("SmartCrateRuleRow"));
    pRow->field = new QComboBox(pRow->container);
    pRow->field->setObjectName(QStringLiteral("SmartCrateField"));
    pRow->op = new QComboBox(pRow->container);
    pRow->op->setObjectName(QStringLiteral("SmartCrateOp"));
    pRow->value = new QLineEdit(pRow->container);
    pRow->value->setObjectName(QStringLiteral("SmartCrateValue"));
    pRow->value->setPlaceholderText(tr("value"));
    pRow->remove = new QPushButton(QStringLiteral("-"), pRow->container);
    pRow->remove->setObjectName(QStringLiteral("SmartCrateRemoveRow"));
    pRow->remove->setFocusPolicy(Qt::NoFocus);
    pRow->remove->setFixedWidth(28);

    for (const FieldDef& f : kFields) {
        pRow->field->addItem(tr(f.label), QString::fromLatin1(f.key));
    }

    auto* pLayout = new QHBoxLayout(pRow->container);
    pLayout->setContentsMargins(0, 0, 0, 0);
    pLayout->setSpacing(6);
    pLayout->addWidget(pRow->field);
    pLayout->addWidget(pRow->op);
    pLayout->addWidget(pRow->value, 1);
    pLayout->addWidget(pRow->remove);

    repopulateOps(pRow);

    connect(pRow->field,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this,
            [this, pRow]() {
                repopulateOps(pRow);
            });
    connect(pRow->remove, &QPushButton::clicked, this, [this, pRow]() {
        removeRow(pRow);
    });

    m_pRowsLayout->addWidget(pRow->container);
    m_rows.append(pRow);
    return pRow;
}

void DlgSmartCrateEditor::removeRow(Row* pRow) {
    const int idx = m_rows.indexOf(pRow);
    if (idx < 0) {
        return;
    }
    m_rows.removeAt(idx);
    m_pRowsLayout->removeWidget(pRow->container);
    pRow->container->deleteLater();
    delete pRow;
}

void DlgSmartCrateEditor::clearRows() {
    while (!m_rows.isEmpty()) {
        removeRow(m_rows.last());
    }
}

void DlgSmartCrateEditor::repopulateOps(Row* pRow, const QString& selectOp) {
    const QString field = pRow->field->currentData().toString();
    const QString wantOp = selectOp.isEmpty() ? pRow->op->currentData().toString() : selectOp;
    pRow->op->clear();
    for (const OpDef& o : opsForType(fieldType(field))) {
        pRow->op->addItem(tr(o.label), QString::fromLatin1(o.key));
    }
    if (!wantOp.isEmpty()) {
        selectComboData(pRow->op, wantOp);
    }
    // "between" wants two numbers; hint the format.
    pRow->value->setPlaceholderText(
            pRow->op->currentData().toString() == QLatin1String("between")
                    ? tr("low, high")
                    : tr("value"));
}

// static
QString DlgSmartCrateEditor::fieldType(const QString& field) {
    if (field == QLatin1String("bpm") || field == QLatin1String("rating") ||
            field == QLatin1String("year") || field == QLatin1String("duration")) {
        return QStringLiteral("num");
    }
    if (field == QLatin1String("key")) {
        return QStringLiteral("key");
    }
    if (field == QLatin1String("text")) {
        return QStringLiteral("free");
    }
    return QStringLiteral("text");
}

void DlgSmartCrateEditor::addConditionRow() {
    appendRow();
}

void DlgSmartCrateEditor::configureRow(int index,
        const QString& field,
        const QString& op,
        const QString& valueText) {
    if (index < 0 || index >= m_rows.size()) {
        return;
    }
    Row* pRow = m_rows.at(index);
    selectComboData(pRow->field, field);
    repopulateOps(pRow, op);
    pRow->value->setText(valueText);
}

int DlgSmartCrateEditor::conditionRowCount() const {
    return m_rows.size();
}

QString DlgSmartCrateEditor::crateName() const {
    return m_pName->text();
}

QString DlgSmartCrateEditor::matchMode() const {
    return m_pMatch->currentData().toString();
}

void DlgSmartCrateEditor::setCrateName(const QString& name) {
    m_pName->setText(name);
}

void DlgSmartCrateEditor::setMatchMode(const QString& mode) {
    selectComboData(m_pMatch, mode.isEmpty() ? QStringLiteral("all") : mode);
}

QJsonObject DlgSmartCrateEditor::buildSpec() const {
    QJsonObject spec;
    spec.insert(QStringLiteral("match"), matchMode());

    QJsonArray conditions;
    for (const Row* pRow : m_rows) {
        const QString field = pRow->field->currentData().toString();
        const QString op = pRow->op->currentData().toString();
        const QString type = fieldType(field);
        const QString text = pRow->value->text().trimmed();

        QJsonValue value;
        if (type == QLatin1String("num") && op == QLatin1String("between")) {
            // canonical form "low, high"; tolerate "low-high" too.
            QStringList parts = text.split(QChar(','));
            if (parts.size() < 2 && text.contains(QChar('-'))) {
                parts = text.split(QChar('-'));
            }
            QJsonArray range;
            for (int i = 0; i < 2; ++i) {
                const QString piece = (i < parts.size()) ? parts.at(i).trimmed() : QString();
                bool ok = false;
                const double d = piece.toDouble(&ok);
                range.append(ok ? QJsonValue(d) : QJsonValue(piece));
            }
            value = range;
        } else if (type == QLatin1String("num")) {
            bool ok = false;
            const double d = text.toDouble(&ok);
            value = ok ? QJsonValue(d) : QJsonValue(text);
        } else {
            value = text;
        }

        QJsonObject condition;
        condition.insert(QStringLiteral("field"), field);
        condition.insert(QStringLiteral("op"), op);
        condition.insert(QStringLiteral("value"), value);
        conditions.append(condition);
    }
    spec.insert(QStringLiteral("conditions"), conditions);
    return spec;
}

void DlgSmartCrateEditor::loadSpec(const QString& name, const QJsonObject& spec) {
    setCrateName(name);
    setMatchMode(spec.value(QStringLiteral("match")).toString());

    clearRows();
    const QJsonArray conditions = spec.value(QStringLiteral("conditions")).toArray();
    for (const QJsonValue& raw : conditions) {
        if (!raw.isObject()) {
            continue;
        }
        const QJsonObject condition = raw.toObject();
        Row* pRow = appendRow();
        selectComboData(pRow->field, condition.value(QStringLiteral("field")).toString());
        repopulateOps(pRow, condition.value(QStringLiteral("op")).toString());

        const QJsonValue value = condition.value(QStringLiteral("value"));
        QString text;
        if (value.isArray()) {
            QStringList pieces;
            const QJsonArray arr = value.toArray();
            for (const QJsonValue& element : arr) {
                pieces.append(jsonScalarToText(element));
            }
            text = pieces.join(QStringLiteral(", "));
        } else {
            text = jsonScalarToText(value);
        }
        pRow->value->setText(text);
    }
    // Never leave the editor with zero rows.
    if (m_rows.isEmpty()) {
        appendRow();
    }
}
