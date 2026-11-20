#include "Simulator.h"
#include "AppSettings.h"
#include "Catalog.h"
#include "CircuitScene.h"
#include "PartItem.h"
#include "SketchInterpreter.h"
#include "Units.h"
#include "WireItem.h"

#include <QMetaObject>
#include <QMutexLocker>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>

#ifdef _WIN32
#include <windows.h>
#include <timeapi.h>
#endif

using ard::LibValue;

namespace {
const double kVcc = 5.0;
const double kPinR = 25.0;          // 출력 핀 내부저항
const double kPullupR = 35000.0;    // 내부 풀업
inline double clampD(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }
QString pinLabel(int ardPin, const QString& boardId)
{
    int aBase = boardId == "arduino_mega" ? 54 : 14;
    int aCount = boardId == "arduino_mega" ? 16 : 8;
    if (ardPin >= aBase && ardPin < aBase + aCount) return QString("A%1").arg(ardPin - aBase);
    return QString("D%1").arg(ardPin);
}
} // namespace

double Simulator::SimPart::p(const char* key, double def) const
{
    auto it = params.find(QString::fromLatin1(key));
    if (it == params.end()) return def;
    bool ok = false;
    double v = it.value().toDouble(&ok);
    return ok ? v : def;
}

Simulator::Simulator(CircuitScene* scene, QObject* parent)
    : QObject(parent), m_scene(scene)
{
    m_timer.setInterval(33);
    connect(&m_timer, &QTimer::timeout, this, &Simulator::onTick);
    connect(scene, &CircuitScene::partParamChanged, this, &Simulator::onPartParamChanged);
}

Simulator::~Simulator()
{
    stop();
}

double Simulator::elapsedSeconds() const
{
    if (!m_running) return 0.0;
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - m_t0).count();
}

// ===========================================================================
// 시작 / 정지
// ===========================================================================
bool Simulator::start(const QString& code, QString& err, int& errLine)
{
    stop();
    errLine = 0;
    m_nl = Netlist::build(m_scene->parts(), m_scene->wires());
    if (m_nl.boardIndex < 0) {
        err = QStringLiteral("아두이노 보드가 없습니다. 부품 목록에서 보드를 먼저 배치하세요.");
        return false;
    }
    m_parts.clear();
    for (int i = 0; i < m_nl.parts.size(); ++i) {
        SimPart sp;
        sp.item = m_nl.parts[i];
        sp.def = sp.item->def();
        sp.id = sp.def->id;
        sp.ref = sp.item->ref();
        sp.params = sp.item->params();
        sp.net = m_nl.pinNet[i];
        m_parts.push_back(sp);
    }
    m_board = m_nl.boardIndex;
    m_boardId = m_parts[m_board].id;

    m_pins.clear(); m_ardNet.clear(); m_adcToPin.clear(); m_net5V.clear(); m_net3V3.clear(); m_netVin.clear();
    m_irqPin.clear(); m_irqMode.clear(); m_irqLast.clear(); m_irqPending = 0;
    const PartDef* bd = m_parts[m_board].def;
    for (int i = 0; i < bd->pins.size(); ++i) {
        const PinDef& pd = bd->pins[i];
        int net = m_parts[m_board].net[i];
        switch (pd.bkind) {
        case BoardPinKind::P5V: case BoardPinKind::IoRef: m_net5V.insert(net); break;
        case BoardPinKind::P3V3: m_net3V3.insert(net); break;
        case BoardPinKind::Vin: m_netVin.insert(net); break;
        default: break;
        }
        if (pd.ardPin >= 0) {
            if (!m_ardNet.contains(pd.ardPin)) m_ardNet[pd.ardPin] = net;
            PinState& ps = m_pins[pd.ardPin];
            ps.exists = true;
            if (pd.adc >= 0) { ps.adc = pd.adc; m_adcToPin[pd.adc] = pd.ardPin; }
            if (pd.irq >= 0) { ps.irq = pd.irq; m_irqPin[pd.irq] = pd.ardPin; }
            if (pd.pwm) ps.pwmCapable = true;
        }
    }
    m_libs.clear();
    m_serialOut.clear();
    m_serialIn.clear();
    m_decoder.resetState();
    m_partWarn.clear();
    m_logged.clear();
    m_pinCurrent.clear();
    m_pendingLogs.clear();
    m_baud = 0;
    m_version = 1;
    m_readVersion = 0;
    m_loopCount = 0;

    if (m_net5V.contains(0)) logOnce("short5v", QStringLiteral("5V 와 GND 가 직접 연결(단락)되어 있습니다! 실제 보드라면 USB 전원이 차단되거나 손상됩니다."), 2);

    m_interp = std::make_unique<ard::SketchInterpreter>(this);
    std::string e;
    int line = 0;
    if (!m_interp->compile(code.toStdString(), e, line)) {
        err = QString::fromStdString(e);
        errLine = line;
        m_interp.reset();
        return false;
    }

#ifdef _WIN32
    timeBeginPeriod(1);
#endif
    m_stop = false;
    m_running = true;
    m_t0 = std::chrono::steady_clock::now();
    m_scene->setSimulating(true);
    m_scene->setPinInfoProvider([this](PartItem* p, int pin) { return pinInfo(p, pin); });

    ard::SketchInterpreter* interp = m_interp.get();
    m_thread = std::thread([this, interp]() {
        std::string runErr;
        int runLine = 0;
        bool ok = interp->run(runErr, runLine);
        QString qerr = QString::fromStdString(runErr);
        QMetaObject::invokeMethod(this, [this, ok, qerr, runLine]() { onThreadFinished(ok, qerr, runLine); }, Qt::QueuedConnection);
    });
    m_timer.start();
    emit logMessage(QStringLiteral("시뮬레이션 시작 (%1, 부품 %2개, 넷 %3개)").arg(bd->name).arg(m_parts.size()).arg(m_nl.netCount), 0);
    return true;
}

void Simulator::stop()
{
    if (!m_running && !m_thread.joinable()) return;
    m_stop = true;
    if (m_thread.joinable()) m_thread.join();
    m_timer.stop();
    m_interp.reset();
    bool wasRunning = m_running;
    m_running = false;
#ifdef _WIN32
    if (wasRunning) timeEndPeriod(1);
#endif
    // 남은 시리얼 출력
    if (!m_serialOut.isEmpty()) {
        emit serialOutput(m_decoder.decode(m_serialOut));
        m_serialOut.clear();
    }
    m_scene->setPinInfoProvider(nullptr);
    m_scene->setSimulating(false);
    for (PartItem* p : m_scene->parts()) p->setSimState({});
    if (wasRunning) {
        emit logMessage(QStringLiteral("시뮬레이션 정지"), 0);
        emit stopped();
    }
}

void Simulator::onThreadFinished(bool ok, const QString& err, int line)
{
    if (!m_running) return;
    if (!ok) {
        emit runtimeError(err, line);
        emit logMessage(QStringLiteral("실행 오류 (줄 %1): %2").arg(line).arg(err), 2);
        stop();
    }
}

void Simulator::sendSerial(const QByteArray& data)
{
    QMutexLocker lock(&m_mutex);
    for (char c : data) m_serialIn.push_back(c);
    m_lastRxUs = micros();
}

void Simulator::onPartParamChanged(PartItem* part, const QString& key)
{
    if (!m_running) return;
    int idx = m_nl.index.value(part, -1);
    if (idx < 0) return;
    QMutexLocker lock(&m_mutex);
    m_parts[idx].params = part->params();
    Q_UNUSED(key);
    bumpVersion();
    checkInterrupts();
}

void Simulator::logOnce(const QString& key, const QString& msg, int level)
{
    // m_mutex 보유 여부와 관계없이 사용 가능하도록 큐에만 쌓는다
    if (m_logged.contains(key)) return;
    m_logged.insert(key);
    m_pendingLogs.push_back({ msg, level });
}

// ===========================================================================
// 넷 / 전원 도우미
// ===========================================================================
int Simulator::pinNet(const SimPart& sp, const QString& pinName) const
{
    int i = sp.def->pinIndex(pinName);
    return i >= 0 ? sp.net[i] : -1;
}

int Simulator::netOfArdPin(int ardPin) const
{
    return m_ardNet.value(ardPin, -1);
}

double Simulator::supplyVoltageOfNet(int net) const
{
    if (net < 0) return 0.0;
    if (m_net5V.contains(net)) return 5.0;
    if (m_net3V3.contains(net)) return 3.3;
    if (m_netVin.contains(net)) return 4.6;
    for (auto it = m_pins.begin(); it != m_pins.end(); ++it) {
        const PinState& ps = it.value();
        if (ps.mode == ard::MODE_OUTPUT && (ps.out || ps.pwm > 0) && netOfArdPin(it.key()) == net) return 5.0;
    }
    return 0.0;
}

bool Simulator::partPowered(const SimPart& sp, double* vccOut) const
{
    double vcc = 0.0;
    bool hasVcc = false, gndOk = true, hasGnd = false;
    for (int i = 0; i < sp.def->pins.size(); ++i) {
        const PinDef& pd = sp.def->pins[i];
        if (pd.role == PinRole::Vcc) {
            // LCD 병렬의 백라이트 A 핀 등은 제외하고 첫 Vcc 를 기준으로
            if (!hasVcc) { vcc = supplyVoltageOfNet(sp.net[i]); hasVcc = true; }
        } else if (pd.role == PinRole::Gnd && pd.name != "V0" && pd.name != "RW" && pd.name != "K" && pd.name != "COM") {
            hasGnd = true;
            if (sp.net[i] != 0) gndOk = false;
        }
    }
    if (vccOut) *vccOut = vcc;
    if (!hasVcc) return true;
    return vcc >= 2.7 && (!hasGnd || gndOk);
}

int Simulator::resolveAnalogPin(int pin) const
{
    int aCount = m_adcToPin.size();
    if (pin >= 0 && pin < aCount && m_adcToPin.contains(pin)) return m_adcToPin.value(pin);
    auto it = m_pins.find(pin);
    if (it != m_pins.end() && it.value().adc >= 0) return pin;
    return -1;
}

