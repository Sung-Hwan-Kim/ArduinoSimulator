#include "MainWindow.h"
#include "AppSettings.h"
#include "AutoWire.h"
#include "Catalog.h"
#include "CircuitScene.h"
#include "CircuitView.h"
#include "CodeEditor.h"
#include "PartItem.h"
#include "PartLibrary.h"
#include "PropertyPanel.h"
#include "ResistorAdvisor.h"
#include "SettingsDialog.h"
#include "Theme.h"
#include "Version.h"
#include "Simulator.h"
#include "WireItem.h"
#include "WizardDialog.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScreen>
#include <QScrollBar>
#include <QStatusBar>
#include <QStyle>
#include <QSysInfo>
#include <QTextStream>
#include <QToolBar>
#include <QVBoxLayout>

namespace {

QString compilerName()
{
#if defined(_MSC_VER)
    return QStringLiteral("MSVC %1 (Visual Studio 2019)").arg(_MSC_VER);
#elif defined(__clang__)
    return QStringLiteral("Clang %1.%2.%3").arg(__clang_major__).arg(__clang_minor__).arg(__clang_patchlevel__);
#elif defined(__GNUC__)
    return QStringLiteral("GCC %1.%2.%3").arg(__GNUC__).arg(__GNUC_MINOR__).arg(__GNUC_PATCHLEVEL__);
#else
    return QStringLiteral("알 수 없음");
#endif
}

const char* kDefaultSketch =
    "// 아두이노 시뮬레이터 - 기본 스케치\n"
    "// 보드의 L(13번 핀) LED 를 1초 간격으로 깜빡입니다.\n"
    "\n"
    "void setup() {\n"
    "  pinMode(LED_BUILTIN, OUTPUT);\n"
    "  Serial.begin(9600);\n"
    "  Serial.println(\"Hello, Arduino!\");\n"
    "}\n"
    "\n"
    "void loop() {\n"
    "  digitalWrite(LED_BUILTIN, HIGH);\n"
    "  delay(1000);\n"
    "  digitalWrite(LED_BUILTIN, LOW);\n"
    "  delay(1000);\n"
    "}\n";

struct Example {
    QString title;
    QStringList parts;
    QString code;   // 비어 있으면 자동 생성
};

QVector<Example> examples()
{
    QVector<Example> ex;
    ex << Example{ QStringLiteral("01. LED 깜빡이기 (Blink)"), { "led" },
                   "// LED 깜빡이기\n"
                   "const int ledPin = {PIN:LED1.A};\n\n"
                   "void setup() {\n  pinMode(ledPin, OUTPUT);\n  pinMode(LED_BUILTIN, OUTPUT);\n}\n\n"
                   "void loop() {\n  digitalWrite(ledPin, HIGH);\n  digitalWrite(LED_BUILTIN, HIGH);\n  delay(500);\n"
                   "  digitalWrite(ledPin, LOW);\n  digitalWrite(LED_BUILTIN, LOW);\n  delay(500);\n}\n" };
    ex << Example{ QStringLiteral("02. 버튼으로 LED 켜기"), { "led", "button" },
                   "// 버튼을 누르는 동안 LED 켜기 (INPUT_PULLUP: 누르면 LOW)\n"
                   "const int ledPin = {PIN:LED1.A};\n"
                   "const int buttonPin = {PIN:SW1.1|2};\n\n"
                   "void setup() {\n  pinMode(ledPin, OUTPUT);\n  pinMode(buttonPin, INPUT_PULLUP);\n  Serial.begin(9600);\n}\n\n"
                   "void loop() {\n  bool pressed = digitalRead(buttonPin) == LOW;\n  digitalWrite(ledPin, pressed ? HIGH : LOW);\n\n"
                   "  static bool last = false;\n  if (pressed != last) {\n    Serial.println(pressed ? \"버튼 눌림\" : \"버튼 뗌\");\n    last = pressed;\n  }\n  delay(10);\n}\n" };
    ex << Example{ QStringLiteral("03. 가변저항으로 LED 밝기 조절 (PWM)"), { "potentiometer", "led" },
                   "// 가변저항 값(0~1023)을 PWM(0~255)으로 변환하여 LED 밝기 조절\n"
                   "const int potPin = {PIN:VR1.W};\n"
                   "const int ledPin = {PIN:LED1.A};   // ~ 표시가 있는 PWM 핀\n\n"
                   "void setup() {\n  pinMode(ledPin, OUTPUT);\n  Serial.begin(9600);\n}\n\n"
                   "void loop() {\n  int value = analogRead(potPin);\n  int bright = map(value, 0, 1023, 0, 255);\n  analogWrite(ledPin, bright);\n\n"
                   "  Serial.print(\"가변저항: \");\n  Serial.print(value);\n  Serial.print(\"  밝기: \");\n  Serial.println(bright);\n  delay(100);\n}\n" };
    ex << Example{ QStringLiteral("04. 조도센서 자동 야간등"), { "ldr", "led" },
                   "// 어두워지면 LED 켜기 (속성 창에서 '주변 밝기' 슬라이더로 조절)\n"
                   "const int ldrPin = {PIN:LDR1.2|1};\n"
                   "const int ledPin = {PIN:LED1.A};\n"
                   "const int threshold = 400;   // 이 값보다 어두우면 켜짐\n\n"
                   "void setup() {\n  pinMode(ledPin, OUTPUT);\n  Serial.begin(9600);\n}\n\n"
                   "void loop() {\n  int light = analogRead(ldrPin);\n  digitalWrite(ledPin, light < threshold ? HIGH : LOW);\n"
                   "  Serial.print(\"밝기 값: \");\n  Serial.print(light);\n  Serial.println(light < threshold ? \"  → 야간등 ON\" : \"  → OFF\");\n  delay(200);\n}\n" };
    ex << Example{ QStringLiteral("05. 초음파 거리 측정 + 경보"), { "hcsr04", "buzzer", "led" },
                   "// 20cm 이내로 물체가 다가오면 부저와 LED 경보\n"
                   "const int trigPin = {PIN:U1.Trig};\n"
                   "const int echoPin = {PIN:U1.Echo};\n"
                   "const int buzzerPin = {PIN:BZ1.+};\n"
                   "const int ledPin = {PIN:LED1.A};\n\n"
                   "void setup() {\n  pinMode(trigPin, OUTPUT);\n  pinMode(echoPin, INPUT);\n  pinMode(buzzerPin, OUTPUT);\n  pinMode(ledPin, OUTPUT);\n  Serial.begin(9600);\n}\n\n"
                   "void loop() {\n  digitalWrite(trigPin, LOW);\n  delayMicroseconds(2);\n  digitalWrite(trigPin, HIGH);\n  delayMicroseconds(10);\n  digitalWrite(trigPin, LOW);\n\n"
                   "  long duration = pulseIn(echoPin, HIGH, 30000);\n  float cm = duration * 0.0343 / 2.0;\n\n"
                   "  bool alarm = duration > 0 && cm < 20;\n  digitalWrite(buzzerPin, alarm ? HIGH : LOW);\n  digitalWrite(ledPin, alarm ? HIGH : LOW);\n\n"
                   "  Serial.print(\"거리: \");\n  Serial.print(cm, 1);\n  Serial.println(alarm ? \" cm  ⚠ 경보!\" : \" cm\");\n  delay(200);\n}\n" };
    ex << Example{ QStringLiteral("06. DHT22 온습도 → LCD 표시"), { "dht", "lcd1602_i2c" },
                   "#include <DHT.h>\n#include <Wire.h>\n#include <LiquidCrystal_I2C.h>\n\n"
                   "DHT dht({PIN:U1.DATA}, DHT22);\n"
                   "LiquidCrystal_I2C lcd(0x27, 16, 2);\n\n"
                   "void setup() {\n  Serial.begin(9600);\n  dht.begin();\n  lcd.init();\n  lcd.backlight();\n  lcd.print(\"DHT22 Monitor\");\n  delay(1000);\n}\n\n"
                   "void loop() {\n  float t = dht.readTemperature();\n  float h = dht.readHumidity();\n\n"
                   "  lcd.setCursor(0, 0);\n  lcd.print(\"Temp: \");\n  lcd.print(t, 1);\n  lcd.print(\" C    \");\n"
                   "  lcd.setCursor(0, 1);\n  lcd.print(\"Humi: \");\n  lcd.print(h, 0);\n  lcd.print(\" %    \");\n\n"
                   "  Serial.print(\"온도 \");\n  Serial.print(t, 1);\n  Serial.print(\"C, 습도 \");\n  Serial.print(h, 0);\n  Serial.println(\"%\");\n  delay(1000);\n}\n" };
    ex << Example{ QStringLiteral("07. 가변저항으로 서보 각도 제어"), { "servo", "potentiometer" },
                   "#include <Servo.h>\n\nServo servo;\n"
                   "const int potPin = {PIN:VR1.W};\n\n"
                   "void setup() {\n  servo.attach({PIN:M1.SIG});\n  Serial.begin(9600);\n}\n\n"
                   "void loop() {\n  int value = analogRead(potPin);\n  int angle = map(value, 0, 1023, 0, 180);\n  servo.write(angle);\n"
                   "  Serial.print(\"각도: \");\n  Serial.println(angle);\n  delay(50);\n}\n" };
    ex << Example{ QStringLiteral("08. RGB LED 색상 순환"), { "rgb_led" }, QString() };
    ex << Example{ QStringLiteral("09. 버튼으로 7세그먼트 숫자 올리기"), { "seven_seg", "button" },
                   "// a b c d e f g dp 순서\n"
                   "const int segPins[8] = {{PIN:DS1.A}, {PIN:DS1.B}, {PIN:DS1.C}, {PIN:DS1.D}, {PIN:DS1.E}, {PIN:DS1.F}, {PIN:DS1.G}, {PIN:DS1.DP}};\n"
                   "const byte font[10] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};\n"
                   "const int buttonPin = {PIN:SW1.1|2};\n"
                   "int count = 0;\nbool lastPressed = false;\n\n"
                   "void show(int n) {\n  for (int i = 0; i < 8; i++) {\n    digitalWrite(segPins[i], bitRead(font[n], i) ? HIGH : LOW);   // 공통 캐소드\n  }\n}\n\n"
                   "void setup() {\n  for (int i = 0; i < 8; i++) pinMode(segPins[i], OUTPUT);\n  pinMode(buttonPin, INPUT_PULLUP);\n  Serial.begin(9600);\n  show(0);\n}\n\n"
                   "void loop() {\n  bool pressed = digitalRead(buttonPin) == LOW;\n  if (pressed && !lastPressed) {\n    count = (count + 1) % 10;\n    show(count);\n"
                   "    Serial.print(\"카운트: \");\n    Serial.println(count);\n  }\n  lastPressed = pressed;\n  delay(20);\n}\n" };
    ex << Example{ QStringLiteral("10. NeoPixel 링 무지개"), { "neopixel_ring" }, QString() };
    ex << Example{ QStringLiteral("11. PIR 인체 감지 경보"), { "pir", "led", "buzzer" },
                   "// PIR 센서를 클릭하면 '움직임 감지' 상태가 토글됩니다\n"
                   "const int pirPin = {PIN:U1.OUT};\n"
                   "const int ledPin = {PIN:LED1.A};\n"
                   "const int buzzerPin = {PIN:BZ1.+};\n\n"
                   "void setup() {\n  pinMode(pirPin, INPUT);\n  pinMode(ledPin, OUTPUT);\n  pinMode(buzzerPin, OUTPUT);\n  Serial.begin(9600);\n}\n\n"
                   "void loop() {\n  bool motion = digitalRead(pirPin) == HIGH;\n  digitalWrite(ledPin, motion);\n  digitalWrite(buzzerPin, motion && (millis() / 200) % 2);\n"
                   "  static bool last = false;\n  if (motion != last) {\n    Serial.println(motion ? \"움직임 감지!\" : \"조용함\");\n    last = motion;\n  }\n  delay(20);\n}\n" };
    ex << Example{ QStringLiteral("12. 인터럽트 버튼 카운터"), { "button" },
                   "// 외부 인터럽트(FALLING)로 버튼 누른 횟수 세기\n"
                   "const int buttonPin = {PIN:SW1.1|2};   // 인터럽트 가능 핀 (Uno: 2, 3)\n"
                   "volatile int presses = 0;\n\n"
                   "void onPress() {\n  presses++;\n}\n\n"
                   "void setup() {\n  pinMode(buttonPin, INPUT_PULLUP);\n  attachInterrupt(digitalPinToInterrupt(buttonPin), onPress, FALLING);\n  Serial.begin(9600);\n}\n\n"
                   "void loop() {\n  static int shown = -1;\n  if (presses != shown) {\n    shown = presses;\n    Serial.print(\"누른 횟수: \");\n    Serial.println(shown);\n  }\n  delay(10);\n}\n" };
    ex << Example{ QStringLiteral("13. 시리얼 명령으로 LED 제어"), { "led" },
                   "// 시리얼 모니터에 1 을 보내면 켜짐, 0 이면 꺼짐\n"
                   "const int ledPin = {PIN:LED1.A};\n\n"
                   "void setup() {\n  pinMode(ledPin, OUTPUT);\n  Serial.begin(9600);\n  Serial.println(\"1=켜기, 0=끄기 를 입력하세요\");\n}\n\n"
                   "void loop() {\n  if (Serial.available() > 0) {\n    char c = Serial.read();\n    if (c == '1') { digitalWrite(ledPin, HIGH); Serial.println(\"LED ON\"); }\n"
                   "    else if (c == '0') { digitalWrite(ledPin, LOW); Serial.println(\"LED OFF\"); }\n  }\n}\n" };
    ex << Example{ QStringLiteral("14. 스텝 모터 회전"), { "stepper" }, QString() };
    ex << Example{ QStringLiteral("15. 센서 종합 대시보드 (자동 생성)"), { "dht", "ldr", "potentiometer", "button", "led", "lcd1602_i2c" }, QString() };
    return ex;
}

} // namespace

