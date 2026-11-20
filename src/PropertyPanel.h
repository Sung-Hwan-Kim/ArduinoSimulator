#pragma once
#include <QHash>
#include <QPointer>
#include <QWidget>

class CircuitScene;
class PartItem;
class QFormLayout;
class QLabel;
class QVBoxLayout;

class PropertyPanel : public QWidget
{
    Q_OBJECT
public:
    explicit PropertyPanel(CircuitScene* scene, QWidget* parent = nullptr);
    void setPart(PartItem* part);
    PartItem* part() const { return m_part; }
    void setSimulating(bool on);
    void refreshValues();
    void refreshTheme() { rebuild(); }

signals:
    void paramChanged(PartItem* part, const QString& key);
    void message(const QString& text);

private:
    void rebuild();
    void applyParam(const QString& key, const QVariant& v);

    CircuitScene* m_scene;
    PartItem* m_part = nullptr;
    bool m_simulating = false;
    bool m_updating = false;
    QVBoxLayout* m_root;
    class QScrollArea* m_scroll = nullptr;
    QWidget* m_content = nullptr;
    QHash<QString, QWidget*> m_editors;
};