int Simulator::findPartOnNet(const QString& idPrefix, const QString& pinName, int net) const
{
    if (net < 0) return -1;
    for (int i = 0; i < m_parts.size(); ++i) {
        if (!m_parts[i].id.startsWith(idPrefix)) continue;
        if (pinNet(m_parts[i], pinName) == net) return i;
    }
    return -1;
}

// ===========================================================================
// 회로 소자 생성
// ===========================================================================
void Simulator::buildElements(std::vector<SElem>& el, const QHash<int, bool>* sliceHigh)
{
    el.clear();
    auto add = [&](SElem::Kind k, int a, int b, double r, double v, int owner, int tag) -> int {
        if (a < 0 || b < 0) return -1;
        SElem e; e.kind = k; e.a = a; e.b = b; e.r = r; e.v = v; e.owner = owner; e.tag = tag;
        el.push_back(e);
        return (int)el.size() - 1;
    };
    const uint64_t now = micros();

    // ---- 보드 ----
    for (auto it = m_pins.begin(); it != m_pins.end(); ++it) {
        const int pin = it.key();
        const PinState& ps = it.value();
        int net = netOfArdPin(pin);
        if (net < 0) continue;
        bool toneOn = ps.toneHz > 0 && (ps.toneEndUs == 0 || now < ps.toneEndUs);
        double duty = -1.0;
        if (toneOn) duty = 0.5;
        else if (ps.pwm > 0 && ps.pwm < 255) duty = ps.pwm / 255.0;
        if (ps.mode == ard::MODE_OUTPUT || duty >= 0) {
            double v;
            if (duty >= 0) {
                if (sliceHigh) v = sliceHigh->value(pin, false) ? kVcc : 0.0;
                else v = kVcc * duty;
            } else {
                v = ps.out ? kVcc : 0.0;
            }
            add(SElem::Source, net, 0, kPinR, v, m_board, pin);
        } else if (ps.mode == ard::MODE_INPUT_PULLUP || (ps.mode == ard::MODE_INPUT && ps.out)) {
            add(SElem::Source, net, 0, kPullupR, kVcc, m_board, 1000 + pin);
        }
    }
    for (int n : m_net5V) if (n > 0) add(SElem::Source, n, 0, 0.05, 5.0, m_board, -5);
    for (int n : m_net3V3) if (n > 0 && !m_net5V.contains(n)) add(SElem::Source, n, 0, 0.5, 3.3, m_board, -33);
    for (int n : m_netVin) if (n > 0 && !m_net5V.contains(n)) add(SElem::Source, n, 0, 0.5, 4.6, m_board, -9);

    // ---- 부품 ----
    for (int i = 0; i < m_parts.size(); ++i) {
        SimPart& sp = m_parts[i];
        if (i == m_board) continue;
        const QString& id = sp.id;
        auto N = [&](const char* name) { return pinNet(sp, QString::fromLatin1(name)); };
        double vcc = 0.0;
        bool powered = partPowered(sp, &vcc);

        if (id == "resistor") {
            add(SElem::Resistor, N("1"), N("2"), std::max(0.01, sp.p("R", 220)), 0, i, 0);
        } else if (id == "potentiometer") {
            double pos = clampD(sp.p("pos", 50) / 100.0, 0, 1);
            if (sp.s("taper") == "A") pos = (std::pow(10.0, 2.0 * pos) - 1.0) / 99.0;
            double R = sp.p("R", 10000);
            add(SElem::Resistor, N("1"), N("W"), std::max(0.5, R * (1.0 - pos)), 0, i, 0);
            add(SElem::Resistor, N("W"), N("3"), std::max(0.5, R * pos), 0, i, 1);
        } else if (id == "ldr") {
            double lux = std::max(0.01, sp.p("lux", 300));
            double R = sp.p("R10", 15000) * std::pow(lux / 10.0, -sp.p("gamma", 0.7));
            R = clampD(R, 30.0, sp.p("Rdark", 1e6));
            add(SElem::Resistor, N("1"), N("2"), R, 0, i, 0);
        } else if (id == "ntc") {
            double T = sp.p("temp", 25) + 273.15;
            double R = sp.p("R25", 10000) * std::exp(sp.p("B", 3950) * (1.0 / T - 1.0 / 298.15));
            add(SElem::Resistor, N("1"), N("2"), R, 0, i, 0);
        } else if (id == "diode") {
            int k = add(SElem::Diode, N("A"), N("K"), 0.3, sp.p("Vf", 0.9) - 0.03, i, 0);
            if (k >= 0) el[k].vz = sp.p("Vz", 0);
        } else if (id == "led") {
            if (!sp.burned.contains(0)) add(SElem::Diode, N("A"), N("K"), 10.0, sp.p("Vf", 2.0) - 0.2, i, 0);
        } else if (id == "rgb_led") {
            bool ca = sp.s("type") == "CA";
            const char* names[3] = { "R", "G", "B" };
            const char* vfk[3] = { "VfR", "VfG", "VfB" };
            int com = N("COM");
            for (int c = 0; c < 3; ++c) {
                if (sp.burned.contains(c)) continue;
                int pn = N(names[c]);
                add(SElem::Diode, ca ? com : pn, ca ? pn : com, 10.0, sp.p(vfk[c], 2.5) - 0.2, i, c);
            }
        } else if (id == "seven_seg") {
            bool ca = sp.s("type") == "CA";
            const char* segs[8] = { "A", "B", "C", "D", "E", "F", "G", "DP" };
            int com = N("COM");
            for (int s = 0; s < 8; ++s) {
                if (sp.burned.contains(s)) continue;
                int pn = N(segs[s]);
                add(SElem::Diode, ca ? com : pn, ca ? pn : com, 10.0, sp.p("Vf", 2.0) - 0.2, i, s);
            }
        } else if (id == "npn") {
            int be = add(SElem::Diode, N("B"), N("E"), 20.0, sp.p("Vbe", 0.7), i, 0);
            double ic = std::max(0.001, sp.p("Ic", 800) / 1000.0);
            int ce = add(SElem::ISwitch, N("C"), N("E"), std::max(0.05, sp.p("Vce", 0.2) / ic), 0, i, 1);
            if (ce >= 0) { el[ce].ctrlElem = be; el[ce].ith = 5e-6; }
        } else if (id == "mosfet") {
            int k = add(SElem::VSwitch, N("D"), N("S"), std::max(0.001, sp.p("Rds", 0.05)), sp.p("Vth", 2.0), i, 0);
            if (k >= 0) { el[k].cp = N("G"); el[k].cn = N("S"); }
        } else if (id == "button") {
            if (sp.b("pressed")) add(SElem::Resistor, N("1"), N("2"), 0.05, 0, i, 0);
        } else if (id == "slide_switch") {
            if (sp.b("on")) add(SElem::Resistor, N("COM"), N("1"), 0.05, 0, i, 0);
            else add(SElem::Resistor, N("COM"), N("2"), 0.05, 0, i, 0);
        } else if (id == "tilt") {
            if (!sp.b("tilted")) add(SElem::Resistor, N("1"), N("2"), 0.05, 0, i, 0);
        } else if (id == "reed") {
            if (sp.b("magnet")) add(SElem::Resistor, N("1"), N("2"), 0.05, 0, i, 0);
        } else if (id == "joystick") {
            int v = N("+5V"), g = N("GND");
            double x = clampD(sp.p("x", 50) / 100.0, 0, 1), y = clampD(sp.p("y", 50) / 100.0, 0, 1);
            add(SElem::Resistor, v, N("VRx"), 10000.0 * (1 - x) + 1, 0, i, 0);
            add(SElem::Resistor, N("VRx"), g, 10000.0 * x + 1, 0, i, 1);
            add(SElem::Resistor, v, N("VRy"), 10000.0 * (1 - y) + 1, 0, i, 2);
            add(SElem::Resistor, N("VRy"), g, 10000.0 * y + 1, 0, i, 3);
            if (sp.b("pressed")) add(SElem::Resistor, N("SW"), g, 0.05, 0, i, 4);
        } else if (id == "touch") {
            if (powered) add(SElem::Source, N("SIG"), 0, 100, sp.b("touch") ? vcc : 0.0, i, 0);
        } else if (id == "analog_temp") {
            if (powered) {
                double v = clampD(sp.p("offset", 0) + sp.p("slope", 0.01) * sp.p("temp", 25), 0.0, vcc - 0.1);
                add(SElem::Source, N("Vout"), 0, 50, v, i, 0);
            }
        } else if (id == "dht") {
            if (powered) add(SElem::Source, N("DATA"), 0, 10000, vcc, i, 0);
        } else if (id == "hcsr04") {
            if (powered) add(SElem::Source, N("Echo"), 0, 100, 0.0, i, 0);
        } else if (id == "pir") {
            if (powered) add(SElem::Source, N("OUT"), 0, 100, sp.b("motion") ? 3.3 : 0.0, i, 0);
        } else if (id == "ir_obstacle") {
            if (powered) { bool d = sp.b("obstacle"); add(SElem::Source, N("OUT"), 0, d ? 100 : 10000, d ? 0.0 : vcc, i, 0); }
        } else if (id == "hall") {
            if (powered) { bool d = sp.b("magnet"); add(SElem::Source, N("SIG"), 0, d ? 100 : 10000, d ? 0.0 : vcc, i, 0); }
        } else if (id == "sound" || id == "soil" || id == "gas" || id == "flame" || id == "water" || id == "rain") {
            if (id == "gas") add(SElem::Resistor, N("VCC"), N("GND"), 33.0, 0, i, 9);   // 히터
            if (!powered) continue;
            double ao = 0, lv = 0; bool doActive = false; bool doActiveLow = true;
            double thr = sp.p("thr", 50);
            if (id == "sound") { lv = sp.p("level", 10); ao = vcc * (0.02 + 0.93 * lv / 100.0); doActive = lv > thr; doActiveLow = false; }
            else if (id == "soil") {
                lv = sp.p("moist", 40);
                if (sp.s("model") == "CAP") ao = (2.9 - 1.7 * lv / 100.0) * vcc / 5.0;
                else ao = vcc * (0.97 - 0.9 * lv / 100.0);
                doActive = lv > thr;
            }
            else if (id == "gas") { lv = sp.p("level", 10); ao = vcc * (0.05 + 0.9 * lv / 100.0); doActive = lv > thr; }
            else if (id == "flame") { lv = sp.p("flame", 0); ao = vcc * (0.98 - 0.95 * lv / 100.0); doActive = lv > thr; }
            else if (id == "water") { lv = sp.p("level", 30); ao = vcc * 0.85 * lv / 100.0; }
            else if (id == "rain") { lv = sp.p("wet", 0); ao = vcc * (0.98 - 0.9 * lv / 100.0); doActive = lv > thr; }
            int aoNet = N("AO") >= 0 ? N("AO") : N("S");
            add(SElem::Source, aoNet, 0, 1000, ao, i, 0);
            int doNet = N("DO");
            if (doNet >= 0) {
                bool high = doActiveLow ? !doActive : doActive;
                add(SElem::Source, doNet, 0, high ? 10000 : 100, high ? vcc : 0.0, i, 1);
            }
        } else if (id == "lcd1602_i2c" || id == "lcd2004_i2c") {
            if (sp.lcdBacklight) add(SElem::Resistor, N("VCC"), N("GND"), 250.0, 0, i, 0);
        } else if (id == "lcd1602") {
            add(SElem::Diode, N("A"), N("K"), 110.0, 2.9, i, 0);
        } else if (id == "buzzer") {
            add(SElem::Resistor, N("+"), N("-"), std::max(1.0, sp.p("R", 170)), 0, i, 0);
        } else if (id == "dc_motor") {
            add(SElem::Resistor, N("+"), N("-"), std::max(0.1, sp.p("R", 8)), 0, i, 0);
        } else if (id == "relay") {
            int com = N("COM"), no = N("NO"), nc = N("NC"), in = N("IN"), gnd = N("GND"), v = N("VCC");
            if (!powered) { add(SElem::Resistor, com, nc, 0.05, 0, i, 1); continue; }
            bool lowTrig = sp.s("trigger") == "LOW";
            if (lowTrig) add(SElem::Resistor, v, in, 2000, 0, i, 5);
            else add(SElem::Resistor, in, gnd, 1200, 0, i, 5);
            int k1 = add(SElem::VSwitch, com, no, 0.05, lowTrig ? 2.5 : 2.0, i, 0);
            int k2 = add(SElem::VSwitch, com, nc, 0.05, lowTrig ? 2.5 : 2.0, i, 1);
            for (int k : { k1, k2 }) {
                if (k < 0) continue;
                el[k].cp = lowTrig ? v : in;
                el[k].cn = lowTrig ? in : gnd;
            }
            if (k2 >= 0) el[k2].invert = true;
        } else if (id == "stepper") {
            for (const char* n : { "IN1", "IN2", "IN3", "IN4" }) add(SElem::Resistor, N(n), N("-"), 2700, 0, i, 0);
        } else if (id.startsWith("custom:")) {
            if (!powered) continue;
            int sig = -1;
            for (int k = 0; k < sp.def->pins.size(); ++k) {
                PinRole r = sp.def->pins[k].role;
                if (r == PinRole::SigAnalogOut || r == PinRole::SigDigitalOut) { sig = sp.net[k]; break; }
            }
            QString out = sp.s("output");
            if (out == "analog") {
                double v = sp.p("vmin", 0) + (sp.p("vmax", 5) - sp.p("vmin", 0)) * sp.p("level", 50) / 100.0;
                add(SElem::Source, sig, 0, 1000, clampD(v, 0, vcc), i, 0);
            } else {
                bool st = sp.b("state");
                if (out == "digital_low") st = !st;
                add(SElem::Source, sig, 0, 100, st ? vcc : 0.0, i, 0);
            }
        }
    }
}

