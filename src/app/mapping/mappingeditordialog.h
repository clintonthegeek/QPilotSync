#ifndef MAPPINGEDITORDIALOG_H
#define MAPPINGEDITORDIALOG_H

#include <QDialog>
#include <QJsonArray>

class QStandardItemModel;
class QTableView;

class MappingEditorDialog : public QDialog
{
    Q_OBJECT
public:
    explicit MappingEditorDialog(QWidget *parent = nullptr);
    ~MappingEditorDialog() override = default;

    void setMappings(const QJsonArray &json);
    QJsonArray mappings() const;

    // Test seam — not part of the user-facing UI; called by unit tests
    // to simulate a Delete-button click.
    void removeRowForTest(int row);

private slots:
    void onAddClicked();
    void onEditClicked();
    void onDeleteClicked();

private:
    void buildUi();
    void appendRow(const QJsonObject &mapping);
    QJsonObject rowToJson(int row) const;
    void setRowFromJson(int row, const QJsonObject &json);

    QTableView         *m_tableView = nullptr;
    QStandardItemModel *m_model     = nullptr;
};

#endif // MAPPINGEDITORDIALOG_H
