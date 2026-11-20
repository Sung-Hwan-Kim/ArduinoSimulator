#pragma once
#include <QDialog>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFontComboBox;
class QLineEdit;
class QSpinBox;
class QTableWidget;

class SettingsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget* parent = nullptr);
    void accept() override;
    void done(int r) override;

private:
    void load();
    void addCustomRow(const QString& name, const QString& pins, const QString& output, double vmin, double vmax, const QString& desc);

    QLineEdit* m_workDir;
    QComboBox* m_board;
    QCheckBox* m_openLast;
    QSpinBox* m_autoSave;
    QDoubleSpinBox* m_pinLimit;
    QDoubleSpinBox* m_burn;
    QDoubleSpinBox* m_usb;
    QCheckBox* m_floating;
    QDoubleSpinBox* m_ledTarget;
    QComboBox* m_series;
    QComboBox* m_pull;
    QCheckBox* m_autoRes;
    QCheckBox* m_autoCode;
    QComboBox* m_theme;
    QFontComboBox* m_font;
    QSpinBox* m_fontSize;
    QCheckBox* m_grid;
    QCheckBox* m_snap;
    QTableWidget* m_custom;
};