bool Simulator::solveForRead()
{
    if (m_readVersion == m_version.load()) return true;
    buildElements(m_readElems, nullptr);
    bool ok = CircuitSolver::solve(m_nl.netCount, m_readElems, m_readVolts);
    m_readFloating = CircuitSolver::floatingNodes(m_nl.netCount, m_readElems);
    m_readVersion = m_version.load();
    return ok;
}

double Simulator::netVoltage(int net)
{
    solveForRead();
    if (net <= 0 || net >= (int)m_readVolts.size()) return 0.0;
    return m_readVolts[net];
}

void Simulator::checkInterrupts()
{
    if (m_irqMode.isEmpty()) return;
    for (auto it = m_irqMode.begin(); it != m_irqMode.end(); ++it) {
        int irq = it.key(), mode = it.value();
        int pin = m_irqPin.value(irq, -1);
        int net = netOfArdPin(pin);
        if (net < 0) continue;
        double v = netVoltage(net);
        int last = m_irqLast.value(irq, -1);
        int level = v > 2.5 ? 1 : 0;
        bool fire = false;
        if (last >= 0) {
            if (mode == ard::INT_CHANGE && level != last) fire = true;
            if (mode == ard::INT_RISING && last == 0 && level == 1) fire = true;
            if (mode == ard::INT_FALLING && last == 1 && level == 0) fire = true;
        }
        if (mode == ard::INT_LOW && level == 0) fire = true;
        m_irqLast[irq] = level;
        if (fire) m_irqPending.fetch_or(1u << irq);
    }
}

// ===========================================================================
// ArduinoHost 구현 (인터프리터 스레드)
// ===========================================================================
void Simulator::pinMode(int pin, int mode)
{
    QMutexLocker lock(&m_mutex);
    auto it = m_pins.find(pin);
    if (it == m_pins.end()) {
        logOnce(QString("nopin%1").arg(pin), QStringLiteral("pinMode(%1): 이 보드에 없는 핀 번호입니다.").arg(pin), 1);
        return;
    }
    it->mode = mode;
    if (mode == ard::MODE_INPUT_PULLUP) it->out = 1;
    if (mode == ard::MODE_INPUT) it->out = 0;
    if (pin == 0 || pin == 1)
        logOnce("serialpins", QStringLiteral("D0/D1 은 시리얼(USB) 통신에 쓰이는 핀입니다. 가능하면 다른 핀을 사용하세요."), 1);
    bumpVersion();
}

void Simulator::digitalWrite(int pin, int value)
{
    QMutexLocker lock(&m_mutex);
    auto it = m_pins.find(pin);
    if (it == m_pins.end()) return;
    int nv = value ? 1 : 0;
    if (it->out == 1 && nv == 0) it->lastFallUs = micros();
    it->out = nv;
    it->pwm = -1;
    if (it->mode != ard::MODE_OUTPUT && nv && it->mode == ard::MODE_INPUT)
        logOnce(QString("wnomode%1").arg(pin), QStringLiteral("%1: pinMode(OUTPUT) 없이 digitalWrite(HIGH) → 내부 풀업만 켜집니다 (LED가 아주 어둡게 켜짐).").arg(pinLabel(pin, m_boardId)), 1);
    bumpVersion();
    checkInterrupts();
}

int Simulator::digitalRead(int pin)
{
    QMutexLocker lock(&m_mutex);
    auto it = m_pins.find(pin);
    if (it == m_pins.end()) return 0;
    if (it->mode == ard::MODE_OUTPUT && it->pwm < 0) return it->out;
    int net = netOfArdPin(pin);
    double v = netVoltage(net);
    if (net > 0 && net < (int)m_readFloating.size() && m_readFloating[net]) {
        if (AppSettings::instance().warnFloating)
            logOnce(QString("float%1").arg(pin), QStringLiteral("%1 입력이 플로팅 상태입니다 (값이 무작위). INPUT_PULLUP 또는 풀업/풀다운 저항(10kΩ)을 사용하세요.").arg(pinLabel(pin, m_boardId)), 1);
        it->lastRead = std::rand() & 1;
        return it->lastRead;
    }
    if (v > 3.0) it->lastRead = 1;
    else if (v < 1.5) it->lastRead = 0;
    return it->lastRead;
}

int Simulator::analogRead(int pin)
{
    QMutexLocker lock(&m_mutex);
    int ap = resolveAnalogPin(pin);
    if (ap < 0) {
        logOnce(QString("noadc%1").arg(pin), QStringLiteral("analogRead(%1): 아날로그 입력 핀이 아닙니다 (A0~ 사용).").arg(pin), 1);
        return 0;
    }
    int net = netOfArdPin(ap);
    double v = netVoltage(net);
    if (net > 0 && net < (int)m_readFloating.size() && m_readFloating[net]) {
        if (AppSettings::instance().warnFloating)
            logOnce(QString("afloat%1").arg(ap), QStringLiteral("%1 아날로그 입력이 연결되지 않았습니다 (플로팅, 값이 무작위).").arg(pinLabel(ap, m_boardId)), 1);
        return 300 + std::rand() % 400;
    }
    return (int)clampD(std::round(v / kVcc * 1023.0), 0, 1023);
}

void Simulator::analogWrite(int pin, int value)
{
    QMutexLocker lock(&m_mutex);
    auto it = m_pins.find(pin);
    if (it == m_pins.end()) return;
    value = (int)clampD(value, 0, 255);
    it->mode = ard::MODE_OUTPUT;
    if (!it->pwmCapable) {
        logOnce(QString("nopwm%1").arg(pin), QStringLiteral("%1 은(는) PWM 핀이 아닙니다. analogWrite 는 ~ 표시 핀(3,5,6,9,10,11)에서만 동작합니다 → 128 이상이면 HIGH 로 처리").arg(pinLabel(pin, m_boardId)), 1);
        it->pwm = -1;
        it->out = value >= 128 ? 1 : 0;
    } else if (value == 0) { it->pwm = -1; it->out = 0; }
    else if (value == 255) { it->pwm = -1; it->out = 1; }
    else { it->pwm = value; }
    bumpVersion();
}