// ===========================================================================
MainWindow::MainWindow()
{
    m_scene = new CircuitScene(this);
    m_sim = new Simulator(m_scene, this);
    buildUi();
    buildMenus();
    applySettings(false);
    restoreWindowState();

    connect(m_scene, &CircuitScene::modified, this, [this] { if (!m_sim->isRunning()) setModified(true); });
    connect(m_scene, &CircuitScene::statusMessage, this, [this](const QString& m) { statusBar()->showMessage(m, 5000); });
    connect(m_scene, &QGraphicsScene::selectionChanged, this, [this] {
        PartItem* sel = nullptr;
        const QList<QGraphicsItem*> items = m_scene->selectedItems();
        for (QGraphicsItem* it : items) if (it->type() == PartItem::Type) { sel = static_cast<PartItem*>(it); break; }
        m_props->setPart(sel);
    });
    connect(m_scene, &CircuitScene::partParamChanged, this, [this](PartItem* p, const QString&) {
        if (p == m_props->part()) m_props->refreshValues();
        if (!m_sim->isRunning()) setModified(true);
    });
    connect(m_props, &PropertyPanel::paramChanged, this, [this](PartItem* p, const QString& key) {
        if (m_sim->isRunning()) m_sim->onPartParamChanged(p, key);
        else setModified(true);
    });
    connect(m_props, &PropertyPanel::message, this, [this](const QString& m) { log(m); statusBar()->showMessage(m.section('\n', 0, 0), 6000); });
    connect(m_lib, &PartLibrary::partActivated, this, &MainWindow::addPartAtCenter);
    connect(m_code, &QPlainTextEdit::textChanged, this, [this] { setModified(true); });
    connect(m_sim, &Simulator::serialOutput, this, &MainWindow::appendSerial);
    connect(m_sim, &Simulator::logMessage, this, &MainWindow::log);
    connect(m_sim, &Simulator::runtimeError, this, [this](const QString& msg, int line) {
        m_code->markErrorLine(line, msg);
        m_dockLog->raise();
        statusBar()->showMessage(QStringLiteral("실행 오류 (줄 %1): %2").arg(line).arg(msg), 10000);
    });
    connect(m_sim, &Simulator::stopped, this, &MainWindow::updateSimUi);
    connect(m_view, &CircuitView::zoomChanged, this, [this](double z) { m_zoomLabel->setText(QStringLiteral("확대 %1%").arg((int)(z * 100))); });

    m_simClock.setInterval(200);
    connect(&m_simClock, &QTimer::timeout, this, [this] {
        m_simLabel->setText(m_sim->isRunning() ? QStringLiteral("▶ 실행 중 %1초").arg(m_sim->elapsedSeconds(), 0, 'f', 1) : QStringLiteral("■ 정지"));
    });
    m_simClock.start();
    connect(&m_autoSaveTimer, &QTimer::timeout, this, &MainWindow::autoSave);
}

