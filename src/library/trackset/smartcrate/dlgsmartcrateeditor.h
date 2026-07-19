#pragma once

#include <QDialog>
#include <QJsonObject>
#include <QList>
#include <QString>

class QComboBox;
class QLineEdit;
class QPushButton;
class QVBoxLayout;
class QWidget;

// Modal rule editor for a smart crate (wave-6). Plain widgets, object names for
// QSS, ink-on-black styling to match the Crate skin. The field/op menus mirror
// exactly what the SmartCrate translator supports (see smartcratespec.cpp):
//   numeric (bpm/rating/year/duration): is, >=, <=, between
//   key:                                is, harmonic
//   text (artist/title/album/comment):  contains, is, not_contains
//   free text ("Any text"):             contains
//
// buildSpec() and loadSpec() are exact inverses, so a build -> save -> reload
// round-trip reproduces the same spec JSON.
class DlgSmartCrateEditor : public QDialog {
    Q_OBJECT

  public:
    explicit DlgSmartCrateEditor(QWidget* pParent = nullptr);
    ~DlgSmartCrateEditor() override = default;

    // Populate the dialog from an existing name + spec (for Edit).
    void loadSpec(const QString& name, const QJsonObject& spec);
    // Assemble the current rows into a {match, conditions:[...]} spec object.
    QJsonObject buildSpec() const;

    QString crateName() const;
    QString matchMode() const;
    int conditionRowCount() const;

    // Utility / test seams -- drive the same state the real controls drive.
    void setCrateName(const QString& name);
    void setMatchMode(const QString& mode);
    void addConditionRow();
    void configureRow(int index,
            const QString& field,
            const QString& op,
            const QString& valueText);

  private:
    struct Row {
        QWidget* container;
        QComboBox* field;
        QComboBox* op;
        QLineEdit* value;
        QPushButton* remove;
    };

    Row* appendRow();
    void removeRow(Row* pRow);
    void clearRows();
    void repopulateOps(Row* pRow, const QString& selectOp = QString());
    static QString fieldType(const QString& field);

    QLineEdit* m_pName;
    QComboBox* m_pMatch;
    QVBoxLayout* m_pRowsLayout;
    QPushButton* m_pAddRow;
    QPushButton* m_pSave;
    QPushButton* m_pCancel;
    QList<Row*> m_rows;
};