uint64_t Simulator::micros()
{
    return (uint64_t)std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - m_t0).count();
}

void Simulator::delayMicros(uint64_t us)
{
    const uint64_t target = micros() + us;
    while (!m_stop.load()) {
        uint64_t now = micros();
        if (now >= target) break;
        uint64_t rem = target - now;
        if (rem > 2500) std::this_thread::sleep_for(std::chrono::microseconds(std::min<uint64_t>(rem - 1500, 10000)));
        else if (rem > 300) std::this_thread::yield();
        // 그 이하는 바쁜 대기
    }
}

void Simulator::onLoopIteration()
{
    if ((++m_loopCount & 31) == 0) std::this_thread::yield();
}

void Simulator::serialBegin(long baud)
{
    QMutexLocker lock(&m_mutex);
    m_baud = baud > 0 ? baud : 9600;
}

void Simulator::serialWrite(const std::string& bytes)
{
    uint64_t waitUs = 0;
    {
        QMutexLocker lock(&m_mutex);
        if (m_baud <= 0) {
            logOnce("nobegin", QStringLiteral("Serial.begin(9600) 을 먼저 호출해야 시리얼 출력이 나옵니다."), 1);
            return;
        }
        m_serialOut.append(bytes.data(), (int)bytes.size());
        if (m_serialOut.size() > 512 * 1024) m_serialOut = m_serialOut.right(256 * 1024);
        m_lastTxUs = micros();
        // 전송 속도(보레이트) 흉내: 출력이 많으면 실제처럼 느려짐
        static thread_local uint64_t txFreeAt = 0;
        uint64_t now = m_lastTxUs;
        double perByte = 10.0e6 / (double)m_baud;
        if (txFreeAt < now) txFreeAt = now;
        txFreeAt += (uint64_t)(perByte * bytes.size());
        uint64_t buffer = (uint64_t)(perByte * 64);
        if (txFreeAt > now + buffer) waitUs = txFreeAt - now - buffer;
    }
    if (waitUs > 0) delayMicros(waitUs);
}

int Simulator::serialAvailable()
{
    QMutexLocker lock(&m_mutex);
    return (int)m_serialIn.size();
}

int Simulator::serialRead()
{
    QMutexLocker lock(&m_mutex);
    if (m_serialIn.empty()) return -1;
    int c = (unsigned char)m_serialIn.front();
    m_serialIn.pop_front();
    return c;
}

int Simulator::serialPeek()
{
    QMutexLocker lock(&m_mutex);
    return m_serialIn.empty() ? -1 : (unsigned char)m_serialIn.front();
}

void Simulator::tone(int pin, unsigned int freq, unsigned long durationMs)
{
    QMutexLocker lock(&m_mutex);
    auto it = m_pins.find(pin);
    if (it == m_pins.end()) return;
    it->mode = ard::MODE_OUTPUT;
    it->toneHz = freq;
    it->toneEndUs = durationMs ? micros() + (uint64_t)durationMs * 1000 : 0;
    bumpVersion();
}

void Simulator::noTone(int pin)
{
    QMutexLocker lock(&m_mutex);
    auto it = m_pins.find(pin);
    if (it == m_pins.end()) return;
    it->toneHz = 0;
    it->out = 0;
    bumpVersion();
}

unsigned long Simulator::pulseIn(int pin, int state, unsigned long timeoutUs)
{
    unsigned long result = 0;
    uint64_t waitUs = timeoutUs;
    {
        QMutexLocker lock(&m_mutex);
        int net = netOfArdPin(pin);
        int idx = findPartOnNet("hcsr04", "Echo", net);
        if (idx >= 0) {
            const SimPart& sp = m_parts[idx];
            bool powered = partPowered(sp);
            // Trig 핀이 최근에 펄스를 받았는지 확인
            int trigNet = pinNet(sp, "Trig");
            bool triggered = false;
            for (auto it = m_pins.begin(); it != m_pins.end(); ++it)
                if (netOfArdPin(it.key()) == trigNet && it->lastFallUs > 0 && micros() - it->lastFallUs < 200000) triggered = true;
            if (!powered) logOnce(QString("sonarpw%1").arg(idx), QStringLiteral("%1: 초음파 센서 전원(VCC/GND) 연결을 확인하세요.").arg(sp.ref), 1);
            else if (!triggered) logOnce(QString("sonartrig%1").arg(idx), QStringLiteral("%1: Trig 핀에 10µs HIGH 펄스를 먼저 보내야 측정됩니다.").arg(sp.ref), 1);
            if (powered && triggered && state == 1) {
                double d = sp.p("dist", 50);
                double mn = sp.p("minCm", 2), mx = sp.p("maxCm", 400);
                double dur = d < mn ? mn * 58.0 : (d > mx ? 38000.0 : d * 58.0);
                if (dur <= timeoutUs) { result = (unsigned long)dur; waitUs = (uint64_t)dur + 150; }
            }
        }
    }
    delayMicros(waitUs);
    return result;
}

int Simulator::digitalPinToInterrupt(int pin)
{
    QMutexLocker lock(&m_mutex);
    auto it = m_pins.find(pin);
    return it == m_pins.end() ? -1 : it->irq;
}

void Simulator::attachInterrupt(int irq, int mode)
{
    QMutexLocker lock(&m_mutex);
    if (!m_irqPin.contains(irq)) {
        logOnce(QString("noirq%1").arg(irq), QStringLiteral("attachInterrupt: 인터럽트 번호 %1 이(가) 이 보드에 없습니다 (digitalPinToInterrupt(핀) 사용 권장).").arg(irq), 1);
        return;
    }
    m_irqMode[irq] = mode;
    m_irqLast.remove(irq);
    checkInterrupts();
}

void Simulator::detachInterrupt(int irq)
{
    QMutexLocker lock(&m_mutex);
    m_irqMode.remove(irq);
}

int Simulator::pollInterrupt()
{
    unsigned bits = m_irqPending.load();
    if (!bits) return -1;
    for (int i = 0; i < 32; ++i) {
        if (bits & (1u << i)) { m_irqPending.fetch_and(~(1u << i)); return i; }
    }
    return -1;
}

bool Simulator::lookupConstant(const std::string& nameStd, double& value)
{
    const QString name = QString::fromStdString(nameStd);
    const bool mega = m_boardId == "arduino_mega";
    const bool nano = m_boardId == "arduino_nano";
    if (name.size() >= 2 && name[0] == 'A' && name.mid(1).toInt() >= 0) {
        bool ok = false;
        int n = name.mid(1).toInt(&ok);
        if (ok) {
            int count = mega ? 16 : (nano ? 8 : 6);
            if (n < count) { value = (mega ? 54 : 14) + n; return true; }
        }
    }
    static const QHash<QString, double> common = {
        { "LED_BUILTIN", 13 }, { "DHT11", 11 }, { "DHT22", 22 }, { "DHT21", 21 }, { "AM2301", 21 }, { "AM2302", 22 },
        { "NEO_GRB", 0x52 }, { "NEO_RGB", 0x06 }, { "NEO_RGBW", 0xC6 }, { "NEO_GRBW", 0xD2 }, { "NEO_KHZ800", 0 }, { "NEO_KHZ400", 0x100 },
        { "BME280_ADDRESS", 0x77 }, { "BME280_ADDRESS_ALTERNATE", 0x76 }, { "BMP280_ADDRESS", 0x77 }, { "BMP280_ADDRESS_ALT", 0x76 },
        { "SERIAL_8N1", 0x06 }, { "MAX_DISTANCE", 200 }, { "LCD_5x8DOTS", 0 }, { "POSITIVE", 1 }, { "NEGATIVE", 0 },
        { "DEFAULT", 1 }, { "INTERNAL", 3 }, { "EXTERNAL", 0 },
    };
    // pitches.h 음계 상수 (NOTE_C4 = 262 ...)
    if (name.startsWith("NOTE_") || name == "REST") {
        static const QHash<QString, int> notes = [] {
            QHash<QString, int> h;
            const char* n[12] = { "C", "CS", "D", "DS", "E", "F", "FS", "G", "GS", "A", "AS", "B" };
            for (int oct = 0; oct <= 8; ++oct)
                for (int i = 0; i < 12; ++i) {
                    int midi = 12 * (oct + 1) + i;
                    h.insert(QString("NOTE_%1%2").arg(n[i]).arg(oct), (int)std::lround(440.0 * std::pow(2.0, (midi - 69) / 12.0)));
                }
            h.insert("REST", 0);
            return h;
        }();
        auto it = notes.find(name);
        if (it != notes.end()) { value = it.value(); return true; }
    }
    if (name == "SDA") { value = mega ? 20 : 18; return true; }
    if (name == "SCL") { value = mega ? 21 : 19; return true; }
    if (name == "MOSI") { value = mega ? 51 : 11; return true; }
    if (name == "MISO") { value = mega ? 50 : 12; return true; }
    if (name == "SCK") { value = mega ? 52 : 13; return true; }
    if (name == "SS") { value = mega ? 53 : 10; return true; }
    auto it = common.find(name);
    if (it != common.end()) { value = it.value(); return true; }
    return false;
}

bool Simulator::isLibraryClass(const std::string& c)
{
    static const QSet<QString> classes = { "Servo", "DHT", "LiquidCrystal_I2C", "LiquidCrystal", "Adafruit_NeoPixel",
                                           "Stepper", "Adafruit_BME280", "Adafruit_BMP280", "NewPing" };
    return classes.contains(QString::fromStdString(c));
}

bool Simulator::isGlobalObject(const std::string& name)
{
    return name == "Wire";
}