MainWindow::~MainWindow()
{
    m_sim->stop();
}

void MainWindow::buildUi()
{
    m_view = new CircuitView;
    m_view->setScene(m_scene);
    setCentralWidget(m_view);

    // 부품 목록
    m_lib = new PartLibrary;
    m_dockLib = new QDockWidget(QStringLiteral("부품 목록"), this);
    m_dockLib->setObjectName("dockLibrary");
    m_dockLib->setWidget(m_lib);
    addDockWidget(Qt::LeftDockWidgetArea, m_dockLib);

    // 속성
    m_props = new PropertyPanel(m_scene);
    m_dockProps = new QDockWidget(QStringLiteral("속성 / 규격"), this);
    m_dockProps->setObjectName("dockProperties");
    m_dockProps->setWidget(m_props);
    addDockWidget(Qt::LeftDockWidgetArea, m_dockProps);
    splitDockWidget(m_dockLib, m_dockProps, Qt::Vertical);

    // 코드
    m_code = new CodeEditor;
    auto* codeHolder = new QWidget;
    auto* cv = new QVBoxLayout(codeHolder);
    cv->setContentsMargins(0, 0, 0, 0);
    cv->setSpacing(2);
    auto* codeBar = new QToolBar;
    codeBar->setIconSize(QSize(16, 16));
    codeBar->addAction(style()->standardIcon(QStyle::SP_MediaPlay), QStringLiteral("실행"), this, &MainWindow::runSimulation);
    codeBar->addAction(QStringLiteral("코드 자동 생성"), this, [this] { generateCode(true); });
    codeBar->addAction(QStringLiteral(".ino 가져오기"), this, [this] {
        QString f = QFileDialog::getOpenFileName(this, QStringLiteral("스케치 가져오기"), AppSettings::instance().effectiveWorkDir(), QStringLiteral("Arduino 스케치 (*.ino *.cpp *.txt)"));
        if (f.isEmpty()) return;
        QFile file(f);
        if (file.open(QIODevice::ReadOnly)) m_code->setPlainText(QString::fromUtf8(file.readAll()));
    });
    codeBar->addAction(QStringLiteral(".ino 내보내기"), this, [this] {
        QString base = m_file.isEmpty() ? QStringLiteral("sketch") : QFileInfo(m_file).completeBaseName();
        QString f = QFileDialog::getSaveFileName(this, QStringLiteral("스케치 내보내기"), AppSettings::instance().effectiveWorkDir() + "/" + base + ".ino", QStringLiteral("Arduino 스케치 (*.ino)"));
        if (f.isEmpty()) return;
        QFile file(f);
        if (file.open(QIODevice::WriteOnly)) { file.write(m_code->toPlainText().toUtf8()); log(QStringLiteral("스케치 저장: %1").arg(QDir::toNativeSeparators(f))); }
    });
    cv->addWidget(codeBar);
    cv->addWidget(m_code, 1);
    m_dockCode = new QDockWidget(QStringLiteral("스케치 코드"), this);
    m_dockCode->setObjectName("dockCode");
    m_dockCode->setWidget(codeHolder);
    addDockWidget(Qt::RightDockWidgetArea, m_dockCode);

    // 시리얼 모니터
    auto* serialHolder = new QWidget;
    auto* sv = new QVBoxLayout(serialHolder);
    sv->setContentsMargins(2, 2, 2, 2);
    m_serialOut = new QPlainTextEdit;
    m_serialOut->setReadOnly(true);
    m_serialOut->setMaximumBlockCount(5000);
    QFont mono = Theme::monoFont(10);
    m_serialOut->setFont(mono);
    sv->addWidget(m_serialOut, 1);
    auto* sh = new QHBoxLayout;
    m_serialIn = new QLineEdit;
    m_serialIn->setPlaceholderText(QStringLiteral("보낼 내용 입력 후 Enter"));
    auto* sendBtn = new QPushButton(QStringLiteral("전송"));
    m_lineEnding = new QComboBox;
    m_lineEnding->addItem(QStringLiteral("줄 끝 없음"), "");
    m_lineEnding->addItem(QStringLiteral("새 줄 (\\n)"), "\n");
    m_lineEnding->addItem(QStringLiteral("캐리지 리턴 (\\r)"), "\r");
    m_lineEnding->addItem(QStringLiteral("둘 다 (\\r\\n)"), "\r\n");
    m_lineEnding->setCurrentIndex(1);
    m_autoScroll = new QCheckBox(QStringLiteral("자동 스크롤"));
    m_autoScroll->setChecked(true);
    auto* clearBtn = new QPushButton(QStringLiteral("지우기"));
    sh->addWidget(m_serialIn, 1);
    sh->addWidget(sendBtn);
    sh->addWidget(m_lineEnding);
    sh->addWidget(m_autoScroll);
    sh->addWidget(clearBtn);
    sv->addLayout(sh);
    connect(m_serialIn, &QLineEdit::returnPressed, this, &MainWindow::sendSerialLine);
    connect(sendBtn, &QPushButton::clicked, this, &MainWindow::sendSerialLine);
    connect(clearBtn, &QPushButton::clicked, this, [this] { m_serialOut->clear(); m_serialLineStart = true; });
    m_dockSerial = new QDockWidget(QStringLiteral("시리얼 모니터"), this);
    m_dockSerial->setObjectName("dockSerial");
    m_dockSerial->setWidget(serialHolder);
    addDockWidget(Qt::BottomDockWidgetArea, m_dockSerial);

    // 메시지
    m_log = new QListWidget;
    m_log->setWordWrap(true);
    m_dockLog = new QDockWidget(QStringLiteral("메시지 / 경고"), this);
    m_dockLog->setObjectName("dockLog");
    m_dockLog->setWidget(m_log);
    addDockWidget(Qt::BottomDockWidgetArea, m_dockLog);
    tabifyDockWidget(m_dockSerial, m_dockLog);
    m_dockSerial->raise();

    // 상태 표시줄
    m_simLabel = new QLabel(QStringLiteral("■ 정지"));
    m_zoomLabel = new QLabel(QStringLiteral("확대 100%"));
    statusBar()->addPermanentWidget(m_simLabel);
    statusBar()->addPermanentWidget(m_zoomLabel);
    statusBar()->addPermanentWidget(new QLabel(QStringLiteral("v" APP_VERSION_STR)));
}

