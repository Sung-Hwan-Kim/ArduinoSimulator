#pragma once
#include <QDialog>
#include <QVariant>
#include <QVector>

class QCheckBox;
class QComboBox;
class QTreeWidget;

struct WizardItem {
    QString id;
    int count = 1;
    QString paramKey;
    QVariant paramValue;
};

class WizardDialog : public QDialog
{
    Q_OBJECT
public:
    explicit WizardDialog(QWidget* parent = nullptr);
    QVector<WizardItem> items() const;
    QString boardId() const;
    bool clearExisting() const;
    void done(int r) override;

private:
    QTreeWidget* m_tree;
    QComboBox* m_board;
    QCheckBox* m_clear;
};