int Simulator::bindLibrary(LibInst& li)
{
    auto argI = [&](int k, int def = -1) { return k < (int)li.args.size() ? (int)li.args[k].num : def; };
    const bool mega = m_boardId == "arduino_mega";
    int sdaNet = netOfArdPin(mega ? 20 : 18), sclNet = netOfArdPin(mega ? 21 : 19);
    if (li.cls == "DHT") {
        int idx = findPartOnNet("dht", "DATA", netOfArdPin(argI(0)));
        if (idx < 0) logOnce(QString("dhtnc%1").arg(argI(0)), QStringLiteral("DHT(%1): 해당 핀에 연결된 DHT 센서가 없습니다 (DATA 핀 확인).").arg(argI(0)), 1);
        return idx;
    }
    if (li.cls == "LiquidCrystal_I2C") {
        int addr = argI(0, 0x27);
        int candidate = -1;
        for (int i = 0; i < m_parts.size(); ++i) {
            const SimPart& sp = m_parts[i];
            if (sp.id != "lcd1602_i2c" && sp.id != "lcd2004_i2c") continue;
            if (pinNet(sp, "SDA") != sdaNet || pinNet(sp, "SCL") != sclNet || sdaNet < 0) { candidate = -2; continue; }
            bool ok = false;
            int a = sp.s("addr").toInt(&ok, 0);
            if (ok && a == addr) return i;
            candidate = -3;
        }
        if (candidate == -2) logOnce("lcdbus", QStringLiteral("LCD I2C: SDA/SCL 이 아두이노 SDA/SCL(Uno: A4/A5) 에 연결되어 있지 않습니다."), 1);
        else if (candidate == -3) logOnce("lcdaddr", QStringLiteral("LCD I2C: 주소 0x%1 의 LCD 가 없습니다. LCD 속성의 I2C 주소(0x27/0x3F)를 확인하세요.").arg(addr, 0, 16), 1);
        else logOnce("lcdnone", QStringLiteral("LiquidCrystal_I2C: 회로에 I2C LCD 가 없습니다."), 1);
        return -1;
    }
    if (li.cls == "LiquidCrystal") {
        int idx = findPartOnNet("lcd1602", "RS", netOfArdPin(argI(0)));
        if (idx >= 0 && m_parts[idx].id != "lcd1602") idx = -1;
        if (idx < 0) logOnce("lcdpar", QStringLiteral("LiquidCrystal(rs=%1, ...): RS 핀에 연결된 병렬 LCD 1602 가 없습니다.").arg(argI(0)), 1);
        return idx;
    }
    if (li.cls == "Adafruit_NeoPixel") {
        int idx = findPartOnNet("neopixel", "DIN", netOfArdPin(argI(1)));
        if (idx < 0) logOnce(QString("neo%1").arg(argI(1)), QStringLiteral("NeoPixel: 핀 %1 에 연결된 NeoPixel(DIN) 이 없습니다.").arg(argI(1)), 1);
        else {
            int n = qMax(1, argI(0, 8));
            m_parts[idx].pix = QVector<quint32>(n, 0);
            m_parts[idx].pixShown = QVector<quint32>(n, 0);
        }
        return idx;
    }
    if (li.cls == "Stepper") {
        for (int k = 1; k <= 4; ++k) {
            int net = netOfArdPin(argI(k));
            for (const char* pn : { "IN1", "IN2", "IN3", "IN4" }) {
                int idx = findPartOnNet("stepper", pn, net);
                if (idx >= 0) { m_parts[idx].stepsPerRev = qMax(1, argI(0, 2048)); return idx; }
            }
        }
        logOnce("stepnc", QStringLiteral("Stepper: 지정한 핀에 연결된 ULN2003 스텝모터 드라이버가 없습니다."), 1);
        return -1;
    }
    if (li.cls == "NewPing") {
        int idx = findPartOnNet("hcsr04", "Trig", netOfArdPin(argI(0)));
        if (idx < 0) idx = findPartOnNet("hcsr04", "Echo", netOfArdPin(argI(1)));
        if (idx < 0) logOnce("pingnc", QStringLiteral("NewPing: Trig/Echo 핀에 연결된 초음파 센서가 없습니다."), 1);
        return idx;
    }
    return -1;
}

void Simulator::libraryConstruct(const std::string& className, int instanceId, const std::vector<LibValue>& args)
{
    QMutexLocker lock(&m_mutex);
    LibInst li;
    li.cls = QString::fromStdString(className);
    li.args = args;
    li.part = bindLibrary(li);
    m_libs[instanceId] = li;
}

QString Simulator::formatLibValue(const std::vector<LibValue>& args) const
{
    if (args.empty()) return QString();
    const LibValue& v = args[0];
    if (v.kind == LibValue::String) return QString::fromStdString(v.str);
    if (v.kind == LibValue::Float) {
        int digits = args.size() > 1 ? (int)args[1].num : 2;
        if (std::isnan(v.num)) return "nan";
        return QString::number(v.num, 'f', qBound(0, digits, 10));
    }
    if (v.kind == LibValue::Int) {
        int base = args.size() > 1 ? (int)args[1].num : 10;
        long long n = (long long)v.num;
        if (base == 16) return QString::number((qulonglong)n & 0xFFFFFFFFull, 16).toUpper();
        if (base == 2) return QString::number((qulonglong)n & 0xFFFFFFFFull, 2);
        if (base == 8) return QString::number((qulonglong)n & 0xFFFFFFFFull, 8);
        return QString::number(n);
    }
    return QString();
}

void Simulator::lcdWrite(SimPart& sp, const QString& text)
{
    int cols = (int)sp.p("cols", 16), rows = (int)sp.p("rows", 2);
    if (sp.lcd.size() != rows) sp.lcd = QVector<QString>(rows, QString(cols, ' '));
    for (QChar ch : text) {
        if (ch == '\r' || ch == '\n') continue;
        if (sp.lcdRow >= 0 && sp.lcdRow < rows && sp.lcdCol >= 0 && sp.lcdCol < cols) sp.lcd[sp.lcdRow][sp.lcdCol] = ch;
        sp.lcdCol++;
    }
}