void MainWindow::buildMenus()
{
    auto* tb = addToolBar(QStringLiteral("기본 도구"));
    tb->setObjectName("mainToolbar");
    tb->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);

    // ---- 파일 ----
    QMenu* file = menuBar()->addMenu(QStringLiteral("파일(&F)"));
    QAction* aNew = file->addAction(style()->standardIcon(QStyle::SP_FileIcon), QStringLiteral("새 회로(&N)"), this, [this] { if (maybeSave()) newCircuit(true); });
    aNew->setShortcut(QKeySequence::New);
    QAction* aOpen = file->addAction(style()->standardIcon(QStyle::SP_DialogOpenButton), QStringLiteral("열기(&O)..."), this, [this] {
        if (!maybeSave()) return;
        QString f = QFileDialog::getOpenFileName(this, QStringLiteral("회로 열기"), AppSettings::instance().effectiveWorkDir(), QStringLiteral("아두이노 시뮬레이터 회로 (*.asim);;모든 파일 (*.*)"));
        if (!f.isEmpty()) openFile(f);
    });
    aOpen->setShortcut(QKeySequence::Open);
    QAction* aSave = file->addAction(style()->standardIcon(QStyle::SP_DialogSaveButton), QStringLiteral("저장(&S)"), this, [this] { save(); });
    aSave->setShortcut(QKeySequence::Save);
    QAction* aSaveAs = file->addAction(QStringLiteral("다른 이름으로 저장(&A)..."), this, [this] { saveAs(); });
    aSaveAs->setShortcut(QKeySequence("Ctrl+Shift+S"));
    m_recentMenu = file->addMenu(QStringLiteral("최근 파일"));
    file->addSeparator();
    file->addAction(QStringLiteral("작업 폴더 열기"), this, [this] {
        QString d = AppSettings::instance().effectiveWorkDir();
        QFileDialog::getOpenFileName(this, QStringLiteral("작업 폴더"), d, QStringLiteral("아두이노 시뮬레이터 회로 (*.asim)"));
    });
    file->addSeparator();
    // macOS: 종료/환경설정/버전 정보는 앱 메뉴(아두이노 시뮬레이터 메뉴)로 이동 (다른 OS 는 영향 없음)
    file->addAction(QStringLiteral("종료(&X)"), this, &QWidget::close)->setMenuRole(QAction::QuitRole);
    tb->addAction(aNew);
    tb->addAction(aOpen);
    tb->addAction(aSave);
    tb->addSeparator();
    m_editActions << aNew << aOpen;

    // ---- 편집 ----
    QMenu* edit = menuBar()->addMenu(QStringLiteral("편집(&E)"));
    QAction* aDel = edit->addAction(QStringLiteral("삭제"), this, [this] { m_scene->deleteSelection(); });
    aDel->setShortcut(QKeySequence::Delete);
    aDel->setShortcutContext(Qt::WidgetShortcut);
    QAction* aRot = edit->addAction(QStringLiteral("회전 (R)"), this, [this] { m_scene->rotateSelection(); });
    QAction* aDup = edit->addAction(QStringLiteral("복제"), this, [this] { m_scene->duplicateSelection(); });
    aDup->setShortcut(QKeySequence("Ctrl+D"));
    edit->addAction(QStringLiteral("모두 선택"), this, [this] { for (QGraphicsItem* it : m_scene->items()) it->setSelected(true); })->setShortcut(QKeySequence::SelectAll);
    QAction* aClearWires = edit->addAction(QStringLiteral("모든 전선 삭제"), this, [this] {
        if (QMessageBox::question(this, QStringLiteral("전선 삭제"), QStringLiteral("회로의 모든 전선을 삭제할까요?")) != QMessageBox::Yes) return;
        for (WireItem* w : m_scene->wires()) m_scene->removeWire(w);
    });
    m_editActions << aDel << aRot << aDup << aClearWires;

    // ---- 보기 ----
    QMenu* view = menuBar()->addMenu(QStringLiteral("보기(&V)"));
    QAction* aZin = view->addAction(QStringLiteral("확대"), this, [this] { m_view->zoomBy(1.2); });
    aZin->setShortcut(QKeySequence::ZoomIn);
    QAction* aZout = view->addAction(QStringLiteral("축소"), this, [this] { m_view->zoomBy(1 / 1.2); });
    aZout->setShortcut(QKeySequence::ZoomOut);
    QAction* aFit = view->addAction(QStringLiteral("전체 보기"), this, [this] { m_view->zoomToFit(); });
    aFit->setShortcut(QKeySequence("Ctrl+0"));
    view->addSeparator();
    QAction* aGrid = view->addAction(QStringLiteral("격자 표시"));
    aGrid->setCheckable(true);
    aGrid->setChecked(AppSettings::instance().showGrid);
    connect(aGrid, &QAction::toggled, this, [this](bool on) { m_scene->setShowGrid(on); AppSettings::instance().showGrid = on; AppSettings::instance().save(); });
    view->addSeparator();
    for (QDockWidget* d : { m_dockLib, m_dockProps, m_dockCode, m_dockSerial, m_dockLog }) view->addAction(d->toggleViewAction());
    view->addSeparator();
    view->addAction(QStringLiteral("창 배치 초기화"), this, [this] {
        for (QDockWidget* d : { m_dockLib, m_dockProps, m_dockCode, m_dockSerial, m_dockLog }) { d->setFloating(false); d->show(); }
        addDockWidget(Qt::LeftDockWidgetArea, m_dockLib);
        addDockWidget(Qt::LeftDockWidgetArea, m_dockProps);
        splitDockWidget(m_dockLib, m_dockProps, Qt::Vertical);
        addDockWidget(Qt::RightDockWidgetArea, m_dockCode);
        addDockWidget(Qt::BottomDockWidgetArea, m_dockSerial);
        addDockWidget(Qt::BottomDockWidgetArea, m_dockLog);
        tabifyDockWidget(m_dockSerial, m_dockLog);
        resizeDocks({ m_dockLib, m_dockCode }, { 280, 420 }, Qt::Horizontal);
    });
    tb->addAction(aFit);
    tb->addSeparator();

    // ---- 시뮬레이션 ----
    QMenu* sim = menuBar()->addMenu(QStringLiteral("시뮬레이션(&S)"));
    m_actRun = sim->addAction(style()->standardIcon(QStyle::SP_MediaPlay), QStringLiteral("실행"), this, &MainWindow::runSimulation);
    m_actRun->setShortcut(QKeySequence("F5"));
    m_actStop = sim->addAction(style()->standardIcon(QStyle::SP_MediaStop), QStringLiteral("정지"), this, &MainWindow::stopSimulation);
    m_actStop->setShortcut(QKeySequence("Shift+F5"));
    m_actStop->setEnabled(false);
    tb->addAction(m_actRun);
    tb->addAction(m_actStop);
    tb->addSeparator();

    // ---- 도구 ----
    QMenu* tools = menuBar()->addMenu(QStringLiteral("도구(&T)"));
    QAction* aWizard = tools->addAction(QStringLiteral("회로 마법사 (센서 선택 → 자동 배치·배선·코드)..."), this, &MainWindow::runWizard);
    aWizard->setShortcut(QKeySequence("Ctrl+M"));
    QAction* aAuto = tools->addAction(QStringLiteral("자동 배치 + 배선 (전체)"), this, &MainWindow::autoPlaceAndWire);
    aAuto->setShortcut(QKeySequence("Ctrl+Shift+W"));
    QAction* aWire = tools->addAction(QStringLiteral("미연결 부품만 자동 배선"), this, &MainWindow::autoWireUnconnected);
    aWire->setShortcut(QKeySequence("Ctrl+W"));
    QAction* aRes = tools->addAction(QStringLiteral("저항 자동 추천·설정"), this, &MainWindow::autoResistors);
    aRes->setShortcut(QKeySequence("Ctrl+R"));
    QAction* aGen = tools->addAction(QStringLiteral("스케치 코드 자동 생성"), this, [this] { generateCode(true); });
    aGen->setShortcut(QKeySequence("Ctrl+G"));
    tools->addSeparator();
    QAction* aSettings = tools->addAction(QStringLiteral("환경설정..."), this, &MainWindow::showSettings);
    aSettings->setShortcut(QKeySequence("Ctrl+,"));
    aSettings->setMenuRole(QAction::PreferencesRole);
    aWizard->setIconText(QStringLiteral("회로 마법사"));
    aAuto->setIconText(QStringLiteral("자동 배치·배선"));
    aRes->setIconText(QStringLiteral("저항 추천"));
    aGen->setIconText(QStringLiteral("코드 생성"));
    tb->addAction(aWizard);
    tb->addAction(aAuto);
    tb->addAction(aRes);
    tb->addAction(aGen);
    m_editActions << aWizard << aAuto << aWire << aRes << aGen;

    // ---- 예제 ----
    QMenu* exm = menuBar()->addMenu(QStringLiteral("예제(&X)"));
    const auto exs = examples();
    for (int i = 0; i < exs.size(); ++i) {
        QAction* a = exm->addAction(exs[i].title, this, [this, i] { loadExample(i); });
        m_editActions << a;
    }

    // ---- 도움말 ----
    QMenu* help = menuBar()->addMenu(QStringLiteral("도움말(&H)"));
    help->addAction(QStringLiteral("사용법"), this, &MainWindow::showHelp)->setShortcut(QKeySequence::HelpContents);
    QAction* aAbout = help->addAction(QStringLiteral("버전 정보 / 프로그램 정보"), this, [this] {
        QMessageBox box(this);
        box.setWindowTitle(QStringLiteral("버전 정보"));
        box.setIconPixmap(QIcon(QStringLiteral(":/icons/app.png")).pixmap(72, 72));
        box.setTextFormat(Qt::RichText);
        box.setText(QStringLiteral("<h3>아두이노 시뮬레이터</h3>"
                                   "<table cellspacing='4'>"
                                   "<tr><td><b>버전</b></td><td>%1</td></tr>"
                                   "<tr><td><b>빌드 날짜</b></td><td>%2</td></tr>"
                                   "<tr><td><b>Qt</b></td><td>%3 (빌드 %4)</td></tr>"
                                   "<tr><td><b>컴파일러</b></td><td>%5</td></tr>"
                                   "<tr><td><b>운영체제</b></td><td>%7</td></tr>"
                                   "<tr><td><b>설정 파일</b></td><td>%6</td></tr>"
                                   "</table>"
                                   "<p>부품 배치·배선, DC 회로 해석(LED·저항·센서), 아두이노 스케치 인터프리터,<br>"
                                   "자동 배치·배선·저항 추천·코드 생성을 지원합니다.</p>")
                        .arg(QStringLiteral(APP_VERSION_STR), QStringLiteral(APP_BUILD_DATE), QString::fromLatin1(qVersion()),
                             QStringLiteral(QT_VERSION_STR), compilerName(), QDir::toNativeSeparators(AppSettings::iniPath()))
                        .arg(QSysInfo::prettyProductName()));
        box.exec();
    });
    aAbout->setMenuRole(QAction::AboutRole);
}

