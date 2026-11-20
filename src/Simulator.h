#pragma once
#include "ArduinoHost.h"
#include "CircuitSolver.h"
#include "Netlist.h"

#include <QHash>
#include <QMutex>
#include <QObject>
#include <QSet>
#include <QStringDecoder>
#include <QTimer>
#include <QVariantMap>
#include <atomic>
#include <chrono>
#include <deque>
#include <memory>
#include <thread>

class CircuitScene;
class PartItem;
struct PartDef;
namespace ard { class SketchInterpreter; }

class Simulator : public QObject, public ard::ArduinoHost
{
    Q_OBJECT
public:
    explicit Simulator(CircuitScene* scene, QObject* parent = nullptr);
    ~Simulator() override;

    // 회로 + 코드로 시뮬레이션 시작. 실패 시 err/errLine
    bool start(const QString& code, QString& err, int& errLine);
    void stop();
    bool isRunning() const { return m_running; }
    void sendSerial(const QByteArray& data);
    // GUI 툴팁용 핀 정보 (GUI 스레드)
    QString pinInfo(PartItem* part, int pin) const;
    double elapsedSeconds() const;

signals:
    void serialOutput(const QString& text);
    void logMessage(const QString& text, int level);   // 0 정보, 1 경고, 2 오류
    void runtimeError(const QString& msg, int line);
    void stopped();

public slots:
    void onPartParamChanged(PartItem* part, const QString& key);

private slots:
    void onTick();
    void onThreadFinished(bool ok, const QString& err, int line);

public:
    // ===== ard::ArduinoHost (인터프리터 스레드) =====
    void pinMode(int pin, int mode) override;
    void digitalWrite(int pin, int value) override;
    int digitalRead(int pin) override;
    int analogRead(int pin) override;
    void analogWrite(int pin, int value) override;
    uint64_t micros() override;
    void delayMicros(uint64_t us) override;
    bool stopRequested() override { return m_stop.load(); }
    void onLoopIteration() override;
    void serialBegin(long baud) override;
    void serialWrite(const std::string& bytes) override;
    int serialAvailable() override;
    int serialRead() override;
    int serialPeek() override;
    void tone(int pin, unsigned int freq, unsigned long durationMs) override;
    void noTone(int pin) override;
    unsigned long pulseIn(int pin, int state, unsigned long timeoutUs) override;
    int digitalPinToInterrupt(int pin) override;
    void attachInterrupt(int irq, int mode) override;
    void detachInterrupt(int irq) override;
    int pollInterrupt() override;
    bool lookupConstant(const std::string& name, double& value) override;
    bool isLibraryClass(const std::string& className) override;
    bool isGlobalObject(const std::string& name) override;
    void libraryConstruct(const std::string& className, int instanceId, const std::vector<ard::LibValue>& args) override;
    bool libraryCall(const std::string& className, int instanceId, const std::string& method,
                     const std::vector<ard::LibValue>& args, ard::LibValue& result, std::string& errorOut) override;

private:
    struct PinState {
        int mode = 0;          // INPUT/OUTPUT/INPUT_PULLUP
        int out = 0;           // 출력 레벨
        int pwm = -1;          // analogWrite 값 (-1 = 디지털)
        double toneHz = 0.0;
        uint64_t toneEndUs = 0;
        int lastRead = 0;
        uint64_t lastFallUs = 0;
        bool exists = false;
        int adc = -1;
        int irq = -1;
        bool pwmCapable = false;
    };
    struct SimPart {
        PartItem* item = nullptr;      // GUI 전용
        const PartDef* def = nullptr;
        QString id, ref;
        QVariantMap params;
        QVector<int> net;
        // 라이브러리 상태
        double servoAngle = 90.0;
        bool servoAttached = false;
        QVector<QString> lcd;
        int lcdCol = 0, lcdRow = 0;
        bool lcdBacklight = true, lcdInit = false, lcdDisplay = true;
        QVector<quint32> pix, pixShown;
        int pixBrightness = 255;
        long stepPos = 0;
        int stepsPerRev = 2048;
        QSet<int> burned;
        double p(const char* key, double def = 0.0) const;
        QString s(const char* key) const { return params.value(QString::fromLatin1(key)).toString(); }
        bool b(const char* key) const { return params.value(QString::fromLatin1(key)).toBool(); }
    };
    struct LibInst {
        QString cls;
        std::vector<ard::LibValue> args;
        int part = -1;
        int servoPin = -1;
        int stepSpeedRpm = 10;
        int wireAddr = -1;
    };

    // ---- 회로 해석 (m_mutex 보유 상태에서 호출) ----
    enum class PwmMode { Average, Slice };
    void buildElements(std::vector<SElem>& el, const QHash<int, bool>* sliceHigh);
    bool solveForRead();                       // 읽기용 (PWM 평균) 캐시 갱신
    double netVoltage(int net);                // 읽기용
    double supplyVoltageOfNet(int net) const;
    bool partPowered(const SimPart& sp, double* vcc = nullptr) const;
    int pinNet(const SimPart& sp, const QString& pinName) const;
    int netOfArdPin(int ardPin) const;
    int resolveAnalogPin(int pin) const;
    int findPartOnNet(const QString& idPrefix, const QString& pinName, int net) const;
    void checkInterrupts();
    void bumpVersion() { ++m_version; }
    void logOnce(const QString& key, const QString& msg, int level);
    QString formatLibValue(const std::vector<ard::LibValue>& args) const;
    void lcdWrite(SimPart& sp, const QString& text);
    int bindLibrary(LibInst& li);

    // ---- GUI 표시 ----
    void computeDisplay();

    CircuitScene* m_scene;
    Netlist m_nl;
    QVector<SimPart> m_parts;
    int m_board = -1;
    QString m_boardId;
    QHash<int, PinState> m_pins;               // 아두이노 핀 번호 → 상태
    QHash<int, int> m_ardNet;                  // 아두이노 핀 번호 → 넷
    QHash<int, int> m_adcToPin;                // 아날로그 채널 → 핀 번호
    QSet<int> m_net5V, m_net3V3, m_netVin;
    QHash<int, LibInst> m_libs;
    QHash<int, int> m_irqPin;                  // irq → 핀
    QHash<int, int> m_irqMode;
    QHash<int, int> m_irqLast;
    std::atomic<unsigned> m_irqPending{ 0 };

    mutable QMutex m_mutex;
    std::atomic<quint64> m_version{ 1 };
    quint64 m_readVersion = 0;
    std::vector<SElem> m_readElems;
    std::vector<double> m_readVolts;
    std::vector<bool> m_readFloating;

    // 표시용 (GUI 스레드)
    std::vector<double> m_dispVolts;
    QHash<int, QString> m_partWarn;
    QSet<QString> m_logged;
    QHash<int, double> m_pinCurrent;           // 핀 번호 → 최대 전류(A)

    // 시리얼
    QByteArray m_serialOut;
    std::deque<char> m_serialIn;
    QStringDecoder m_decoder{ QStringDecoder::Utf8 };
    uint64_t m_lastTxUs = 0, m_lastRxUs = 0;
    long m_baud = 0;

    // 실행 상태
    std::unique_ptr<ard::SketchInterpreter> m_interp;
    std::thread m_thread;
    std::atomic<bool> m_stop{ false };
    bool m_running = false;
    std::chrono::steady_clock::time_point m_t0;
    QTimer m_timer;
    unsigned m_loopCount = 0;
    QVector<QPair<QString, int>> m_pendingLogs;  // 인터프리터 스레드에서 쌓인 로그
};