bool Simulator::libraryCall(const std::string& classNameStd, int instanceId, const std::string& methodStd,
                            const std::vector<LibValue>& args, LibValue& result, std::string& errorOut)
{
    const QString cls = QString::fromStdString(classNameStd);
    const QString m = QString::fromStdString(methodStd);
    auto argN = [&](int k, double def = 0.0) { return k < (int)args.size() ? args[k].num : def; };
    result = LibValue::makeVoid();
    const double NaN = std::numeric_limits<double>::quiet_NaN();

    QMutexLocker lock(&m_mutex);
    const bool mega = m_boardId == "arduino_mega";

    // ---------------- Wire (전역) ----------------
    if (cls == "Wire") {
        static thread_local int txAddr = -1;
        if (m == "begin" || m == "setClock" || m == "end") return true;
        if (m == "beginTransmission") { txAddr = (int)argN(0); return true; }
        if (m == "write") { result = LibValue::makeInt(1); return true; }
        if (m == "endTransmission") {
            int sdaNet = netOfArdPin(mega ? 20 : 18), sclNet = netOfArdPin(mega ? 21 : 19);
            bool found = false;
            for (const SimPart& sp : m_parts) {
                bool i2c = sp.id == "lcd1602_i2c" || sp.id == "lcd2004_i2c" || sp.id == "bme280";
                if (!i2c || pinNet(sp, "SDA") != sdaNet || pinNet(sp, "SCL") != sclNet || sdaNet < 0 || !partPowered(sp)) continue;
                if (sp.s("addr").toInt(nullptr, 0) == txAddr) found = true;
            }
            result = LibValue::makeInt(found ? 0 : 2);
            return true;
        }
        if (m == "requestFrom" || m == "available") { result = LibValue::makeInt(0); return true; }
        if (m == "read") { result = LibValue::makeInt(-1); return true; }
        result = LibValue::makeInt(0);
        return true;
    }

    auto it = m_libs.find(instanceId);
    if (it == m_libs.end()) { errorOut = "알 수 없는 라이브러리 객체입니다: " + classNameStd; return false; }
    LibInst& li = it.value();
    SimPart* sp = (li.part >= 0 && li.part < m_parts.size()) ? &m_parts[li.part] : nullptr;

    // ---------------- Servo ----------------
    if (cls == "Servo") {
        if (m == "attach") {
            int pin = (int)argN(0);
            li.servoPin = pin;
            li.part = findPartOnNet("servo", "SIG", netOfArdPin(pin));
            if (li.part < 0) logOnce(QString("servonc%1").arg(pin), QStringLiteral("Servo.attach(%1): 해당 핀에 서보 SIG(주황) 선이 연결되어 있지 않습니다.").arg(pin), 1);
            else m_parts[li.part].servoAttached = true;
            result = LibValue::makeInt(li.part >= 0 ? 1 : 0);
            return true;
        }
        if (m == "write" || m == "writeMicroseconds") {
            double v = argN(0);
            double ang = (m == "writeMicroseconds" || v >= 544) ? (v - 544.0) / (2400.0 - 544.0) * 180.0 : v;
            ang = clampD(ang, 0, 180);
            if (sp) {
                sp->servoAngle = ang;
                if (!partPowered(*sp)) logOnce(QString("servopw%1").arg(li.part), QStringLiteral("%1: 서보 전원(V+ 빨강, GND 갈색)이 연결되지 않았습니다.").arg(sp->ref), 1);
            }
            return true;
        }
        if (m == "read") { result = LibValue::makeInt(sp ? (long long)std::lround(sp->servoAngle) : 0); return true; }
        if (m == "readMicroseconds") { result = LibValue::makeInt(sp ? (long long)(544 + sp->servoAngle / 180.0 * 1856) : 0); return true; }
        if (m == "attached") { result = LibValue::makeInt(sp && sp->servoAttached ? 1 : 0); return true; }
        if (m == "detach") { if (sp) sp->servoAttached = false; return true; }
        errorOut = "Servo 에 없는 함수: " + methodStd; return false;
    }

    // ---------------- DHT ----------------
    if (cls == "DHT") {
        if (m == "begin") return true;
        if (m == "read") { result = LibValue::makeInt(sp && partPowered(*sp) ? 1 : 0); return true; }
        bool ok = sp && partPowered(*sp);
        if (sp && !ok) logOnce(QString("dhtpw%1").arg(li.part), QStringLiteral("%1: DHT 센서 전원(VCC/GND)을 확인하세요 → NaN 반환").arg(sp->ref), 1);
        bool dht11 = sp && sp->s("dhtType") == "DHT11";
        if (m == "readTemperature") {
            if (!ok) { result = LibValue::makeFloat(NaN); return true; }
            double t = sp->p("temp", 24);
            if (dht11) t = std::round(clampD(t, 0, 50)); else t = std::round(clampD(t, -40, 80) * 10) / 10;
            if (argN(0) != 0) t = t * 1.8 + 32;
            result = LibValue::makeFloat(t);
            return true;
        }
        if (m == "readHumidity") {
            if (!ok) { result = LibValue::makeFloat(NaN); return true; }
            double h = sp->p("hum", 55);
            if (dht11) h = std::round(clampD(h, 20, 90)); else h = std::round(clampD(h, 0, 100) * 10) / 10;
            result = LibValue::makeFloat(h);
            return true;
        }
        if (m == "computeHeatIndex") {
            double t = argN(0), h = argN(1);
            bool isF = args.size() > 2 ? argN(2) != 0 : true;
            if (!isF) t = t * 1.8 + 32;
            double hi = 0.5 * (t + 61.0 + ((t - 68.0) * 1.2) + (h * 0.094));
            if (hi > 79) {
                hi = -42.379 + 2.04901523 * t + 10.14333127 * h - 0.22475541 * t * h - 0.00683783 * t * t
                     - 0.05481717 * h * h + 0.00122874 * t * t * h + 0.00085282 * t * h * h - 0.00000199 * t * t * h * h;
            }
            result = LibValue::makeFloat(isF ? hi : (hi - 32) * 0.55555);
            return true;
        }
        if (m == "convertCtoF") { result = LibValue::makeFloat(argN(0) * 1.8 + 32); return true; }
        if (m == "convertFtoC") { result = LibValue::makeFloat((argN(0) - 32) * 0.55555); return true; }
        errorOut = "DHT 에 없는 함수: " + methodStd; return false;
    }

    // ---------------- LCD ----------------
    if (cls == "LiquidCrystal_I2C" || cls == "LiquidCrystal") {
        if (!sp) {
            // 연결 안 된 LCD: 조용히 무시 (경고는 bind 시 출력)
            if (m == "print" || m == "write") result = LibValue::makeInt(0);
            return true;
        }
        int cols = (int)sp->p("cols", 16), rows = (int)sp->p("rows", 2);
        auto clearBuf = [&] { sp->lcd = QVector<QString>(rows, QString(cols, ' ')); sp->lcdCol = sp->lcdRow = 0; };
        if (m == "init" || m == "begin") { sp->lcdInit = true; clearBuf(); return true; }
        if (m == "clear") { clearBuf(); return true; }
        if (m == "home") { sp->lcdCol = sp->lcdRow = 0; return true; }
        if (m == "setCursor") { sp->lcdCol = (int)argN(0); sp->lcdRow = (int)argN(1); return true; }
        if (m == "backlight") { sp->lcdBacklight = true; bumpVersion(); return true; }
        if (m == "noBacklight") { sp->lcdBacklight = false; bumpVersion(); return true; }
        if (m == "setBacklight") { sp->lcdBacklight = argN(0) != 0; bumpVersion(); return true; }
        if (m == "display") { sp->lcdDisplay = true; return true; }
        if (m == "noDisplay") { sp->lcdDisplay = false; return true; }
        if (m == "print" || m == "println") {
            QString t = formatLibValue(args);
            lcdWrite(*sp, t);
            result = LibValue::makeInt(t.size());
            return true;
        }
        if (m == "write") {
            QString t = args.empty() ? QString() : (args[0].kind == LibValue::String ? QString::fromStdString(args[0].str) : QString(QChar((int)argN(0))));
            lcdWrite(*sp, t);
            result = LibValue::makeInt(1);
            return true;
        }
        if (m == "scrollDisplayLeft" || m == "scrollDisplayRight") {
            for (QString& line : sp->lcd) {
                if (line.isEmpty()) continue;
                if (m == "scrollDisplayLeft") line = line.mid(1) + line.left(1);
                else line = line.right(1) + line.left(line.size() - 1);
            }
            return true;
        }
        // cursor, noCursor, blink, noBlink, createChar, autoscroll ... 무시
        return true;
    }

    // ---------------- NeoPixel ----------------
    if (cls == "Adafruit_NeoPixel") {
        auto pack = [](int r, int g, int b) { return (quint32)(((r & 255) << 16) | ((g & 255) << 8) | (b & 255)); };
        if (m == "Color") { result = LibValue::makeInt(pack((int)argN(0), (int)argN(1), (int)argN(2))); return true; }
        if (m == "ColorHSV") {
            unsigned hue = (unsigned)((long long)argN(0, 0)) & 0xFFFF;
            unsigned sat = args.size() > 1 ? (unsigned)argN(1) & 255 : 255, val = args.size() > 2 ? (unsigned)argN(2) & 255 : 255;
            unsigned r, g, b;
            hue = (hue * 1530L + 32768) / 65536;
            if (hue < 510) { b = 0; if (hue < 255) { r = 255; g = hue; } else { r = 510 - hue; g = 255; } }
            else if (hue < 1020) { r = 0; if (hue < 765) { g = 255; b = hue - 510; } else { g = 1020 - hue; b = 255; } }
            else if (hue < 1530) { g = 0; if (hue < 1275) { r = hue - 1020; b = 255; } else { r = 255; b = 1530 - hue; } }
            else { r = 255; g = b = 0; }
            unsigned v1 = 1 + val, s1 = 1 + sat, s2 = 255 - sat;
            quint32 c = ((((((r * s1) >> 8) + s2) * v1) & 0xff00) << 8) | (((((g * s1) >> 8) + s2) * v1) & 0xff00) | (((((b * s1) >> 8) + s2) * v1) >> 8);
            result = LibValue::makeInt(c);
            return true;
        }
        if (m == "gamma32") { result = LibValue::makeInt((long long)argN(0)); return true; }
        if (m == "gamma8") { result = LibValue::makeInt((long long)argN(0)); return true; }
        if (m == "numPixels") { result = LibValue::makeInt(sp ? sp->pix.size() : (long long)(li.args.empty() ? 0 : li.args[0].num)); return true; }
        if (!sp) return true;
        if (m == "begin" || m == "setPin" || m == "updateType" || m == "updateLength") return true;
        if (m == "setBrightness") { sp->pixBrightness = (int)clampD(argN(0), 0, 255); return true; }
        if (m == "getBrightness") { result = LibValue::makeInt(sp->pixBrightness); return true; }
        if (m == "canShow") { result = LibValue::makeInt(1); return true; }
        if (m == "setPixelColor") {
            int n = (int)argN(0);
            if (n < 0 || n >= sp->pix.size()) return true;
            sp->pix[n] = args.size() >= 4 ? pack((int)argN(1), (int)argN(2), (int)argN(3)) : (quint32)((long long)argN(1) & 0xFFFFFF);
            return true;
        }
        if (m == "getPixelColor") { int n = (int)argN(0); result = LibValue::makeInt(n >= 0 && n < sp->pix.size() ? sp->pix[n] : 0); return true; }
        if (m == "clear") { sp->pix.fill(0); return true; }
        if (m == "fill") {
            quint32 c = (quint32)((long long)argN(0, 0) & 0xFFFFFF);
            int first = (int)argN(1, 0), count = (int)argN(2, 0);
            if (count <= 0) count = sp->pix.size() - first;
            for (int k = first; k < first + count && k < sp->pix.size(); ++k) if (k >= 0) sp->pix[k] = c;
            return true;
        }
        if (m == "show") {
            int br = sp->pixBrightness;
            sp->pixShown.resize(sp->pix.size());
            for (int k = 0; k < sp->pix.size(); ++k) {
                quint32 c = sp->pix[k];
                int r = ((c >> 16) & 255) * (br + 1) / 256, g = ((c >> 8) & 255) * (br + 1) / 256, b = (c & 255) * (br + 1) / 256;
                sp->pixShown[k] = 0xFF000000u | (quint32)(r << 16) | (quint32)(g << 8) | (quint32)b;
            }
            return true;
        }
        return true;
    }

    // ---------------- Stepper ----------------
    if (cls == "Stepper") {
        if (m == "setSpeed") { li.stepSpeedRpm = qMax(1, (int)argN(0)); return true; }
        if (m == "version") { result = LibValue::makeInt(5); return true; }
        if (m == "step") {
            long n = (long)argN(0);
            int spr = sp ? sp->stepsPerRev : qMax(1, (int)(li.args.empty() ? 2048 : li.args[0].num));
            double perStep = 60.0e6 / ((double)li.stepSpeedRpm * spr);
            int partIdx = li.part;
            long dir = n >= 0 ? 1 : -1;
            for (long k = 0; k < std::labs(n) && !m_stop.load(); ++k) {
                if (partIdx >= 0) m_parts[partIdx].stepPos += dir;
                lock.unlock();
                delayMicros((uint64_t)perStep);
                lock.relock();
            }
            return true;
        }
        return true;
    }

    // ---------------- NewPing ----------------
    if (cls == "NewPing") {
        double dist = 0, mn = 2, mx = 400;
        bool ok = sp && partPowered(*sp);
        int maxCm = (int)(li.args.size() > 2 ? li.args[2].num : 500);
        if (ok) { dist = sp->p("dist", 50); mn = sp->p("minCm", 2); mx = std::min(sp->p("maxCm", 400), (double)maxCm); }
        bool inRange = ok && dist >= mn && dist <= mx;
        double us = inRange ? dist * 57.0 : 0.0;
        uint64_t wait = inRange ? (uint64_t)us : (uint64_t)(maxCm * 57);
        if (m == "ping_cm") { lock.unlock(); delayMicros(wait); result = LibValue::makeInt(inRange ? (long long)std::lround(dist) : 0); return true; }
        if (m == "ping_in") { lock.unlock(); delayMicros(wait); result = LibValue::makeInt(inRange ? (long long)std::lround(dist / 2.54) : 0); return true; }
        if (m == "ping" || m == "ping_median") { lock.unlock(); delayMicros(wait); result = LibValue::makeInt((long long)us); return true; }
        if (m == "convert_cm") { result = LibValue::makeInt((long long)(argN(0) / 57)); return true; }
        if (m == "convert_in") { result = LibValue::makeInt((long long)(argN(0) / 146)); return true; }
        return true;
    }

    // ---------------- BME280 / BMP280 ----------------
    if (cls == "Adafruit_BME280" || cls == "Adafruit_BMP280") {
        if (m == "begin") {
            int addr = args.empty() ? 0x77 : (int)argN(0);
            int sdaNet = netOfArdPin(mega ? 20 : 18), sclNet = netOfArdPin(mega ? 21 : 19);
            li.part = -1;
            for (int i = 0; i < m_parts.size(); ++i) {
                const SimPart& s = m_parts[i];
                if (s.id != "bme280" || pinNet(s, "SDA") != sdaNet || pinNet(s, "SCL") != sclNet || sdaNet < 0) continue;
                if (s.s("addr").toInt(nullptr, 0) != addr) continue;
                if (cls == "Adafruit_BME280" && s.s("model") != "BME280") continue;
                if (partPowered(s)) li.part = i;
            }
            if (li.part < 0) logOnce(QString("bme%1").arg(addr), QStringLiteral("%1.begin(0x%2): 해당 주소의 센서를 I2C(SDA/SCL)에서 찾지 못했습니다 (주소·모델·배선 확인).").arg(cls).arg(addr, 0, 16), 1);
            result = LibValue::makeInt(li.part >= 0 ? 1 : 0);
            return true;
        }
        if (!sp) { result = LibValue::makeFloat(NaN); return true; }
        if (m == "readTemperature") { result = LibValue::makeFloat(sp->p("temp", 23)); return true; }
        if (m == "readPressure") { result = LibValue::makeFloat(sp->p("press", 1013.25) * 100.0); return true; }
        if (m == "readHumidity") { result = LibValue::makeFloat(cls == "Adafruit_BME280" ? sp->p("hum", 45) : NaN); return true; }
        if (m == "readAltitude") {
            double sea = args.empty() ? 1013.25 : argN(0);
            result = LibValue::makeFloat(44330.0 * (1.0 - std::pow(sp->p("press", 1013.25) / sea, 0.1903)));
            return true;
        }
        return true;
    }

    errorOut = "지원하지 않는 라이브러리: " + classNameStd;
    return false;
}