void MainWindow::applySettings(bool applyTheme)
{
    const AppSettings& s = AppSettings::instance();
    if (applyTheme) {
        Theme::apply(s.theme);
        m_code->applyTheme();
        m_props->refreshTheme();
        m_scene->update();
        m_view->viewport()->update();
    }
    m_code->setEditorFont(s.editorFont, s.editorFontSize);
    m_scene->setShowGrid(s.showGrid);
    m_scene->setSnapToGrid(s.snapToGrid);
    if (s.autoSaveMinutes > 0) m_autoSaveTimer.start(s.autoSaveMinutes * 60 * 1000);
    else m_autoSaveTimer.stop();
    updateRecentMenu();
}

// ---------------------------------------------------------------------------
// 창 위치/크기 저장·복원 (INI)
// ---------------------------------------------------------------------------
void MainWindow::restoreWindowState()
{
    auto s = AppSettings::open();
    QByteArray geo = s->value("MainWindow/Geometry").toByteArray();
    QByteArray st = s->value("MainWindow/State").toByteArray();
    bool restored = !geo.isEmpty() && restoreGeometry(geo);
    if (!restored) {
        QRect avail = QGuiApplication::primaryScreen()->availableGeometry();
        QSize sz(qMin(1600, avail.width() - 40), qMin(960, avail.height() - 40));
        resize(sz);
        move(avail.center() - QPoint(sz.width() / 2, sz.height() / 2));
        resizeDocks({ m_dockLib, m_dockCode }, { 280, 440 }, Qt::Horizontal);
        resizeDocks({ m_dockSerial }, { 190 }, Qt::Vertical);
    } else {
        // 저장된 위치가 현재 연결된 화면 밖이면 주 화면으로 이동
        bool visible = false;
        for (QScreen* sc : QGuiApplication::screens())
            if (sc->availableGeometry().intersects(frameGeometry())) visible = true;
        if (!visible) {
            QRect avail = QGuiApplication::primaryScreen()->availableGeometry();
            move(avail.topLeft() + QPoint(40, 40));
        }
    }
    if (!st.isEmpty()) restoreState(st);
}

