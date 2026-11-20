#include "AppSettings.h"
#include "Version.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

AppSettings& AppSettings::instance()
{
    static AppSettings s;
    return s;
}

QString AppSettings::iniPath()
{
    static QString cached;
    if (!cached.isEmpty()) return cached;
    // 1순위: 실행 파일 폴더 (포터블), 쓰기 불가하면 AppData
    QString dir = QCoreApplication::applicationDirPath();
    QString path = dir + "/ArduinoSimulator.ini";
    QFileInfo fi(dir);
    bool writable = fi.isWritable();
    if (writable) {
        QFile test(dir + "/.write_test");
        writable = test.open(QIODevice::WriteOnly);
        if (writable) { test.close(); test.remove(); }
    }
    if (!writable) {
        QString cfg = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
        QDir().mkpath(cfg);
        path = cfg + "/ArduinoSimulator.ini";
    }
    cached = path;
    return cached;
}

std::unique_ptr<QSettings> AppSettings::open()
{
    return std::make_unique<QSettings>(iniPath(), QSettings::IniFormat);
}

QString AppSettings::effectiveWorkDir() const
{
    if (!workDir.isEmpty() && QDir(workDir).exists()) return workDir;
    QString docs = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + "/ArduinoSimulator";
    QDir().mkpath(docs);
    return docs;
}

void AppSettings::addRecentFile(const QString& path)
{
    QString p = QDir::fromNativeSeparators(path);
    recentFiles.removeAll(p);
    recentFiles.prepend(p);
    while (recentFiles.size() > 10) recentFiles.removeLast();
    lastFile = p;
}

void AppSettings::load()
{
    auto s = open();
    s->beginGroup("General");
    workDir = s->value("WorkDir", workDir).toString();
    defaultBoard = s->value("DefaultBoard", defaultBoard).toString();
    openLastFile = s->value("OpenLastFile", openLastFile).toBool();
    lastFile = s->value("LastFile").toString();
    recentFiles = s->value("RecentFiles").toStringList();
    autoSaveMinutes = s->value("AutoSaveMinutes", autoSaveMinutes).toInt();
    s->endGroup();

    s->beginGroup("Simulation");
    pinCurrentLimit = s->value("PinCurrentLimit_mA", pinCurrentLimit).toDouble();
    ledBurnFactor = s->value("LedBurnFactor", ledBurnFactor).toDouble();
    usbCurrentLimit = s->value("UsbCurrentLimit_mA", usbCurrentLimit).toDouble();
    warnFloating = s->value("WarnFloatingInput", warnFloating).toBool();
    s->endGroup();

    s->beginGroup("Resistor");
    ledTargetCurrent = s->value("LedTargetCurrent_mA", ledTargetCurrent).toDouble();
    resistorSeries = s->value("Series", resistorSeries).toString();
    pullResistor = s->value("PullResistor_Ohm", pullResistor).toDouble();
    autoInsertResistors = s->value("AutoInsertResistors", autoInsertResistors).toBool();
    generateCodeOnAutoWire = s->value("GenerateCodeOnAutoWire", generateCodeOnAutoWire).toBool();
    s->endGroup();

    s->beginGroup("Appearance");
    theme = s->value("Theme", theme).toString();
    if (theme != "light") theme = "dark";
    s->endGroup();

    s->beginGroup("Editor");
    editorFont = s->value("Font", editorFont).toString();
    editorFontSize = s->value("FontSize", editorFontSize).toInt();
    showGrid = s->value("ShowGrid", showGrid).toBool();
    snapToGrid = s->value("SnapToGrid", snapToGrid).toBool();
    s->endGroup();

    customParts.clear();
    int n = s->beginReadArray("CustomParts");
    for (int i = 0; i < n; ++i) {
        s->setArrayIndex(i);
        CustomPartSpec c;
        c.name = s->value("Name").toString();
        c.desc = s->value("Description").toString();
        c.pins = s->value("Pins", "VCC,GND,OUT").toString();
        c.output = s->value("Output", "analog").toString();
        c.vmin = s->value("Vmin", 0.0).toDouble();
        c.vmax = s->value("Vmax", 5.0).toDouble();
        c.color = QColor(s->value("Color", "#1e5aa0").toString());
        if (!c.name.trimmed().isEmpty()) customParts << c;
    }
    s->endArray();
}

void AppSettings::save() const
{
    auto s = open();
    s->beginGroup("General");
    s->setValue("AppVersion", QStringLiteral(APP_VERSION_STR));
    s->setValue("WorkDir", workDir);
    s->setValue("DefaultBoard", defaultBoard);
    s->setValue("OpenLastFile", openLastFile);
    s->setValue("LastFile", lastFile);
    s->setValue("RecentFiles", recentFiles);
    s->setValue("AutoSaveMinutes", autoSaveMinutes);
    s->endGroup();

    s->beginGroup("Simulation");
    s->setValue("PinCurrentLimit_mA", pinCurrentLimit);
    s->setValue("LedBurnFactor", ledBurnFactor);
    s->setValue("UsbCurrentLimit_mA", usbCurrentLimit);
    s->setValue("WarnFloatingInput", warnFloating);
    s->endGroup();

    s->beginGroup("Resistor");
    s->setValue("LedTargetCurrent_mA", ledTargetCurrent);
    s->setValue("Series", resistorSeries);
    s->setValue("PullResistor_Ohm", pullResistor);
    s->setValue("AutoInsertResistors", autoInsertResistors);
    s->setValue("GenerateCodeOnAutoWire", generateCodeOnAutoWire);
    s->endGroup();

    s->beginGroup("Appearance");
    s->setValue("Theme", theme);
    s->endGroup();

    s->beginGroup("Editor");
    s->setValue("Font", editorFont);
    s->setValue("FontSize", editorFontSize);
    s->setValue("ShowGrid", showGrid);
    s->setValue("SnapToGrid", snapToGrid);
    s->endGroup();

    s->remove("CustomParts");
    s->beginWriteArray("CustomParts", customParts.size());
    for (int i = 0; i < customParts.size(); ++i) {
        s->setArrayIndex(i);
        const CustomPartSpec& c = customParts[i];
        s->setValue("Name", c.name);
        s->setValue("Description", c.desc);
        s->setValue("Pins", c.pins);
        s->setValue("Output", c.output);
        s->setValue("Vmin", c.vmin);
        s->setValue("Vmax", c.vmax);
        s->setValue("Color", c.color.name());
    }
    s->endArray();
    s->sync();
}
