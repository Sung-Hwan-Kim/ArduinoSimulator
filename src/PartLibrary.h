#pragma once
#include <QTreeWidget>
#include <QWidget>

class QLineEdit;

class PartTree : public QTreeWidget
{
    Q_OBJECT
public:
    explicit PartTree(QWidget* parent = nullptr);
protected:
    QMimeData* mimeData(const QList<QTreeWidgetItem*>& items) const override;
    QStringList mimeTypes() const override;
};

class PartLibrary : public QWidget
{
    Q_OBJECT
public:
    explicit PartLibrary(QWidget* parent = nullptr);
    void reload();

signals:
    void partActivated(const QString& id);

private:
    void applyFilter(const QString& text);
    PartTree* m_tree;
    QLineEdit* m_search;
};