void MainWindow::saveWindowState()
{
    auto s = AppSettings::open();
    s->setValue("MainWindow/Geometry", saveGeometry());
    s->setValue("MainWindow/State", saveState());
    s->setValue("MainWindow/Zoom", m_view->zoomLevel());
    s->sync();
}

void MainWindow::startup()
{
    const AppSettings& s = AppSettings::instance();
    bool opened = false;
    if (s.openLastFile && !s.lastFile.isEmpty() && QFileInfo::exists(s.lastFile)) opened = openFile(s.lastFile);
    if (!opened) newCircuit(true);
    auto st = AppSettings::open();
    double z = st->value("MainWindow/Zoom", 0.0).toDouble();
    if (z > 0.1) { m_view->resetTransform(); m_view->scale(z, z); m_view->centerOn(m_scene->itemsBoundingRect().center()); }
    else m_view->zoomToFit();
    log(QStringLiteral("설정 파일: %1").arg(QDir::toNativeSeparators(AppSettings::iniPath())));
    log(QStringLiteral("작업 폴더: %1").arg(QDir::toNativeSeparators(s.effectiveWorkDir())));
}

void MainWindow::selfTest(int example, bool run, const QString& screenshot, int seconds, const QString& wizardIds)
{
    m_selfTest = true;
    m_modified = false;
    if (example >= 0) loadExample(example);
    if (!wizardIds.isEmpty()) {
        m_scene->clearAll();
        PartItem* board = m_scene->addPart(AppSettings::instance().defaultBoard, QPointF(-140, 0));
        double x = board->pos().x();
        for (const QString& id : wizardIds.split(',', Qt::SkipEmptyParts)) {
            PartItem* p = m_scene->addPart(id.trimmed(), QPointF(x, -300));
            if (p) x += p->def()->size.width() + 40;
        }
        AutoWire::Result r = AutoWire::placeAndWireAll(m_scene);
        for (const QString& l : r.log) log(l);
        generateCode(false);
        m_view->zoomToFit();
    }
    setModified(false);
    if (run) runSimulation();
    // 테스트 스크립트가 실제 마우스로 부품을 클릭할 수 있도록 부품의 화면 좌표 기록
    if (!screenshot.isEmpty()) {
        QTimer::singleShot(800, this, [this, screenshot] {
            QFile f(screenshot + ".layout.txt");
            if (!f.open(QIODevice::WriteOnly)) return;
            QTextStream ts(&f);
            for (PartItem* p : m_scene->parts()) {
                QRectF sr = p->sceneBoundingRect();
                QPoint c = m_view->viewport()->mapToGlobal(m_view->mapFromScene(sr.center()));
                QPoint tl = m_view->viewport()->mapToGlobal(m_view->mapFromScene(sr.topLeft()));
                QPoint br = m_view->viewport()->mapToGlobal(m_view->mapFromScene(sr.bottomRight()));
                ts << p->ref() << '\t' << p->def()->id << '\t' << c.x() << '\t' << c.y() << '\t'
                   << tl.x() << '\t' << tl.y() << '\t' << br.x() << '\t' << br.y() << '\n';
            }
        });
    }
    QTimer::singleShot(qMax(1, seconds) * 1000, this, [this, screenshot] {
        if (!screenshot.isEmpty()) {
            grab().save(screenshot);
            QFile f(screenshot + ".txt");
            if (f.open(QIODevice::WriteOnly)) {
                QTextStream ts(&f);
                ts << "=== SERIAL ===\n" << m_serialOut->toPlainText() << "\n=== LOG ===\n";
                for (int i = 0; i < m_log->count(); ++i) ts << m_log->item(i)->text() << "\n";
                ts << "=== CODE ===\n" << m_code->toPlainText();
            }
        }
        m_sim->stop();
        m_modified = false;
        close();
    });
}

void MainWindow::closeEvent(QCloseEvent* e)
{
    if (!m_selfTest && !maybeSave()) { e->ignore(); return; }
    m_sim->stop();
    if (!m_selfTest) {
        saveWindowState();
        AppSettings::instance().save();
    }
    e->accept();
}

// ---------------------------------------------------------------------------
// 파일
// ---------------------------------------------------------------------------
void MainWindow::newCircuit(bool withBoard)
{
    m_sim->stop();
    m_scene->clearAll();
    if (withBoard) {
        QString id = AppSettings::instance().defaultBoard;
        if (!Catalog::instance().find(id)) id = "arduino_uno";
        m_scene->addPart(id, QPointF(-140, 0));
    }
    m_code->setPlainText(QString::fromUtf8(kDefaultSketch));
    m_code->clearErrorLine();
    m_file.clear();
    setModified(false);
    m_view->zoomToFit();
}

bool MainWindow::openFile(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, QStringLiteral("열기 실패"), QStringLiteral("파일을 열 수 없습니다:\n%1").arg(path));
        return false;
    }
    QJsonParseError pe;
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
    if (doc.isNull()) {
        QMessageBox::warning(this, QStringLiteral("열기 실패"), QStringLiteral("파일 형식 오류: %1").arg(pe.errorString()));
        return false;
    }
    m_sim->stop();
    QJsonObject root = doc.object();
    QString err;
    m_scene->fromJson(root, &err);
    if (!err.isEmpty()) log(err, 1);
    m_code->setPlainText(root["code"].toString());
    m_code->clearErrorLine();
    m_file = path;
    AppSettings::instance().addRecentFile(path);
    AppSettings::instance().save();
    updateRecentMenu();
    setModified(false);
    m_view->zoomToFit();
    log(QStringLiteral("열기: %1").arg(QDir::toNativeSeparators(path)));
    return true;
}

bool MainWindow::saveFile(const QString& path)
{
    QJsonObject root = m_scene->toJson();
    root["code"] = m_code->toPlainText();
    root["saved"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    root["appVersion"] = QStringLiteral(APP_VERSION_STR);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::warning(this, QStringLiteral("저장 실패"), QStringLiteral("파일을 저장할 수 없습니다:\n%1").arg(path));
        return false;
    }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    m_file = path;
    AppSettings::instance().addRecentFile(path);
    AppSettings::instance().save();
    updateRecentMenu();
    setModified(false);
    statusBar()->showMessage(QStringLiteral("저장됨: %1").arg(QDir::toNativeSeparators(path)), 4000);
    return true;
}

bool MainWindow::save()
{
    if (m_file.isEmpty()) return saveAs();
    return saveFile(m_file);
}

bool MainWindow::saveAs()
{
    QString dir = AppSettings::instance().effectiveWorkDir();
    QString name = m_file.isEmpty() ? dir + QStringLiteral("/새 회로.asim") : m_file;
    QString f = QFileDialog::getSaveFileName(this, QStringLiteral("다른 이름으로 저장"), name, QStringLiteral("아두이노 시뮬레이터 회로 (*.asim)"));
    if (f.isEmpty()) return false;
    if (!f.endsWith(".asim", Qt::CaseInsensitive)) f += ".asim";
    return saveFile(f);
}

