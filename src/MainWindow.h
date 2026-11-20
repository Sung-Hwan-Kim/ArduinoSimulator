#pragma once
#include <QMainWindow>
#include <QTimer>

class CircuitScene;
class CircuitView;
class CodeEditor;
class PartLibrary;
class PropertyPanel;
class Simulator;
class PartItem;
class QAction;
class QCheckBox;
class QComboBox;
class QDockWidget;
class QLabel;
class QLineEdit;
class QListWidget;
class QMenu;
class QPlainTextEdit;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    MainWindow();
    ~MainWindow() override;
    void startup();
    // 명령줄 자동 테스트: 예제 불러오기 → 실행 → 스크린샷 → 종료
    void selfTest(int example, bool run, const QString& screenshot, int seconds, const QString& wizardIds);

protected:
    void closeEvent(QCloseEvent* e) override;

private:
    void buildUi();
    void buildMenus();
    void restoreWindowState();
    void saveWindowState();
    void applySettings(bool applyTheme);

    // 파일
    void newCircuit(bool withBoard = true);
    bool openFile(const QString& path);
    bool saveFile(const QString& path);
    bool save();
    bool saveAs();
    bool maybeSave();
    void setModified(bool m);
    void updateTitle();
    void updateRecentMenu();
    void autoSave();

    // 시뮬레이션
    void runSimulation();
    void stopSimulation();
    void updateSimUi();

    // 도구
    void addPartAtCenter(const QString& id);
    void autoPlaceAndWire();
    void autoWireUnconnected();
    void autoResistors();
    void generateCode(bool ask);
    void runWizard();
    void loadExample(int index);
    void showSettings();
    void showHelp();

    void log(const QString& text, int level = 0);
    void appendSerial(const QString& text);
    void sendSerialLine();

    CircuitScene* m_scene;
    CircuitView* m_view;
    Simulator* m_sim;
    PartLibrary* m_lib;
    PropertyPanel* m_props;
    CodeEditor* m_code;
    QPlainTextEdit* m_serialOut;
    QLineEdit* m_serialIn;
    QComboBox* m_lineEnding;
    QCheckBox* m_autoScroll;
    QListWidget* m_log;
    QDockWidget* m_dockLib;
    QDockWidget* m_dockProps;
    QDockWidget* m_dockCode;
    QDockWidget* m_dockSerial;
    QDockWidget* m_dockLog;
    QLabel* m_simLabel;
    QLabel* m_zoomLabel;
    QAction* m_actRun;
    QAction* m_actStop;
    QList<QAction*> m_editActions;
    QMenu* m_recentMenu;
    QTimer m_simClock;
    QTimer m_autoSaveTimer;
    QString m_file;
    bool m_modified = false;
    bool m_serialLineStart = true;
    bool m_selfTest = false;
};