// ===========================================================================
// GUI 갱신 (30fps)
// ===========================================================================
void Simulator::onTick()
{
    if (!m_running) return;
    computeDisplay();
}

void Simulator::computeDisplay()
{
    const AppSettings& cfg = AppSettings::instance();
    QVector<QVariantMap> visuals(m_parts.size());
    QString serialText;
    QVector<QPair<QString, int>> logs;
    {
        QMutexLocker lock(&m_mutex);
        const uint64_t now = micros();

        // ---- PWM 시간 분할 ----
        QVector<QPair<int, double>> pwm;
        for (auto it = m_pins.begin(); it != m_pins.end(); ++it) {
            const PinState& ps = it.value();
            bool toneOn = ps.toneHz > 0 && (ps.toneEndUs == 0 || now < ps.toneEndUs);
            if (toneOn) pwm.push_back({ it.key(), 0.5 });
            else if (ps.pwm > 0 && ps.pwm < 255) pwm.push_back({ it.key(), ps.pwm / 255.0 });
        }
        QVector<double> bounds{ 0.0 };
        for (auto& p : pwm) bounds.push_back(p.second);
        bounds.push_back(1.0);
        std::sort(bounds.begin(), bounds.end());
        bounds.erase(std::unique(bounds.begin(), bounds.end()), bounds.end());

        std::vector<SElem> el;
        std::vector<double> volts, avgV;
        std::vector<double> avgI, avgI2, peakI;
        bool first = true;
        for (int s = 0; s + 1 < bounds.size(); ++s) {
            double w = bounds[s + 1] - bounds[s];
            if (w <= 1e-9) continue;
            QHash<int, bool> high;
            for (auto& p : pwm) high[p.first] = p.second > bounds[s] + 1e-9;
            buildElements(el, &high);
            CircuitSolver::solve(m_nl.netCount, el, volts);
            if (first) {
                avgV.assign(volts.size(), 0.0);
                avgI.assign(el.size(), 0.0); avgI2.assign(el.size(), 0.0); peakI.assign(el.size(), 0.0);
                first = false;
            }
            for (size_t k = 0; k < volts.size() && k < avgV.size(); ++k) avgV[k] += w * volts[k];
            for (size_t k = 0; k < el.size() && k < avgI.size(); ++k) {
                avgI[k] += w * el[k].current;
                avgI2[k] += w * el[k].current * el[k].current;
                peakI[k] = std::max(peakI[k], std::fabs(el[k].current));
            }
        }
        if (first) { buildElements(el, nullptr); CircuitSolver::solve(m_nl.netCount, el, volts); avgV = volts; avgI.assign(el.size(), 0); avgI2 = avgI; peakI = avgI; }
        m_dispVolts = avgV;
        auto VN = [&](int net) { return (net > 0 && net < (int)avgV.size()) ? avgV[net] : 0.0; };

        // 소자 찾기
        QHash<qint64, int> elemOf;
        for (int k = 0; k < (int)el.size(); ++k) elemOf.insert(((qint64)el[k].owner << 32) | (quint32)(el[k].tag & 0xFFFFFFFF), k);
        auto E = [&](int owner, int tag) { return elemOf.value(((qint64)owner << 32) | (quint32)(tag & 0xFFFFFFFF), -1); };

        m_partWarn.clear();
        auto warn = [&](int idx, const QString& w) {
            QString& cur = m_partWarn[idx];
            if (!cur.contains(w)) cur = cur.isEmpty() ? w : cur + "\n" + w;
        };

        // ---- 보드 ----
        {
            QVariantMap v;
            QVariantList states;
            const PartDef* bd = m_parts[m_board].def;
            for (int i = 0; i < bd->pins.size(); ++i) {
                const PinDef& pd = bd->pins[i];
                QVariantList st;
                auto it = m_pins.find(pd.ardPin);
                if (pd.ardPin >= 0 && it != m_pins.end()) {
                    const PinState& ps = it.value();
                    bool toneOn = ps.toneHz > 0 && (ps.toneEndUs == 0 || now < ps.toneEndUs);
                    int net = m_parts[m_board].net[i];
                    if (toneOn) st << 2 << 0.5;
                    else if (ps.pwm > 0) st << 2 << ps.pwm / 255.0;
                    else if (ps.mode == ard::MODE_OUTPUT) st << 1 << ps.out;
                    else if (m_nl.nonBoardPinCount(net) > 0 || ps.mode == ard::MODE_INPUT_PULLUP)
                        st << (ps.mode == ard::MODE_INPUT_PULLUP ? 4 : 3) << (VN(net) > 2.5 ? 1 : 0);
                }
                states << QVariant(st);
            }
            v["pins"] = states;
            auto p13 = m_pins.find(13);
            double l = 0;
            if (p13 != m_pins.end()) {
                if (p13->pwm > 0) l = p13->pwm / 255.0;
                else if (p13->mode == ard::MODE_OUTPUT) l = p13->out;
            }
            v["boardL"] = l;
            v["tx"] = (now - m_lastTxUs) < 80000 && m_lastTxUs > 0;
            v["rx"] = (now - m_lastRxUs) < 80000 && m_lastRxUs > 0;
            // 핀 과전류
            m_pinCurrent.clear();
            for (auto it = m_pins.begin(); it != m_pins.end(); ++it) {
                int k = E(m_board, it.key());
                if (k < 0) continue;
                double ip = peakI[k];
                m_pinCurrent[it.key()] = ip;
                if (ip * 1000.0 > cfg.pinCurrentLimit) {
                    QString msg = QStringLiteral("%1 핀 과전류 %2mA (최대 %3mA) — 직렬 저항을 추가하세요").arg(pinLabel(it.key(), m_boardId)).arg(ip * 1000, 0, 'f', 0).arg(cfg.pinCurrentLimit);
                    warn(m_board, msg);
                    logOnce(QString("oc%1").arg(it.key()), msg, 2);
                }
            }
            int k5 = E(m_board, -5);
            if (k5 >= 0) {
                double i5 = avgI[k5] * 1000.0;
                if (i5 > 2000) { warn(m_board, QStringLiteral("5V 단락!")); logOnce("short5", QStringLiteral("5V 전원이 단락되었습니다 (%1A). 배선을 확인하세요.").arg(i5 / 1000, 0, 'f', 1), 2); }
                else if (i5 > cfg.usbCurrentLimit) { warn(m_board, QStringLiteral("5V 전류 %1mA > USB 한계").arg(i5, 0, 'f', 0)); logOnce("usb5", QStringLiteral("5V 소비전류 %1mA 가 USB 한계(%2mA)를 넘습니다. 외부 전원을 사용하세요.").arg(i5, 0, 'f', 0).arg(cfg.usbCurrentLimit), 1); }
            }
            int k33 = E(m_board, -33);
            if (k33 >= 0 && avgI[k33] > 0.15) { warn(m_board, QStringLiteral("3.3V 과부하")); logOnce("ov33", QStringLiteral("3.3V 핀 전류가 150mA 를 넘습니다."), 1); }
            visuals[m_board] = v;
        }

        // ---- 부품 ----
        for (int i = 0; i < m_parts.size(); ++i) {
            if (i == m_board) continue;
            SimPart& sp = m_parts[i];
            QVariantMap v;
            double vcc = 0;
            bool powered = partPowered(sp, &vcc);
            v["powered"] = powered;
            const QString& id = sp.id;
            auto N = [&](const char* n) { return pinNet(sp, QString::fromLatin1(n)); };
            auto ledLevel = [&](int k, double ifMaxA, int tag, const QString& what) -> double {
                if (sp.burned.contains(tag)) return 0.0;
                if (k < 0) return 0.0;
                double avg = std::max(0.0, avgI[k]);
                double peak = peakI[k];
                if (peak > ifMaxA * cfg.ledBurnFactor) {
                    sp.burned.insert(tag);
                    bumpVersion();
                    logOnce(QString("burn%1_%2").arg(i).arg(tag),
                            QStringLiteral("%1%2 이(가) 과전류 %3mA (정격 %4mA)로 타버렸습니다! 직렬 저항을 넣으세요 (도구 → 저항 자동 추천).")
                                .arg(sp.ref, what).arg(peak * 1000, 0, 'f', 0).arg(ifMaxA * 1000, 0, 'f', 0), 2);
                    return 0.0;
                }
                if (peak > ifMaxA * 1.05) warn(i, QStringLiteral("%1 전류 %2mA > 정격 %3mA").arg(what.isEmpty() ? "LED" : what).arg(peak * 1000, 0, 'f', 0).arg(ifMaxA * 1000, 0, 'f', 0));
                if (avg < 2e-5) return 0.0;
                return clampD(std::sqrt(avg / (ifMaxA * 0.75)), 0.08, 1.0);
            };

            if (id == "led") {
                double ifA = sp.p("If", 20) / 1000.0;
                v["glow"] = ledLevel(E(i, 0), ifA, 0, QString());
                v["burned"] = sp.burned.contains(0);
                double vrev = VN(N("K")) - VN(N("A"));
                if (vrev > 2.0) warn(i, QStringLiteral("역방향 전압 %1V — LED 방향(A=+, K=-)을 확인하세요").arg(vrev, 0, 'f', 1));
            } else if (id == "rgb_led") {
                double ifA = sp.p("If", 20) / 1000.0;
                v["glowR"] = ledLevel(E(i, 0), ifA, 0, "(R)");
                v["glowG"] = ledLevel(E(i, 1), ifA, 1, "(G)");
                v["glowB"] = ledLevel(E(i, 2), ifA, 2, "(B)");
            } else if (id == "seven_seg") {
                double ifA = sp.p("If", 20) / 1000.0;
                QVariantList seg;
                const char* names[8] = { "a", "b", "c", "d", "e", "f", "g", "dp" };
                for (int s = 0; s < 8; ++s) seg << ledLevel(E(i, s), ifA, s, QString("(%1)").arg(names[s]));
                v["seg"] = seg;
            } else if (id == "resistor") {
                int k = E(i, 0);
                if (k >= 0) {
                    double R = sp.p("R", 220), P = avgI2[k] * R, rating = sp.p("P", 0.25);
                    if (P > rating) {
                        v["hot"] = true;
                        warn(i, QStringLiteral("소비전력 %1W > 정격 %2").arg(P, 0, 'f', 3).arg(Units::powerLabel(rating)));
                        logOnce(QString("hot%1").arg(i), QStringLiteral("%1 저항 소비전력 %2W 가 정격(%3)을 초과합니다. 더 큰 정격의 저항을 사용하세요.").arg(sp.ref).arg(P, 0, 'f', 3).arg(Units::powerLabel(rating)), 1);
                    }
                }
            } else if (id == "buzzer") {
                double vd = VN(N("+")) - VN(N("-"));
                bool active = sp.b("active");
                double freq = 0;
                for (auto it = m_pins.begin(); it != m_pins.end(); ++it)
                    if (it->toneHz > 0 && (it->toneEndUs == 0 || now < it->toneEndUs) && netOfArdPin(it.key()) == N("+")) freq = it->toneHz;
                if (active) { if (vd > 2.0 || freq > 0) v["buzz"] = freq > 0 ? freq : -1.0; }
                else if (freq > 0) v["buzz"] = freq;
                else if (vd > 2.0) warn(i, QStringLiteral("수동 부저는 tone() 으로 주파수를 줘야 소리가 납니다"));
            } else if (id == "dc_motor") {
                double vd = VN(N("+")) - VN(N("-"));
                v["motor"] = clampD(vd / std::max(0.5, sp.p("Vr", 5)), -1.5, 1.5);
            } else if (id == "servo") {
                v["angle"] = sp.servoAngle;
                if (!powered) warn(i, QStringLiteral("전원 미연결 (V+, GND)"));
            } else if (id == "lcd1602_i2c" || id == "lcd2004_i2c" || id == "lcd1602") {
                QStringList lines;
                if (sp.lcdDisplay) for (const QString& l : sp.lcd) lines << l;
                v["lcd"] = lines;
                bool bl = id == "lcd1602" ? (E(i, 0) >= 0 && avgI[E(i, 0)] > 0.002) : sp.lcdBacklight;
                v["lcdOn"] = bl;
                if (!powered) warn(i, QStringLiteral("전원 미연결 (VCC/VDD, GND/VSS)"));
            } else if (id.startsWith("neopixel")) {
                QVariantList px;
                if (powered) for (quint32 c : sp.pixShown) px << c;
                v["pixels"] = px;
                if (!powered) warn(i, QStringLiteral("전원 미연결 (VCC, GND)"));
            } else if (id == "stepper") {
                v["stepAngle"] = sp.stepPos * 360.0 / std::max(1, sp.stepsPerRev);
                QVariantList coils;
                for (const char* n : { "IN1", "IN2", "IN3", "IN4" }) coils << (VN(N(n)) > 2.5);
                v["coils"] = coils;
                if (!powered) warn(i, QStringLiteral("모터 전원 미연결 (+, -)"));
            } else if (id == "relay") {
                int k = E(i, 0);
                v["relayOn"] = k >= 0 && el[k].state == 1;
            } else if (id == "mosfet") {
                int k = E(i, 0);
                v["on"] = k >= 0 && el[k].state == 1;
                if (k >= 0 && peakI[k] > sp.p("Id", 10)) warn(i, QStringLiteral("드레인 전류 초과"));
            } else if (id == "npn") {
                int k = E(i, 1), kb = E(i, 0);
                if (k >= 0 && peakI[k] * 1000 > sp.p("Ic", 800)) warn(i, QStringLiteral("컬렉터 전류 %1mA > 최대 %2mA").arg(peakI[k] * 1000, 0, 'f', 0).arg(sp.p("Ic", 800)));
                if (kb >= 0 && peakI[kb] > 0.03) warn(i, QStringLiteral("베이스 전류 과다 %1mA — 베이스 저항(1kΩ) 필요").arg(peakI[kb] * 1000, 0, 'f', 0));
            } else if (id == "pir") {
                v["sigLed"] = sp.b("motion");
            } else if (id == "ir_obstacle" || id == "hall") {
                v["sigLed"] = sp.b(id == "hall" ? "magnet" : "obstacle");
            } else if (id == "sound" || id == "gas" || id == "flame" || id == "rain" || id == "soil") {
                const char* key = id == "sound" || id == "gas" ? "level" : (id == "flame" ? "flame" : (id == "rain" ? "wet" : "moist"));
                v["sigLed"] = sp.p(key) > sp.p("thr", 50);
            }
            // 전원이 필요한 모듈인데 전원이 없는 경우
            bool needsPower = false;
            for (const PinDef& pd : sp.def->pins) if (pd.role == PinRole::Vcc) needsPower = true;
            if (needsPower && !powered && id != "potentiometer" && id != "joystick" && !m_partWarn.contains(i))
                warn(i, QStringLiteral("전원(VCC/GND) 미연결"));
            visuals[i] = v;
        }

        // 경고 → visual
        for (auto it = m_partWarn.begin(); it != m_partWarn.end(); ++it)
            if (it.key() >= 0 && it.key() < visuals.size()) visuals[it.key()]["warn"] = it.value();

        checkInterrupts();

        if (!m_serialOut.isEmpty()) {
            serialText = m_decoder.decode(m_serialOut);
            m_serialOut.clear();
        }
        logs = m_pendingLogs;
        m_pendingLogs.clear();
    }

    // GUI 반영 (락 밖에서)
    for (int i = 0; i < m_parts.size() && i < visuals.size(); ++i) {
        PartItem* item = m_parts[i].item;
        QVariantMap v = visuals[i];
        if (item) {
            item->setSimState(v);
            QString w = v.value("warn").toString();
            item->setToolTip(w.isEmpty() ? QString() : QStringLiteral("⚠ ") + w);
        }
    }
    if (!serialText.isEmpty()) emit serialOutput(serialText);
    for (const auto& l : logs) emit logMessage(l.first, l.second);
}