bool MainWindow::maybeSave()
{
    if (!m_modified) return true;
    QMessageBox box(QMessageBox::Question, QStringLiteral("저장 확인"), QStringLiteral("변경 내용을 저장할까요?"), QMessageBox::NoButton, this);
    QPushButton* yes = box.addButton(QStringLiteral("저장"), QMessageBox::AcceptRole);
    QPushButton* no = box.addButton(QStringLiteral("저장 안 함"), QMessageBox::DestructiveRole);
    box.addButton(QStringLiteral("취소"), QMessageBox::RejectRole);
    box.exec();
    if (box.clickedButton() == yes) return save();
    if (box.clickedButton() == no) return true;
    return false;
}

void MainWindow::setModified(bool m)
{
    m_modified = m;
    updateTitle();
}

void MainWindow::updateTitle()
{
    QString name = m_file.isEmpty() ? QStringLiteral("새 회로") : QFileInfo(m_file).fileName();
    setWindowTitle(name + (m_modified ? "*" : ""));   // " - 아두이노 시뮬레이터 vX.Y.Z" 는 Qt 가 붙임
}

void MainWindow::updateRecentMenu()
{
    m_recentMenu->clear();
    const QStringList files = AppSettings::instance().recentFiles;
    for (const QString& f : files) {
        m_recentMenu->addAction(QDir::toNativeSeparators(f), this, [this, f] { if (maybeSave()) openFile(f); });
    }
    m_recentMenu->setEnabled(!files.isEmpty());
}

void MainWindow::autoSave()
{
    if (!m_modified || m_sim->isRunning()) return;
    if (!m_file.isEmpty()) { saveFile(m_file); return; }
    QString path = AppSettings::instance().effectiveWorkDir() + "/autosave.asim";
    QJsonObject root = m_scene->toJson();
    root["code"] = m_code->toPlainText();
    QFile f(path);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(QJsonDocument(root).toJson());
        statusBar()->showMessage(QStringLiteral("자동 저장: %1").arg(QDir::toNativeSeparators(path)), 3000);
    }
}

// ---------------------------------------------------------------------------
// 시뮬레이션
// ---------------------------------------------------------------------------
void MainWindow::runSimulation()
{
    if (m_sim->isRunning()) return;
    m_scene->cancelWiring();
    m_code->clearErrorLine();
    QString err;
    int line = 0;
    if (!m_serialOut->toPlainText().isEmpty()) appendSerial(QStringLiteral("\n───────── 재시작 ─────────\n"));
    if (!m_sim->start(m_code->toPlainText(), err, line)) {
        if (line > 0) {
            m_code->markErrorLine(line, err);
            log(QStringLiteral("컴파일 오류 (줄 %1): %2").arg(line).arg(err), 2);
        } else {
            log(err, 2);
        }
        m_dockLog->raise();
        if (!m_selfTest)
            QMessageBox::warning(this, QStringLiteral("시뮬레이션 시작 실패"), line > 0 ? QStringLiteral("줄 %1: %2").arg(line).arg(err) : err);
        return;
    }
    m_dockSerial->raise();
    updateSimUi();
}

void MainWindow::stopSimulation()
{
    m_sim->stop();
    updateSimUi();
}

void MainWindow::updateSimUi()
{
    bool run = m_sim->isRunning();
    m_actRun->setEnabled(!run);
    m_actStop->setEnabled(run);
    for (QAction* a : m_editActions) a->setEnabled(!run);
    m_code->setReadOnly(run);
    m_lib->setEnabled(!run);
    m_props->setSimulating(run);
}

void MainWindow::appendSerial(const QString& text)
{
    QScrollBar* sb = m_serialOut->verticalScrollBar();
    int keep = sb->value();
    QTextCursor c(m_serialOut->document());
    c.movePosition(QTextCursor::End);
    QString t = text;
    t.replace("\r\n", "\n");
    t.replace('\r', '\n');
    c.insertText(t);
    if (m_autoScroll->isChecked()) sb->setValue(sb->maximum());
    else sb->setValue(keep);
}

void MainWindow::sendSerialLine()
{
    QString t = m_serialIn->text();
    if (!m_sim->isRunning()) { statusBar()->showMessage(QStringLiteral("시뮬레이션 실행 중에만 전송할 수 있습니다."), 3000); return; }
    QByteArray data = (t + m_lineEnding->currentData().toString()).toUtf8();
    m_sim->sendSerial(data);
    m_serialIn->clear();
}

void MainWindow::log(const QString& text, int level)
{
    QString stamp = QTime::currentTime().toString("HH:mm:ss");
    auto* it = new QListWidgetItem(QString("[%1] %2").arg(stamp, text));
    if (level == 1) { it->setIcon(style()->standardIcon(QStyle::SP_MessageBoxWarning)); it->setForeground(QColor(170, 100, 0)); }
    else if (level == 2) { it->setIcon(style()->standardIcon(QStyle::SP_MessageBoxCritical)); it->setForeground(QColor(200, 0, 0)); }
    else it->setIcon(style()->standardIcon(QStyle::SP_MessageBoxInformation));
    m_log->addItem(it);
    while (m_log->count() > 1000) delete m_log->takeItem(0);
    m_log->scrollToBottom();
    if (level >= 1) statusBar()->showMessage(text, 8000);
}

// ---------------------------------------------------------------------------
// 도구
// ---------------------------------------------------------------------------
void MainWindow::addPartAtCenter(const QString& id)
{
    if (m_sim->isRunning()) return;
    const PartDef* d = Catalog::instance().find(id);
    if (!d) return;
    QPointF c = m_view->mapToScene(m_view->viewport()->rect().center());
    // 겹치지 않게 약간씩 비켜서 배치
    QPointF pos = c - QPointF(d->size.width() / 2, d->size.height() / 2);
    for (int k = 0; k < 20; ++k) {
        bool overlap = false;
        for (PartItem* p : m_scene->parts())
            if (QRectF(p->pos(), p->def()->size).intersects(QRectF(pos, d->size))) overlap = true;
        if (!overlap) break;
        pos += QPointF(30, 30);
    }
    PartItem* p = m_scene->addPart(id, pos);
    if (p) {
        m_scene->clearSelection();
        p->setSelected(true);
        statusBar()->showMessage(QStringLiteral("%1 (%2) 추가 — 속성 창에서 규격을 선택하세요").arg(p->ref(), d->name), 5000);
    }
}

void MainWindow::autoPlaceAndWire()
{
    if (m_sim->isRunning()) return;
    AutoWire::Result r = AutoWire::placeAndWireAll(m_scene);
    log(QStringLiteral("자동 배치·배선: 전선 %1개, 저항 %2개 추가").arg(r.wires).arg(r.resistors));
    for (const QString& l : r.log) log("  " + l);
    if (AppSettings::instance().generateCodeOnAutoWire && r.wires > 0) generateCode(false);
    m_view->zoomToFit();
    m_dockLog->raise();
}

void MainWindow::autoWireUnconnected()
{
    if (m_sim->isRunning()) return;
    AutoWire::Result r = AutoWire::wireUnconnected(m_scene);
    log(QStringLiteral("미연결 부품 자동 배선: 전선 %1개, 저항 %2개 추가").arg(r.wires).arg(r.resistors));
    for (const QString& l : r.log) log("  " + l);
    if (AppSettings::instance().generateCodeOnAutoWire && r.wires > 0) generateCode(true);
    m_dockLog->raise();
}

void MainWindow::autoResistors()
{
    if (m_sim->isRunning()) return;
    QStringList rep;
    int n = ResistorAdvisor::applyAll(m_scene, &rep);
    log(QStringLiteral("저항 자동 추천·설정: %1개 변경/추가").arg(n));
    for (const QString& l : rep) log("  " + l);
    if (rep.isEmpty()) log(QStringLiteral("  회로에 저항이나 LED 가 없습니다."));
    m_props->setPart(m_props->part());
    m_dockLog->raise();
    QMessageBox::information(this, QStringLiteral("저항 자동 추천·설정"),
                             rep.isEmpty() ? QStringLiteral("추천할 저항이 없습니다.")
                                           : QStringLiteral("%1개 저항을 설정/추가했습니다.\n\n").arg(n) + rep.mid(0, 12).join("\n\n"));
}

void MainWindow::generateCode(bool ask)
{
    if (ask && !m_code->toPlainText().trimmed().isEmpty() && m_code->toPlainText() != QString::fromUtf8(kDefaultSketch)) {
        if (QMessageBox::question(this, QStringLiteral("코드 자동 생성"), QStringLiteral("현재 스케치를 회로에 맞게 자동 생성된 코드로 바꿀까요?")) != QMessageBox::Yes) return;
    }
    QStringList notes;
    QString code = CodeGen::generate(m_scene, &notes);
    m_code->setPlainText(code);
    log(QStringLiteral("스케치 코드를 자동 생성했습니다."));
    for (const QString& n : notes) log("  " + n, 1);
}

void MainWindow::runWizard()
{
    if (m_sim->isRunning()) return;
    WizardDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted) return;
    QVector<WizardItem> items = dlg.items();
    if (items.isEmpty()) { QMessageBox::information(this, QStringLiteral("회로 마법사"), QStringLiteral("선택한 부품이 없습니다.")); return; }
    if (dlg.clearExisting()) {
        if (!maybeSave()) return;
        m_scene->clearAll();
        m_file.clear();
    }
    PartItem* board = m_scene->board();
    if (board && board->def()->id != dlg.boardId() && dlg.clearExisting()) { m_scene->removePart(board); board = nullptr; }
    if (!board) board = m_scene->addPart(dlg.boardId(), QPointF(-140, 0));
    double x = board->pos().x();
    for (const WizardItem& wi : items) {
        for (int k = 0; k < wi.count; ++k) {
            PartItem* p = m_scene->addPart(wi.id, QPointF(x, board->pos().y() - 300));
            if (!p) continue;
            if (!wi.paramKey.isEmpty()) p->setParam(wi.paramKey, wi.paramValue);
            x += p->def()->size.width() + 40;
        }
    }
    AutoWire::Result r = AutoWire::placeAndWireAll(m_scene);
    log(QStringLiteral("회로 마법사: 부품 %1종 배치, 전선 %2개, 저항 %3개").arg(items.size()).arg(r.wires).arg(r.resistors));
    for (const QString& l : r.log) log("  " + l);
    generateCode(false);
    m_view->zoomToFit();
    m_dockLog->raise();
}

void MainWindow::loadExample(int index)
{
    const auto exs = examples();
    if (index < 0 || index >= exs.size() || m_sim->isRunning()) return;
    if (!maybeSave()) return;
    const Example& ex = exs[index];
    m_scene->clearAll();
    QString bid = AppSettings::instance().defaultBoard;
    if (!Catalog::instance().find(bid)) bid = "arduino_uno";
    PartItem* board = m_scene->addPart(bid, QPointF(-140, 0));
    double x = board->pos().x();
    for (const QString& id : ex.parts) {
        PartItem* p = m_scene->addPart(id, QPointF(x, -300));
        if (p) x += p->def()->size.width() + 40;
    }
    AutoWire::Result r = AutoWire::placeAndWireAll(m_scene);
    if (ex.code.isEmpty()) m_code->setPlainText(CodeGen::generate(m_scene));
    else m_code->setPlainText(CodeGen::resolvePinTokens(m_scene, ex.code));
    m_code->clearErrorLine();
    m_file.clear();
    setModified(false);
    setWindowTitle(QStringLiteral("예제: %1").arg(ex.title));
    log(QStringLiteral("예제 불러오기: %1 (전선 %2개, 저항 %3개)").arg(ex.title).arg(r.wires).arg(r.resistors));
    m_view->zoomToFit();
}

void MainWindow::showSettings()
{
    SettingsDialog dlg(this);
    if (dlg.exec() == QDialog::Accepted) {
        applySettings(true);
        m_lib->reload();
        log(QStringLiteral("환경설정을 저장했습니다: %1").arg(QDir::toNativeSeparators(AppSettings::iniPath())));
    }
}

void MainWindow::showHelp()
{
    QMessageBox box(this);
    box.setWindowTitle(QStringLiteral("사용법"));
    box.setTextFormat(Qt::RichText);
    box.setText(QStringLiteral(
        "<h3>아두이노 시뮬레이터 사용법</h3>"
        "<b>1. 부품 배치</b><br>왼쪽 <i>부품 목록</i>에서 더블클릭하거나 회로로 끌어다 놓습니다. 배치 후 <i>속성</i> 창에서 모델·색상·저항값 등 규격을 고릅니다.<br><br>"
        "<b>2. 배선</b><br>핀(작은 동그라미)을 클릭 → 연결할 핀을 클릭. 중간 빈 곳을 클릭하면 꺾임점이 생깁니다. 우클릭/ESC 로 취소.<br>"
        "전선을 우클릭하면 색상 변경·삭제. 부품은 R 로 회전, Del 로 삭제.<br><br>"
        "<b>3. 자동 기능</b> (도구 메뉴)<br>"
        "• <b>회로 마법사</b>: 센서를 고르면 배치·핀 할당·배선·저항 삽입·코드 생성까지 자동<br>"
        "• <b>자동 배치 + 배선</b>: 올려둔 부품을 보드 위에 정렬하고 자동 연결<br>"
        "• <b>저항 자동 추천·설정</b>: LED·센서·스위치·트랜지스터에 맞는 저항값 계산/적용, 빠진 LED 저항 삽입<br>"
        "• <b>코드 자동 생성</b>: 현재 연결 상태에 맞는 스케치 작성<br><br>"
        "<b>4. 시뮬레이션</b><br>F5 로 실행. 버튼·스위치·PIR 등은 부품을 클릭해서 조작하고, 센서값(온도·밝기·거리...)은 속성 창 슬라이더나 마우스 휠로 조절합니다.<br>"
        "핀에 마우스를 올리면 전압·전류가 표시됩니다. 과전류·LED 소손·전원 미연결 등은 ⚠ 표시와 메시지 창으로 알려줍니다.<br><br>"
        "<b>5. 설정</b><br>도구 → 환경설정에서 작업 폴더, 저항 추천 기준, 사용자 센서 추가 등을 설정하며 모든 설정과 창 위치·크기는 INI 파일에 저장됩니다."));
    box.exec();
}