QString Simulator::pinInfo(PartItem* part, int pin) const
{
    if (!m_running) return QString();
    int idx = m_nl.index.value(part, -1);
    if (idx < 0 || pin < 0 || pin >= m_nl.pinNet[idx].size()) return QString();
    int net = m_nl.pinNet[idx][pin];
    double v = (net > 0 && net < (int)m_dispVolts.size()) ? m_dispVolts[net] : 0.0;
    QString t = QStringLiteral("전압: %1 V").arg(v, 0, 'f', 2);
    if (idx == m_board) {
        int ap = part->def()->pins[pin].ardPin;
        QMutexLocker lock(&m_mutex);
        auto it = m_pins.find(ap);
        if (it != m_pins.end()) {
            static const char* modes[] = { "INPUT", "OUTPUT", "INPUT_PULLUP" };
            t += QStringLiteral(" · 모드: %1").arg(modes[qBound(0, it->mode, 2)]);
            if (it->pwm > 0) t += QStringLiteral(" · PWM %1/255").arg(it->pwm);
            if (it->toneHz > 0) t += QStringLiteral(" · tone %1Hz").arg(it->toneHz);
            double ic = m_pinCurrent.value(ap, 0.0);
            if (ic > 1e-5) t += QStringLiteral(" · 전류 %1 mA").arg(ic * 1000, 0, 'f', 1);
        }
    }
    return t;
}
