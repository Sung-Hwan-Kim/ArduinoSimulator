#include "Catalog.h"
#include "Units.h"
#include <QSet>

// ---------------------------------------------------------------------------
// PartDef helpers
// ---------------------------------------------------------------------------
int PartDef::pinIndex(const QString& pinName) const
{
    for (int i = 0; i < pins.size(); ++i)
        if (pins[i].name.compare(pinName, Qt::CaseInsensitive) == 0) return i;
    return -1;
}

const ParamDef* PartDef::param(const QString& key) const
{
    for (const ParamDef& p : params)
        if (p.key == key) return &p;
    return nullptr;
}

QVariantMap PartDef::defaultParams() const
{
    QVariantMap m;
    for (const ParamDef& p : params) m[p.key] = p.def;
    // 기본 선택지의 sets 적용
    for (const ParamDef& p : params) {
        if (p.type != ParamType::Choice) continue;
        for (const ParamOption& o : p.options) {
            if (o.value == p.def) {
                for (auto it = o.sets.begin(); it != o.sets.end(); ++it) m[it.key()] = it.value();
            }
        }
    }
    return m;
}

// ---------------------------------------------------------------------------
// 생성 도우미
// ---------------------------------------------------------------------------
namespace {

const QString CAT_BOARD   = QStringLiteral("아두이노 보드");
const QString CAT_PASSIVE = QStringLiteral("저항·수동 소자");
const QString CAT_SEMI    = QStringLiteral("반도체 (다이오드·트랜지스터)");
const QString CAT_LED     = QStringLiteral("LED·디스플레이");
const QString CAT_INPUT   = QStringLiteral("스위치·입력 장치");
const QString CAT_SENSOR  = QStringLiteral("센서");
const QString CAT_OUTPUT  = QStringLiteral("모터·출력 장치");

PinDef P(const QString& name, double x, double y, PinRole role = PinRole::Passive, bool optional = false)
{
    PinDef p; p.name = name; p.pos = QPointF(x, y); p.role = role; p.optional = optional;
    return p;
}

ParamOption O(const QString& label, const QVariant& value, const QVariantMap& sets = {})
{
    ParamOption o; o.label = label; o.value = value; o.sets = sets; return o;
}

ParamDef Num(const QString& key, const QString& label, double def, const QString& unit,
             double minV, double maxV, bool live = false)
{
    ParamDef p; p.key = key; p.label = label; p.type = ParamType::Number; p.def = def; p.unit = unit;
    p.minV = minV; p.maxV = maxV; p.live = live; return p;
}

ParamDef Choice(const QString& key, const QString& label, const QVector<ParamOption>& opts, int defIndex = 0)
{
    ParamDef p; p.key = key; p.label = label; p.type = ParamType::Choice; p.options = opts;
    p.def = opts.isEmpty() ? QVariant() : opts[qBound(0, defIndex, (int)opts.size() - 1)].value;
    return p;
}

ParamDef Bool(const QString& key, const QString& label, bool def, bool live = true)
{
    ParamDef p; p.key = key; p.label = label; p.type = ParamType::Bool; p.def = def; p.live = live; return p;
}

ParamDef Hidden(const QString& key, const QVariant& def)
{
    ParamDef p; p.key = key; p.label = key; p.type = ParamType::Number; p.def = def; p.hidden = true;
    p.minV = -1e12; return p;
}

QVector<ParamOption> suggestions(const QVector<double>& values, const QString& unit)
{
    QVector<ParamOption> v;
    for (double d : values) v.push_back(O(Units::format(d, unit), d));
    return v;
}

// 모듈 형태: 아래쪽에 핀을 일렬로 배치
void moduleLayout(PartDef& d, double w, double bodyH, const QVector<PinDef>& pinsIn)
{
    int n = pinsIn.size();
    double span = (n - 1) * 10.0;
    double start = qRound((w - span) / 2.0 / 10.0) * 10.0;
    if (start < 10) start = 10;
    d.size = QSizeF(w, bodyH + 10);
    d.pins.clear();
    for (int i = 0; i < n; ++i) {
        PinDef p = pinsIn[i];
        p.pos = QPointF(start + i * 10.0, bodyH + 10);
        d.pins.push_back(p);
    }
}

PinDef BP(const QString& name, double x, double y, BoardPinKind kind, int ardPin = -1, int adc = -1,
          bool pwm = false, int irq = -1, const QString& func = QString())
{
    PinDef p; p.name = name; p.pos = QPointF(x, y); p.role = PinRole::Board; p.bkind = kind;
    p.ardPin = ardPin; p.adc = adc; p.pwm = pwm; p.irq = irq; p.func = func;
    return p;
}

// 공통 LED 색상 선택지
QVector<ParamOption> ledColors()
{
    auto c = [](const QString& label, double vf, const char* rgb, double wl) {
        QVariantMap s; s["Vf"] = vf; s["rgb"] = QString::fromLatin1(rgb); s["wl"] = wl;
        return O(label, label, s);
    };
    return {
        c(QStringLiteral("빨강 (Red, 620~630nm)"), 2.0, "#ff2a1a", 625),
        c(QStringLiteral("주황 (Orange, 605nm)"), 2.0, "#ff8a00", 605),
        c(QStringLiteral("노랑 (Yellow, 590nm)"), 2.1, "#ffe000", 590),
        c(QStringLiteral("초록 (Green GaP, 570nm)"), 2.2, "#55ff2a", 570),
        c(QStringLiteral("퓨어 그린 (InGaN, 525nm)"), 3.2, "#00ff80", 525),
        c(QStringLiteral("파랑 (Blue, 470nm)"), 3.2, "#2a6bff", 470),
        c(QStringLiteral("흰색 (Cool White)"), 3.2, "#f4f8ff", 0),
        c(QStringLiteral("웜 화이트 (Warm White)"), 3.1, "#fff0c8", 0),
        c(QStringLiteral("분홍 (Pink)"), 3.2, "#ff6ec7", 0),
        c(QStringLiteral("보라 (UV 395~405nm)"), 3.4, "#9a3bff", 400),
        c(QStringLiteral("적외선 (IR 940nm)"), 1.2, "#5a1010", 940),
    };
}

} // namespace

// ---------------------------------------------------------------------------
// 아두이노 보드
// ---------------------------------------------------------------------------
static PartDef makeUno()
{
    PartDef d;
    d.id = "arduino_uno"; d.category = CAT_BOARD; d.name = QStringLiteral("Arduino Uno R3");
    d.desc = QStringLiteral("ATmega328P, 16MHz, 디지털 14핀(PWM 6), 아날로그 입력 6핀, 5V 동작. 핀당 최대 40mA (권장 20mA).");
    d.prefix = "BOARD"; d.shape = "uno"; d.isBoard = true; d.color = QColor(0, 129, 143);
    d.size = QSizeF(290, 210);
    const double T = 10, B = 200;
    // 상단 헤더
    d.pins << BP("SCL", 90, T, BoardPinKind::Digital, 19, 5, false, -1, "SCL")
           << BP("SDA", 100, T, BoardPinKind::Digital, 18, 4, false, -1, "SDA")
           << BP("AREF", 110, T, BoardPinKind::Aref)
           << BP("GND", 120, T, BoardPinKind::Gnd);
    for (int i = 0; i < 6; ++i) {           // D13..D8
        int n = 13 - i;
        d.pins << BP(QString("D%1").arg(n), 130 + i * 10, T, BoardPinKind::Digital, n, -1,
                     n == 11 || n == 10 || n == 9, -1, n == 13 ? "LED" : (n == 11 ? "MOSI" : (n == 12 ? "MISO" : (n == 10 ? "SS" : ""))));
    }
    for (int i = 0; i < 8; ++i) {           // D7..D0
        int n = 7 - i;
        bool pwm = (n == 6 || n == 5 || n == 3);
        int irq = n == 2 ? 0 : (n == 3 ? 1 : -1);
        QString f = n == 1 ? "TX" : (n == 0 ? "RX" : "");
        d.pins << BP(QString("D%1").arg(n), 200 + i * 10, T, BoardPinKind::Digital, n, -1, pwm, irq, f);
    }
    // 하단 전원 헤더
    d.pins << BP("NC", 100, B, BoardPinKind::NC)
           << BP("IOREF", 110, B, BoardPinKind::IoRef)
           << BP("RESET", 120, B, BoardPinKind::Reset)
           << BP("3.3V", 130, B, BoardPinKind::P3V3)
           << BP("5V", 140, B, BoardPinKind::P5V)
           << BP("GND", 150, B, BoardPinKind::Gnd)
           << BP("GND", 160, B, BoardPinKind::Gnd)
           << BP("VIN", 170, B, BoardPinKind::Vin);
    for (int i = 0; i < 6; ++i)
        d.pins << BP(QString("A%1").arg(i), 190 + i * 10, B, BoardPinKind::Digital, 14 + i, i, false, -1,
                     i == 4 ? "SDA" : (i == 5 ? "SCL" : ""));
    // 내부 연결
    QVector<int> gnds, sda, scl, v5;
    for (int i = 0; i < d.pins.size(); ++i) {
        const PinDef& p = d.pins[i];
        if (p.bkind == BoardPinKind::Gnd) gnds << i;
        if (p.ardPin == 18) sda << i;
        if (p.ardPin == 19) scl << i;
        if (p.bkind == BoardPinKind::P5V || p.bkind == BoardPinKind::IoRef) v5 << i;
    }
    d.internalNets << gnds << sda << scl << v5;
    return d;
}

static PartDef makeNano()
{
    PartDef d;
    d.id = "arduino_nano"; d.category = CAT_BOARD; d.name = QStringLiteral("Arduino Nano (V3)");
    d.desc = QStringLiteral("ATmega328P, 16MHz, Uno 와 동일한 핀 기능 + 아날로그 전용 A6/A7. 브레드보드용 소형 보드.");
    d.prefix = "BOARD"; d.shape = "nano"; d.isBoard = true; d.color = QColor(0, 100, 160);
    d.size = QSizeF(200, 70);
    const double T = 10, B = 60;
    QStringList top = { "D13", "3V3", "AREF", "A0", "A1", "A2", "A3", "A4", "A5", "A6", "A7", "5V", "RST", "GND", "VIN" };
    for (int i = 0; i < top.size(); ++i) {
        const QString& n = top[i];
        double x = 30 + i * 10;
        if (n == "D13") d.pins << BP(n, x, T, BoardPinKind::Digital, 13, -1, false, -1, "LED");
        else if (n == "3V3") d.pins << BP(n, x, T, BoardPinKind::P3V3);
        else if (n == "AREF") d.pins << BP(n, x, T, BoardPinKind::Aref);
        else if (n.startsWith("A")) {
            int a = n.mid(1).toInt();
            bool only = a >= 6;
            d.pins << BP(n, x, T, only ? BoardPinKind::AnalogOnly : BoardPinKind::Digital, 14 + a, a, false, -1,
                         a == 4 ? "SDA" : (a == 5 ? "SCL" : ""));
        }
        else if (n == "5V") d.pins << BP(n, x, T, BoardPinKind::P5V);
        else if (n == "RST") d.pins << BP(n, x, T, BoardPinKind::Reset);
        else if (n == "GND") d.pins << BP(n, x, T, BoardPinKind::Gnd);
        else if (n == "VIN") d.pins << BP(n, x, T, BoardPinKind::Vin);
    }
    QStringList bot = { "D12", "D11", "D10", "D9", "D8", "D7", "D6", "D5", "D4", "D3", "D2", "GND", "RST", "D0", "D1" };
    for (int i = 0; i < bot.size(); ++i) {
        const QString& n = bot[i];
        double x = 30 + i * 10;
        if (n == "GND") d.pins << BP(n, x, B, BoardPinKind::Gnd);
        else if (n == "RST") d.pins << BP(n, x, B, BoardPinKind::Reset);
        else {
            int k = n.mid(1).toInt();
            bool pwm = (k == 3 || k == 5 || k == 6 || k == 9 || k == 10 || k == 11);
            int irq = k == 2 ? 0 : (k == 3 ? 1 : -1);
            d.pins << BP(n, x, B, BoardPinKind::Digital, k, -1, pwm, irq, k == 0 ? "RX" : (k == 1 ? "TX" : ""));
        }
    }
    QVector<int> gnds, rst;
    for (int i = 0; i < d.pins.size(); ++i) {
        if (d.pins[i].bkind == BoardPinKind::Gnd) gnds << i;
        if (d.pins[i].bkind == BoardPinKind::Reset) rst << i;
    }
    d.internalNets << gnds << rst;
    return d;
}

static PartDef makeMega()
{
    PartDef d;
    d.id = "arduino_mega"; d.category = CAT_BOARD; d.name = QStringLiteral("Arduino Mega 2560 R3");
    d.desc = QStringLiteral("ATmega2560, 16MHz, 디지털 54핀(PWM 15), 아날로그 입력 16핀, 외부 인터럽트 6개.");
    d.prefix = "BOARD"; d.shape = "mega"; d.isBoard = true; d.color = QColor(0, 120, 140);
    d.size = QSizeF(410, 210);
    const double T = 10, B = 200;
    auto isPwm = [](int n) { return (n >= 2 && n <= 13) || n == 44 || n == 45 || n == 46; };
    auto irqOf = [](int n) {
        switch (n) { case 2: return 0; case 3: return 1; case 21: return 2; case 20: return 3; case 19: return 4; case 18: return 5; }
        return -1;
    };
    d.pins << BP("SCL", 90, T, BoardPinKind::Digital, 21, -1, false, 2, "SCL")
           << BP("SDA", 100, T, BoardPinKind::Digital, 20, -1, false, 3, "SDA")
           << BP("AREF", 110, T, BoardPinKind::Aref)
           << BP("GND", 120, T, BoardPinKind::Gnd);
    for (int i = 0; i < 6; ++i) { int n = 13 - i; d.pins << BP(QString("D%1").arg(n), 130 + i * 10, T, BoardPinKind::Digital, n, -1, isPwm(n), irqOf(n), n == 13 ? "LED" : ""); }
    for (int i = 0; i < 8; ++i) { int n = 7 - i; d.pins << BP(QString("D%1").arg(n), 200 + i * 10, T, BoardPinKind::Digital, n, -1, isPwm(n), irqOf(n), n == 1 ? "TX0" : (n == 0 ? "RX0" : "")); }
    QStringList comm = { "TX3", "RX3", "TX2", "RX2", "TX1", "RX1", "SDA", "SCL" };
    for (int i = 0; i < 8; ++i) { int n = 14 + i; d.pins << BP(QString("D%1").arg(n), 290 + i * 10, T, BoardPinKind::Digital, n, -1, false, irqOf(n), comm[i]); }
    d.pins << BP("NC", 100, B, BoardPinKind::NC)
           << BP("IOREF", 110, B, BoardPinKind::IoRef)
           << BP("RESET", 120, B, BoardPinKind::Reset)
           << BP("3.3V", 130, B, BoardPinKind::P3V3)
           << BP("5V", 140, B, BoardPinKind::P5V)
           << BP("GND", 150, B, BoardPinKind::Gnd)
           << BP("GND", 160, B, BoardPinKind::Gnd)
           << BP("VIN", 170, B, BoardPinKind::Vin);
    for (int i = 0; i < 8; ++i) d.pins << BP(QString("A%1").arg(i), 190 + i * 10, B, BoardPinKind::Digital, 54 + i, i);
    for (int i = 8; i < 16; ++i) d.pins << BP(QString("A%1").arg(i), 280 + (i - 8) * 10, B, BoardPinKind::Digital, 54 + i, i);
    // 오른쪽 2열 헤더
    d.pins << BP("5V", 380, 20, BoardPinKind::P5V) << BP("5V", 390, 20, BoardPinKind::P5V);
    for (int r = 0; r < 16; ++r) {
        int a = 22 + r * 2, b = a + 1;
        d.pins << BP(QString("D%1").arg(a), 380, 30 + r * 10, BoardPinKind::Digital, a, -1, isPwm(a), -1, a == 50 ? "MISO" : (a == 52 ? "SCK" : ""));
        d.pins << BP(QString("D%1").arg(b), 390, 30 + r * 10, BoardPinKind::Digital, b, -1, isPwm(b), -1, b == 51 ? "MOSI" : (b == 53 ? "SS" : ""));
    }
    d.pins << BP("GND", 380, 190, BoardPinKind::Gnd) << BP("GND", 390, 190, BoardPinKind::Gnd);
    // 44,45,46 PWM 표시 보정
    for (PinDef& p : d.pins) if (p.ardPin >= 0 && p.bkind == BoardPinKind::Digital) p.pwm = isPwm(p.ardPin);
    QVector<int> gnds, v5, sda, scl;
    for (int i = 0; i < d.pins.size(); ++i) {
        const PinDef& p = d.pins[i];
        if (p.bkind == BoardPinKind::Gnd) gnds << i;
        if (p.bkind == BoardPinKind::P5V || p.bkind == BoardPinKind::IoRef) v5 << i;
        if (p.ardPin == 20) sda << i;
        if (p.ardPin == 21) scl << i;
    }
    d.internalNets << gnds << v5 << sda << scl;
    return d;
}

// ---------------------------------------------------------------------------
// 카탈로그 구성
// ---------------------------------------------------------------------------
Catalog& Catalog::mutableInstance()
{
    static Catalog c;
    return c;
}

const Catalog& Catalog::instance()
{
    return mutableInstance();
}

PartDef Catalog::makeCustomPart(const CustomPartSpec& spec)
{
    PartDef d;
    QString key = spec.name.trimmed();
    d.id = "custom:" + key;
    d.category = QStringLiteral("사용자 부품");
    d.name = key.isEmpty() ? QStringLiteral("사용자 센서") : key;
    d.desc = spec.desc.isEmpty() ? QStringLiteral("환경설정에서 추가한 사용자 정의 센서/모듈") : spec.desc;
    d.prefix = "U"; d.varPrefix = "custom"; d.shape = "module"; d.icon = "custom"; d.color = spec.color;
    QVector<PinDef> pins;
    bool sigAssigned = false;
    const QStringList names = spec.pins.split(',', Qt::SkipEmptyParts);
    for (QString n : names) {
        n = n.trimmed();
        if (n.isEmpty()) continue;
        QString u = n.toUpper();
        PinRole role = PinRole::NC;
        if (u == "VCC" || u == "VIN" || u == "+" || u == "5V" || u == "3V3" || u == "V+" || u == "+5V") role = PinRole::Vcc;
        else if (u == "GND" || u == "-" || u == "G") role = PinRole::Gnd;
        else if (u == "SDA") role = PinRole::I2cSda;
        else if (u == "SCL") role = PinRole::I2cScl;
        else if (!sigAssigned) {
            role = spec.output == "analog" ? PinRole::SigAnalogOut : PinRole::SigDigitalOut;
            sigAssigned = true;
        }
        pins.push_back(P(n, 0, 0, role, role == PinRole::NC));
    }
    if (pins.isEmpty()) pins << P("VCC", 0, 0, PinRole::Vcc) << P("GND", 0, 0, PinRole::Gnd) << P("OUT", 0, 0, PinRole::SigAnalogOut);
    moduleLayout(d, qMax(60.0, pins.size() * 10.0 + 30.0), 50, pins);
    d.params << Hidden("vmin", spec.vmin) << Hidden("vmax", spec.vmax);
    ParamDef out; out.key = "output"; out.type = ParamType::Choice; out.def = spec.output; out.hidden = true; d.params << out;
    QString sig;
    for (const PinDef& p : d.pins) if (p.role == PinRole::SigAnalogOut || p.role == PinRole::SigDigitalOut) { sig = p.name; break; }
    if (spec.output == "analog") {
        d.params << Num("level", QStringLiteral("센서 값"), 50, "%", 0, 100, true);
        d.cgGlobal = QString("const int {N}Pin = {P:%1};").arg(sig);
        d.cgRead = "int {N}Value = analogRead({N}Pin);";
        d.cgPrint = "Serial.print(\"{L}=\"); Serial.print({N}Value); Serial.print(\"  \");";
    } else {
        d.params << Bool("state", QStringLiteral("감지 상태"), false);
        d.clickParam = "state";
        d.cgGlobal = QString("const int {N}Pin = {P:%1};").arg(sig);
        d.cgSetup = "pinMode({N}Pin, INPUT);";
        d.cgRead = spec.output == "digital_low" ? "bool {N}State = digitalRead({N}Pin) == LOW;" : "bool {N}State = digitalRead({N}Pin) == HIGH;";
        d.cgPrint = "Serial.print(\"{L}=\"); Serial.print({N}State ? \"ON\" : \"OFF\"); Serial.print(\"  \");";
    }
    return d;
}

void Catalog::setCustomParts(const QVector<CustomPartSpec>& specs)
{
    Catalog& c = mutableInstance();
    // 주의: 이미 배치된 부품이 PartDef 포인터를 들고 있으므로 기존 항목은 지우지 않고 갱신/추가만 한다
    // (QVector 재할당을 막기 위해 충분한 용량을 미리 확보)
    if (c.m_parts.capacity() < c.m_builtinCount + 256) c.m_parts.reserve(c.m_builtinCount + 256);
    for (const CustomPartSpec& s : specs) {
        PartDef d = makeCustomPart(s);
        bool replaced = false;
        for (int i = c.m_builtinCount; i < c.m_parts.size(); ++i) {
            if (c.m_parts[i].id == d.id) { c.m_parts[i] = d; replaced = true; break; }
        }
        if (!replaced && c.m_parts.size() < c.m_parts.capacity()) c.m_parts.push_back(d);
    }
    // 목록에서 삭제된 사용자 부품은 카테고리에서 숨김 처리 (category 비움)
    for (int i = c.m_builtinCount; i < c.m_parts.size(); ++i) {
        bool exists = false;
        for (const CustomPartSpec& s : specs) if ("custom:" + s.name.trimmed() == c.m_parts[i].id) exists = true;
        if (!exists) c.m_parts[i].category.clear();
        else c.m_parts[i].category = QStringLiteral("사용자 부품");
    }
}

const PartDef* Catalog::find(const QString& id) const
{
    for (const PartDef& d : m_parts) if (d.id == id) return &d;
    return nullptr;
}

QStringList Catalog::categories() const
{
    QStringList out;
    for (const PartDef& d : m_parts) if (!d.category.isEmpty() && !out.contains(d.category)) out << d.category;
    return out;
}

Catalog::Catalog()
{
    m_parts << makeUno() << makeNano() << makeMega();

    const QString printSep = QStringLiteral("Serial.print(\"  \");");
    Q_UNUSED(printSep);

    // ===================== 저항·수동 소자 =====================
    {
        PartDef d; d.id = "resistor"; d.category = CAT_PASSIVE; d.name = QStringLiteral("저항 (Resistor)");
        d.desc = QStringLiteral("탄소/금속피막 저항. E24 표준값 1Ω~10MΩ 또는 직접 입력(예: 4.7k, 2M2). 정격 전력 초과 시 경고.");
        d.prefix = "R"; d.varPrefix = "r"; d.shape = "resistor"; d.size = QSizeF(60, 20);
        d.pins << P("1", 0, 10) << P("2", 60, 10);
        ParamDef r = Num("R", QStringLiteral("저항값"), 220, QStringLiteral("Ω"), 0.1, 1e8);
        r.options = suggestions(Units::series(Units::e24(), 1.0, 10e6), QStringLiteral("Ω"));
        d.params << r;
        d.params << Choice("P", QStringLiteral("정격 전력"), {
            O("1/8W", 0.125), O(QStringLiteral("1/4W (일반)"), 0.25), O("1/2W", 0.5), O("1W", 1.0),
            O("2W", 2.0), O(QStringLiteral("3W (시멘트)"), 3.0), O(QStringLiteral("5W (시멘트)"), 5.0) }, 1);
        d.params << Choice("tol", QStringLiteral("허용 오차"), {
            O(QStringLiteral("±1% (금속피막, 갈색띠)"), 1.0), O(QStringLiteral("±5% (탄소피막, 금색띠)"), 5.0),
            O(QStringLiteral("±10% (은색띠)"), 10.0) }, 1);
        m_parts << d;
    }
    {
        PartDef d; d.id = "potentiometer"; d.category = CAT_PASSIVE; d.name = QStringLiteral("가변저항 (Potentiometer)");
        d.desc = QStringLiteral("회전형/트리머/슬라이드 가변저항. 시뮬레이션 중 '노브 위치' 슬라이더 또는 마우스 휠로 조절.");
        d.prefix = "VR"; d.varPrefix = "pot"; d.shape = "pot"; d.size = QSizeF(40, 50);
        d.pins << P("1", 10, 50, PinRole::Vcc) << P("W", 20, 50, PinRole::SigAnalogOut) << P("3", 30, 50, PinRole::Gnd);
        auto m = [](const QString& l, double r, const QString& kind) { QVariantMap s; s["R"] = r; s["kind"] = kind; return O(l, l, s); };
        d.params << Choice("model", QStringLiteral("모델"), {
            m(QStringLiteral("B1K 회전형 (RV09)"), 1e3, "rot"), m(QStringLiteral("B5K 회전형 (RV09)"), 5e3, "rot"),
            m(QStringLiteral("B10K 회전형 (RV16)"), 10e3, "rot"), m(QStringLiteral("B20K 회전형"), 20e3, "rot"),
            m(QStringLiteral("B50K 회전형"), 50e3, "rot"), m(QStringLiteral("B100K 회전형"), 100e3, "rot"),
            m(QStringLiteral("B500K 회전형"), 500e3, "rot"), m(QStringLiteral("B1M 회전형"), 1e6, "rot"),
            m(QStringLiteral("3296W 다회전 트리머 10K"), 10e3, "trim"), m(QStringLiteral("3362P 트리머 1K"), 1e3, "trim"),
            m(QStringLiteral("3362P 트리머 100K"), 100e3, "trim"), m(QStringLiteral("슬라이드 가변저항 10K (45mm)"), 10e3, "slide") }, 2);
        ParamDef r = Num("R", QStringLiteral("전체 저항"), 10e3, QStringLiteral("Ω"), 10, 1e7);
        r.options = suggestions({ 1e3, 2e3, 5e3, 10e3, 20e3, 50e3, 100e3, 200e3, 500e3, 1e6 }, QStringLiteral("Ω"));
        d.params << r;
        d.params << Choice("taper", QStringLiteral("커브"), { O(QStringLiteral("B (선형)"), "B"), O(QStringLiteral("A (로그, 오디오)"), "A") });
        d.params << Num("pos", QStringLiteral("노브 위치"), 50, "%", 0, 100, true);
        d.params << Hidden("kind", "rot");
        d.cgGlobal = "const int {N}Pin = {P:W};";
        d.cgRead = "int {N}Value = analogRead({N}Pin);   // {L}: 0~1023";
        d.cgPrint = "Serial.print(\"{L}=\"); Serial.print({N}Value); Serial.print(\"  \");";
        m_parts << d;
    }
    {
        PartDef d; d.id = "cap_ceramic"; d.category = CAT_PASSIVE; d.name = QStringLiteral("세라믹 커패시터");
        d.desc = QStringLiteral("적층 세라믹/디스크 커패시터 (10pF~10µF). DC 해석에서는 개방 회로로 취급됩니다.");
        d.prefix = "C"; d.shape = "cap_ceramic"; d.size = QSizeF(30, 40);
        d.pins << P("1", 10, 40) << P("2", 20, 40);
        ParamDef c = Num("C", QStringLiteral("용량"), 100e-9, "F", 1e-12, 1e-4);
        c.options = suggestions(Units::series(Units::e6(), 10e-12, 10e-6), "F");
        d.params << c;
        d.params << Choice("V", QStringLiteral("내압"), { O("16V", 16), O("25V", 25), O("50V", 50), O("100V", 100), O("1kV", 1000) }, 2);
        m_parts << d;
    }
    {
        PartDef d; d.id = "cap_elec"; d.category = CAT_PASSIVE; d.name = QStringLiteral("전해 커패시터");
        d.desc = QStringLiteral("알루미늄 전해 커패시터 (0.1µF~4700µF, 극성 있음). DC 해석에서는 개방 회로로 취급됩니다.");
        d.prefix = "C"; d.shape = "cap_elec"; d.size = QSizeF(30, 50);
        d.pins << P("+", 10, 50) << P("-", 20, 50);
        ParamDef c = Num("C", QStringLiteral("용량"), 100e-6, "F", 1e-7, 1e-2);
        c.options = suggestions(Units::series(Units::e6(), 0.1e-6, 4700e-6), "F");
        d.params << c;
        d.params << Choice("V", QStringLiteral("내압"), { O("6.3V", 6.3), O("10V", 10), O("16V", 16), O("25V", 25), O("35V", 35), O("50V", 50), O("63V", 63), O("100V", 100) }, 3);
        m_parts << d;
    }

    // ===================== 반도체 =====================
    {
        PartDef d; d.id = "diode"; d.category = CAT_SEMI; d.name = QStringLiteral("다이오드 / 제너");
        d.desc = QStringLiteral("정류·스위칭·쇼트키·제너 다이오드. A=애노드, K=캐소드(띠).");
        d.prefix = "D"; d.shape = "diode"; d.size = QSizeF(60, 20);
        d.pins << P("A", 0, 10) << P("K", 60, 10);
        auto m = [](const QString& l, double vf, double vz, double imax) { QVariantMap s; s["Vf"] = vf; s["Vz"] = vz; s["If"] = imax; return O(l, l, s); };
        d.params << Choice("model", QStringLiteral("모델"), {
            m(QStringLiteral("1N4148 (소신호 스위칭, 300mA)"), 0.72, 0, 300),
            m(QStringLiteral("1N4001 (정류 50V 1A)"), 0.93, 0, 1000),
            m(QStringLiteral("1N4007 (정류 1000V 1A)"), 0.93, 0, 1000),
            m(QStringLiteral("1N5408 (정류 1000V 3A)"), 0.95, 0, 3000),
            m(QStringLiteral("1N5817 (쇼트키 20V 1A)"), 0.45, 0, 1000),
            m(QStringLiteral("1N5819 (쇼트키 40V 1A)"), 0.6, 0, 1000),
            m(QStringLiteral("SS34 (쇼트키 40V 3A)"), 0.5, 0, 3000),
            m(QStringLiteral("1N4728A 제너 3.3V"), 0.9, 3.3, 270),
            m(QStringLiteral("1N4733A 제너 5.1V"), 0.9, 5.1, 178),
            m(QStringLiteral("1N4742A 제너 12V"), 0.9, 12.0, 76) }, 1);
        d.params << Num("Vf", QStringLiteral("순방향 전압"), 0.93, "V", 0.1, 5);
        d.params << Num("Vz", QStringLiteral("제너 전압 (0=없음)"), 0, "V", 0, 200);
        d.params << Num("If", QStringLiteral("최대 전류"), 1000, "mA", 1, 100000);
        m_parts << d;
    }
    {
        PartDef d; d.id = "npn"; d.category = CAT_SEMI; d.name = QStringLiteral("NPN 트랜지스터");
        d.desc = QStringLiteral("모터/릴레이 구동용 NPN. 핀 순서는 E-B-C 로 표준화. 베이스 저항(보통 1kΩ) 필요.");
        d.prefix = "Q"; d.shape = "to92"; d.size = QSizeF(40, 50);
        d.pins << P("E", 10, 50) << P("B", 20, 50) << P("C", 30, 50);
        auto m = [](const QString& l, double vbe, double beta, double ic, double vce) {
            QVariantMap s; s["Vbe"] = vbe; s["beta"] = beta; s["Ic"] = ic; s["Vce"] = vce; return O(l, l, s); };
        d.params << Choice("model", QStringLiteral("모델"), {
            m(QStringLiteral("2N2222A (40V 800mA)"), 0.7, 100, 800, 0.2),
            m(QStringLiteral("PN2222A (40V 1A)"), 0.7, 100, 1000, 0.2),
            m(QStringLiteral("2N3904 (40V 200mA)"), 0.7, 100, 200, 0.2),
            m(QStringLiteral("BC547 (45V 100mA)"), 0.7, 200, 100, 0.2),
            m(QStringLiteral("S8050 (25V 500mA)"), 0.7, 150, 500, 0.3),
            m(QStringLiteral("TIP120 달링턴 (60V 5A)"), 1.4, 1000, 5000, 0.9),
            m(QStringLiteral("TIP122 달링턴 (100V 5A)"), 1.4, 1000, 5000, 0.9) }, 0);
        d.params << Num("Vbe", "Vbe", 0.7, "V", 0.3, 3);
        d.params << Num("beta", QStringLiteral("hFE (β)"), 100, "", 10, 10000);
        d.params << Num("Ic", QStringLiteral("최대 컬렉터 전류"), 800, "mA", 1, 100000);
        d.params << Num("Vce", QStringLiteral("Vce(sat)"), 0.2, "V", 0.05, 3);
        m_parts << d;
    }
    {
        PartDef d; d.id = "mosfet"; d.category = CAT_SEMI; d.name = QStringLiteral("N채널 MOSFET");
        d.desc = QStringLiteral("모터·LED 스트립 구동용 N-MOSFET. 5V 로직 직접 구동은 로직레벨(IRLZ44N 등) 권장. G-D-S 핀.");
        d.prefix = "Q"; d.shape = "to220"; d.size = QSizeF(40, 60);
        d.pins << P("G", 10, 60) << P("D", 20, 60) << P("S", 30, 60);
        auto m = [](const QString& l, double vth, double rds, double id) { QVariantMap s; s["Vth"] = vth; s["Rds"] = rds; s["Id"] = id; return O(l, l, s); };
        d.params << Choice("model", QStringLiteral("모델"), {
            m(QStringLiteral("IRLZ44N (로직레벨, 55V 47A)"), 1.5, 0.025, 47),
            m(QStringLiteral("IRLB8721 (로직레벨, 30V 62A)"), 1.8, 0.0087, 62),
            m(QStringLiteral("IRF520 (100V 9.7A, 5V구동 부적합)"), 4.0, 0.27, 9.7),
            m(QStringLiteral("IRF540N (100V 33A)"), 3.5, 0.044, 33),
            m(QStringLiteral("IRFZ44N (55V 49A)"), 3.5, 0.0175, 49),
            m(QStringLiteral("2N7000 (TO-92, 60V 200mA)"), 2.1, 5.0, 0.2),
            m(QStringLiteral("AO3400 (SMD, 30V 5.7A)"), 1.05, 0.03, 5.7) }, 0);
        d.params << Num("Vth", QStringLiteral("문턱 전압 Vgs(th)"), 1.5, "V", 0.5, 10);
        d.params << Num("Rds", QStringLiteral("Rds(on)"), 0.025, QStringLiteral("Ω"), 0.001, 100);
        d.params << Num("Id", QStringLiteral("최대 드레인 전류"), 47, "A", 0.01, 500);
        m_parts << d;
    }

    // ===================== LED·디스플레이 =====================
    {
        PartDef d; d.id = "led"; d.category = CAT_LED; d.name = QStringLiteral("LED (발광 다이오드)");
        d.desc = QStringLiteral("3/5/8/10mm LED. 긴 다리=애노드(A,+), 짧은 다리=캐소드(K,-). 반드시 직렬 저항 사용 (자동 추천 지원).");
        d.prefix = "LED"; d.varPrefix = "led"; d.shape = "led"; d.size = QSizeF(30, 50);
        d.pins << P("A", 10, 50, PinRole::LedAnode) << P("K", 20, 50, PinRole::LedCathode);
        d.params << Choice("color", QStringLiteral("색상"), ledColors(), 0);
        d.params << Choice("size", QStringLiteral("크기"), {
            O("3mm", "3mm", { { "If", 20 } }), O(QStringLiteral("5mm (표준)"), "5mm", { { "If", 20 } }),
            O("8mm", "8mm", { { "If", 20 } }), O(QStringLiteral("10mm (고휘도)"), "10mm", { { "If", 30 } }) }, 1);
        d.params << Num("Vf", QStringLiteral("순방향 전압 Vf"), 2.0, "V", 1.0, 4.5);
        d.params << Num("If", QStringLiteral("정격 전류 If"), 20, "mA", 1, 350);
        ParamDef rgb; rgb.key = "rgb"; rgb.type = ParamType::Choice; rgb.def = "#ff2a1a"; rgb.hidden = true; d.params << rgb;
        d.params << Hidden("wl", 625);
        d.cgGlobal = "const int {N}Pin = {P:A};";
        d.cgSetup = "pinMode({N}Pin, OUTPUT);";
        d.cgAct = "digitalWrite({N}Pin, (millis() / 500) % 2 ? HIGH : LOW);   // {L}: 0.5초 간격 깜빡임";
        m_parts << d;
    }
    {
        PartDef d; d.id = "rgb_led"; d.category = CAT_LED; d.name = QStringLiteral("RGB LED (4핀)");
        d.desc = QStringLiteral("공통 캐소드(CC) / 공통 애노드(CA) RGB LED. 핀 순서 R-COM-G-B. 색상별 직렬 저항 필요.");
        d.prefix = "LED"; d.varPrefix = "rgb"; d.shape = "rgb_led"; d.size = QSizeF(50, 50);
        d.pins << P("R", 10, 50, PinRole::LedAnode) << P("COM", 20, 50, PinRole::Gnd)
               << P("G", 30, 50, PinRole::LedAnode) << P("B", 40, 50, PinRole::LedAnode);
        d.params << Choice("type", QStringLiteral("공통 단자"), { O(QStringLiteral("공통 캐소드 (CC, COM→GND)"), "CC"), O(QStringLiteral("공통 애노드 (CA, COM→5V)"), "CA") });
        d.params << Choice("size", QStringLiteral("크기"), { O(QStringLiteral("5mm 확산형"), "5mm"), O(QStringLiteral("10mm 확산형"), "10mm"), O(QStringLiteral("5050 SMD 모듈"), "5050") });
        d.params << Num("VfR", QStringLiteral("Vf 빨강"), 2.0, "V", 1, 4);
        d.params << Num("VfG", QStringLiteral("Vf 초록"), 3.2, "V", 1, 4);
        d.params << Num("VfB", QStringLiteral("Vf 파랑"), 3.2, "V", 1, 4);
        d.params << Num("If", QStringLiteral("정격 전류 If (색상당)"), 20, "mA", 1, 100);
        d.cgGlobal = "const int {N}R = {P:R};\nconst int {N}G = {P:G};\nconst int {N}B = {P:B};";
        d.cgSetup = "pinMode({N}R, OUTPUT);\npinMode({N}G, OUTPUT);\npinMode({N}B, OUTPUT);";
        d.cgAct = "{\n  int h = (millis() / 10) % 768;   // {L}: 색상 순환\n"
                  "  int r = h < 256 ? 255 - h : (h < 512 ? 0 : h - 512);\n"
                  "  int g = h < 256 ? h : (h < 512 ? 511 - h : 0);\n"
                  "  int b = h < 256 ? 0 : (h < 512 ? h - 256 : 767 - h);\n"
                  "  analogWrite({N}R, {CA}r);\n  analogWrite({N}G, {CA}g);\n  analogWrite({N}B, {CA}b);\n}";
        m_parts << d;
    }
    {
        PartDef d; d.id = "seven_seg"; d.category = CAT_LED; d.name = QStringLiteral("7세그먼트 표시기 (1자리)");
        d.desc = QStringLiteral("5161AS(공통 캐소드)/5161BS(공통 애노드) 0.56\" 1자리. 위: G F COM A B / 아래: E D COM C DP. 세그먼트마다 저항 필요.");
        d.prefix = "DS"; d.varPrefix = "seg"; d.shape = "seven_seg"; d.size = QSizeF(60, 100);
        d.pins << P("G", 10, 0, PinRole::LedAnode) << P("F", 20, 0, PinRole::LedAnode) << P("COM", 30, 0, PinRole::Gnd)
               << P("A", 40, 0, PinRole::LedAnode) << P("B", 50, 0, PinRole::LedAnode)
               << P("E", 10, 100, PinRole::LedAnode) << P("D", 20, 100, PinRole::LedAnode) << P("COM2", 30, 100, PinRole::NC)
               << P("C", 40, 100, PinRole::LedAnode) << P("DP", 50, 100, PinRole::LedAnode);
        d.internalNets << QVector<int>{ 2, 7 };
        d.params << Choice("type", QStringLiteral("공통 단자"), { O(QStringLiteral("공통 캐소드 (5161AS)"), "CC"), O(QStringLiteral("공통 애노드 (5161BS)"), "CA") });
        auto c = [](const QString& l, double vf, const char* rgb) { QVariantMap s; s["Vf"] = vf; s["rgb"] = QString::fromLatin1(rgb); return O(l, l, s); };
        d.params << Choice("color", QStringLiteral("색상"), { c(QStringLiteral("빨강"), 2.0, "#ff2a1a"), c(QStringLiteral("초록"), 2.2, "#55ff2a"),
                                                             c(QStringLiteral("파랑"), 3.2, "#3a7bff"), c(QStringLiteral("노랑"), 2.1, "#ffe000"),
                                                             c(QStringLiteral("흰색"), 3.0, "#f4f8ff") });
        d.params << Choice("size", QStringLiteral("크기"), { O("0.56\"", "0.56"), O("0.36\"", "0.36"), O("0.8\"", "0.8") });
        d.params << Num("Vf", QStringLiteral("세그먼트 Vf"), 2.0, "V", 1, 4);
        d.params << Num("If", QStringLiteral("세그먼트 정격 전류"), 20, "mA", 1, 50);
        ParamDef rgb; rgb.key = "rgb"; rgb.type = ParamType::Choice; rgb.def = "#ff2a1a"; rgb.hidden = true; d.params << rgb;
        d.cgGlobal = "const int {N}Pins[8] = {{P:A}, {P:B}, {P:C}, {P:D}, {P:E}, {P:F}, {P:G}, {P:DP}};   // a b c d e f g dp\n"
                     "const byte {N}Font[10] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};";
        d.cgSetup = "for (int i = 0; i < 8; i++) pinMode({N}Pins[i], OUTPUT);";
        d.cgAct = "{\n  byte seg = {N}Font[(millis() / 1000) % 10];   // {L}: 0~9 카운트\n"
                  "  for (int i = 0; i < 8; i++) digitalWrite({N}Pins[i], bitRead(seg, i) ? {ON} : {OFF});\n}";
        m_parts << d;
    }
    {
        PartDef d; d.id = "lcd1602_i2c"; d.category = CAT_LED; d.name = QStringLiteral("LCD 1602 I2C (PCF8574)");
        d.desc = QStringLiteral("16x2 문자 LCD + I2C 백팩. 주소 0x27 또는 0x3F. 라이브러리: LiquidCrystal_I2C");
        d.prefix = "LCD"; d.varPrefix = "lcd"; d.shape = "lcd"; d.size = QSizeF(180, 80);
        d.pins << P("GND", 10, 80, PinRole::Gnd) << P("VCC", 20, 80, PinRole::Vcc)
               << P("SDA", 30, 80, PinRole::I2cSda) << P("SCL", 40, 80, PinRole::I2cScl);
        d.params << Choice("addr", QStringLiteral("I2C 주소"), { O(QStringLiteral("0x27 (PCF8574T)"), "0x27"), O(QStringLiteral("0x3F (PCF8574AT)"), "0x3F") });
        d.params << Choice("backlight", QStringLiteral("백라이트"), { O(QStringLiteral("파랑 (흰 글자)"), "blue"), O(QStringLiteral("노랑초록 (검은 글자)"), "green") });
        d.params << Hidden("cols", 16) << Hidden("rows", 2);
        d.cgInclude = "#include <Wire.h>\n#include <LiquidCrystal_I2C.h>";
        d.cgGlobal = "LiquidCrystal_I2C {N}({A:addr}, {A:cols}, {A:rows});";
        d.cgSetup = "{N}.init();\n{N}.backlight();\n{N}.setCursor(0, 0);\n{N}.print(\"Hello Arduino!\");";
        d.cgPrint = "{N}.setCursor(0, 1);\n{N}.print(\"Time: \");\n{N}.print(millis() / 1000);\n{N}.print(\"s   \");";
        m_parts << d;
    }
    {
        PartDef d = m_parts.last();
        d.id = "lcd2004_i2c"; d.name = QStringLiteral("LCD 2004 I2C (PCF8574)");
        d.desc = QStringLiteral("20x4 문자 LCD + I2C 백팩. 라이브러리: LiquidCrystal_I2C");
        d.size = QSizeF(220, 110);
        for (PinDef& p : d.pins) p.pos.setY(110);
        for (ParamDef& p : d.params) { if (p.key == "cols") p.def = 20; if (p.key == "rows") p.def = 4; }
        m_parts << d;
    }
    {
        PartDef d; d.id = "lcd1602"; d.category = CAT_LED; d.name = QStringLiteral("LCD 1602 (병렬 16핀)");
        d.desc = QStringLiteral("HD44780 호환 16x2 LCD, 4비트 모드. 라이브러리: LiquidCrystal(rs, en, d4, d5, d6, d7). V0(명암)은 GND 또는 가변저항.");
        d.prefix = "LCD"; d.varPrefix = "lcd"; d.shape = "lcd"; d.size = QSizeF(180, 90);
        QStringList names = { "VSS", "VDD", "V0", "RS", "RW", "E", "D0", "D1", "D2", "D3", "D4", "D5", "D6", "D7", "A", "K" };
        QVector<PinRole> roles = { PinRole::Gnd, PinRole::Vcc, PinRole::Gnd, PinRole::SigDigitalIn, PinRole::Gnd, PinRole::SigDigitalIn,
                                   PinRole::NC, PinRole::NC, PinRole::NC, PinRole::NC,
                                   PinRole::SigDigitalIn, PinRole::SigDigitalIn, PinRole::SigDigitalIn, PinRole::SigDigitalIn,
                                   PinRole::Vcc, PinRole::Gnd };
        for (int i = 0; i < names.size(); ++i) d.pins << P(names[i], 10 + i * 10, 90, roles[i]);
        d.params << Choice("backlight", QStringLiteral("백라이트"), { O(QStringLiteral("파랑 (흰 글자)"), "blue"), O(QStringLiteral("노랑초록 (검은 글자)"), "green") });
        d.params << Hidden("cols", 16) << Hidden("rows", 2);
        d.cgInclude = "#include <LiquidCrystal.h>";
        d.cgGlobal = "LiquidCrystal {N}({P:RS}, {P:E}, {P:D4}, {P:D5}, {P:D6}, {P:D7});";
        d.cgSetup = "{N}.begin({A:cols}, {A:rows});\n{N}.print(\"Hello Arduino!\");";
        d.cgPrint = "{N}.setCursor(0, 1);\n{N}.print(\"Time: \");\n{N}.print(millis() / 1000);\n{N}.print(\"s   \");";
        m_parts << d;
    }
    {
        PartDef d; d.id = "neopixel_ring"; d.category = CAT_LED; d.name = QStringLiteral("NeoPixel 링 (WS2812B)");
        d.desc = QStringLiteral("주소 지정 RGB LED 링. 라이브러리: Adafruit_NeoPixel. 픽셀당 최대 60mA.");
        d.prefix = "LED"; d.varPrefix = "ring"; d.shape = "neopixel_ring"; d.size = QSizeF(140, 150);
        d.pins << P("VCC", 60, 150, PinRole::Vcc) << P("DIN", 70, 150, PinRole::SigDigitalIn) << P("GND", 80, 150, PinRole::Gnd);
        d.params << Choice("count", QStringLiteral("픽셀 수"), { O(QStringLiteral("8 픽셀 링"), 8), O(QStringLiteral("12 픽셀 링"), 12), O(QStringLiteral("16 픽셀 링"), 16), O(QStringLiteral("24 픽셀 링"), 24) }, 2);
        d.cgInclude = "#include <Adafruit_NeoPixel.h>";
        d.cgGlobal = "Adafruit_NeoPixel {N}({A:count}, {P:DIN}, NEO_GRB + NEO_KHZ800);";
        d.cgSetup = "{N}.begin();\n{N}.setBrightness(60);";
        d.cgAct = "for (int i = 0; i < {N}.numPixels(); i++) {   // {L}: 무지개 회전\n"
                  "  {N}.setPixelColor(i, {N}.ColorHSV((millis() * 30 + i * 65536L / {N}.numPixels()) % 65536, 255, 255));\n}\n{N}.show();";
        m_parts << d;
    }
    {
        PartDef d = m_parts.last();
        d.id = "neopixel_strip"; d.name = QStringLiteral("NeoPixel 스틱/스트립 (WS2812B)");
        d.desc = QStringLiteral("일렬 주소 지정 RGB LED (8픽셀 스틱 ~ 16픽셀 스트립). 라이브러리: Adafruit_NeoPixel.");
        d.varPrefix = "strip"; d.shape = "neopixel_strip"; d.size = QSizeF(180, 40);
        d.pins.clear();
        d.pins << P("VCC", 10, 40, PinRole::Vcc) << P("DIN", 20, 40, PinRole::SigDigitalIn) << P("GND", 30, 40, PinRole::Gnd);
        d.params.clear();
        d.params << Choice("count", QStringLiteral("픽셀 수"), { O(QStringLiteral("8 픽셀 스틱"), 8), O(QStringLiteral("10 픽셀"), 10), O(QStringLiteral("16 픽셀"), 16) }, 0);
        m_parts << d;
    }

    // ===================== 스위치·입력 =====================
    {
        PartDef d; d.id = "button"; d.category = CAT_INPUT; d.name = QStringLiteral("푸시 버튼 (택트 스위치)");
        d.desc = QStringLiteral("모멘터리 푸시 버튼. 시뮬레이션 중 마우스로 누르면 닫힘. 한쪽은 디지털 핀(INPUT_PULLUP), 다른쪽은 GND.");
        d.prefix = "SW"; d.varPrefix = "btn"; d.shape = "button"; d.size = QSizeF(40, 40);
        d.pins << P("1", 10, 40, PinRole::SwitchToPin) << P("2", 30, 40, PinRole::SwitchToGnd);
        d.params << Choice("model", QStringLiteral("모델"), { O(QStringLiteral("6x6x5mm 택트"), "6x6"), O(QStringLiteral("6x6x13mm 택트 (긴 축)"), "6x6L"),
                                                           O(QStringLiteral("12x12mm 택트 + 캡"), "12x12"), O(QStringLiteral("16mm 판넬 버튼"), "16mm"),
                                                           O(QStringLiteral("30mm 아케이드 버튼"), "arcade") }, 2);
        d.params << Choice("cap", QStringLiteral("캡 색상"), { O(QStringLiteral("빨강"), "#d62828"), O(QStringLiteral("파랑"), "#1d4ed8"), O(QStringLiteral("초록"), "#15803d"),
                                                          O(QStringLiteral("노랑"), "#eab308"), O(QStringLiteral("검정"), "#222222"), O(QStringLiteral("흰색"), "#eeeeee") });
        d.params << Bool("pressed", QStringLiteral("눌림"), false);
        d.clickParam = "pressed"; d.clickMomentary = true;
        d.cgGlobal = "const int {N}Pin = {P:1|2};";
        d.cgSetup = "pinMode({N}Pin, INPUT_PULLUP);";
        d.cgRead = "bool {N}Pressed = digitalRead({N}Pin) == LOW;   // {L}: 누르면 LOW";
        d.cgPrint = "Serial.print(\"{L}=\"); Serial.print({N}Pressed ? \"ON\" : \"OFF\"); Serial.print(\"  \");";
        m_parts << d;
    }
    {
        PartDef d; d.id = "slide_switch"; d.category = CAT_INPUT; d.name = QStringLiteral("슬라이드 스위치 (SPDT)");
        d.desc = QStringLiteral("SS12D00 등 3핀 슬라이드 스위치. 클릭하면 전환. COM 이 1 또는 2 와 연결됨.");
        d.prefix = "SW"; d.varPrefix = "sw"; d.shape = "slide"; d.size = QSizeF(40, 40);
        d.pins << P("1", 10, 40, PinRole::SwitchToGnd) << P("COM", 20, 40, PinRole::SwitchToPin) << P("2", 30, 40, PinRole::NC);
        d.params << Choice("model", QStringLiteral("모델"), { O(QStringLiteral("SS12D00 미니 슬라이드"), "ss12"), O(QStringLiteral("MTS-102 토글 스위치"), "toggle") });
        d.params << Bool("on", QStringLiteral("COM↔1 연결 (ON)"), false);
        d.clickParam = "on";
        d.cgGlobal = "const int {N}Pin = {P:COM};";
        d.cgSetup = "pinMode({N}Pin, INPUT_PULLUP);";
        d.cgRead = "bool {N}On = digitalRead({N}Pin) == LOW;   // {L}";
        d.cgPrint = "Serial.print(\"{L}=\"); Serial.print({N}On ? \"ON\" : \"OFF\"); Serial.print(\"  \");";
        m_parts << d;
    }
    {
        PartDef d; d.id = "joystick"; d.category = CAT_INPUT; d.name = QStringLiteral("조이스틱 모듈 (KY-023)");
        d.desc = QStringLiteral("2축 10kΩ 가변저항 + 누름 스위치. VRx/VRy → 아날로그 핀, SW → 디지털 핀(INPUT_PULLUP).");
        d.prefix = "J"; d.varPrefix = "joy"; d.shape = "joystick"; d.size = QSizeF(70, 80);
        d.pins << P("GND", 10, 80, PinRole::Gnd) << P("+5V", 20, 80, PinRole::Vcc) << P("VRx", 30, 80, PinRole::SigAnalogOut)
               << P("VRy", 40, 80, PinRole::SigAnalogOut) << P("SW", 50, 80, PinRole::SwitchToPin);
        d.params << Num("x", QStringLiteral("X 축"), 50, "%", 0, 100, true);
        d.params << Num("y", QStringLiteral("Y 축"), 50, "%", 0, 100, true);
        d.params << Bool("pressed", QStringLiteral("버튼 눌림"), false);
        d.clickParam = "pressed"; d.clickMomentary = true;
        d.cgGlobal = "const int {N}XPin = {P:VRx};\nconst int {N}YPin = {P:VRy};\nconst int {N}SwPin = {P:SW};";
        d.cgSetup = "pinMode({N}SwPin, INPUT_PULLUP);";
        d.cgRead = "int {N}X = analogRead({N}XPin);\nint {N}Y = analogRead({N}YPin);\nbool {N}Btn = digitalRead({N}SwPin) == LOW;";
        d.cgPrint = "Serial.print(\"{L} X=\"); Serial.print({N}X); Serial.print(\" Y=\"); Serial.print({N}Y); Serial.print({N}Btn ? \" [SW]  \" : \"  \");";
        m_parts << d;
    }
    {
        PartDef d; d.id = "touch"; d.category = CAT_INPUT; d.name = QStringLiteral("터치 센서 (TTP223)");
        d.desc = QStringLiteral("정전식 터치 모듈. 터치하면 SIG=HIGH. 시뮬레이션 중 클릭하고 있으면 터치.");
        d.prefix = "U"; d.varPrefix = "touch"; d.shape = "module"; d.icon = "touch"; d.color = QColor(200, 30, 40);
        moduleLayout(d, 60, 50, { P("SIG", 0, 0, PinRole::SigDigitalOut), P("VCC", 0, 0, PinRole::Vcc), P("GND", 0, 0, PinRole::Gnd) });
        d.params << Bool("touch", QStringLiteral("터치"), false);
        d.clickParam = "touch"; d.clickMomentary = true;
        d.cgGlobal = "const int {N}Pin = {P:SIG};";
        d.cgSetup = "pinMode({N}Pin, INPUT);";
        d.cgRead = "bool {N}Touched = digitalRead({N}Pin) == HIGH;";
        d.cgPrint = "Serial.print(\"{L}=\"); Serial.print({N}Touched ? \"터치\" : \"-\"); Serial.print(\"  \");";
        m_parts << d;
    }
    {
        PartDef d; d.id = "tilt"; d.category = CAT_INPUT; d.name = QStringLiteral("기울기 스위치 (SW-520D)");
        d.desc = QStringLiteral("볼 타입 기울기 스위치. 똑바로 서 있으면 닫힘, 기울면 열림. 클릭으로 기울이기.");
        d.prefix = "SW"; d.varPrefix = "tilt"; d.shape = "tilt"; d.size = QSizeF(30, 40);
        d.pins << P("1", 10, 40, PinRole::SwitchToPin) << P("2", 20, 40, PinRole::SwitchToGnd);
        d.params << Bool("tilted", QStringLiteral("기울어짐"), false);
        d.clickParam = "tilted";
        d.cgGlobal = "const int {N}Pin = {P:1|2};";
        d.cgSetup = "pinMode({N}Pin, INPUT_PULLUP);";
        d.cgRead = "bool {N}Tilted = digitalRead({N}Pin) == HIGH;   // 기울면 스위치가 열려 HIGH";
        d.cgPrint = "Serial.print(\"{L}=\"); Serial.print({N}Tilted ? \"기울어짐\" : \"수평\"); Serial.print(\"  \");";
        m_parts << d;
    }
    {
        PartDef d; d.id = "reed"; d.category = CAT_INPUT; d.name = QStringLiteral("리드 스위치 (자석 감지)");
        d.desc = QStringLiteral("유리관 리드 스위치. 자석이 가까우면 닫힘 (Normally Open). 클릭으로 자석 토글.");
        d.prefix = "SW"; d.varPrefix = "reed"; d.shape = "reed"; d.size = QSizeF(60, 20);
        d.pins << P("1", 0, 10, PinRole::SwitchToPin) << P("2", 60, 10, PinRole::SwitchToGnd);
        d.params << Bool("magnet", QStringLiteral("자석 접근"), false);
        d.clickParam = "magnet";
        d.cgGlobal = "const int {N}Pin = {P:1|2};";
        d.cgSetup = "pinMode({N}Pin, INPUT_PULLUP);";
        d.cgRead = "bool {N}Magnet = digitalRead({N}Pin) == LOW;";
        d.cgPrint = "Serial.print(\"{L}=\"); Serial.print({N}Magnet ? \"자석\" : \"-\"); Serial.print(\"  \");";
        m_parts << d;
    }

    // ===================== 센서 =====================
    {
        PartDef d; d.id = "ldr"; d.category = CAT_SENSOR; d.name = QStringLiteral("조도 센서 CdS (LDR)");
        d.desc = QStringLiteral("광 의존 저항. 밝을수록 저항 감소. 고정 저항과 분압 회로로 아날로그 핀에 연결 (분압 저항 자동 추천).");
        d.prefix = "LDR"; d.varPrefix = "ldr"; d.shape = "ldr"; d.size = QSizeF(30, 40);
        d.pins << P("1", 10, 40, PinRole::DividerHigh) << P("2", 20, 40, PinRole::DividerOut);
        auto m = [](const QString& l, double r10, double gamma, double rdark) {
            QVariantMap s; s["R10"] = r10; s["gamma"] = gamma; s["Rdark"] = rdark; return O(l, l, s); };
        d.params << Choice("model", QStringLiteral("모델"), {
            m(QStringLiteral("GL5506 (2~5kΩ @10lx)"), 3.5e3, 0.6, 0.2e6),
            m(QStringLiteral("GL5516 (5~10kΩ @10lx)"), 7.5e3, 0.7, 0.5e6),
            m(QStringLiteral("GL5528 (10~20kΩ @10lx)"), 15e3, 0.7, 1e6),
            m(QStringLiteral("GL5537-1 (20~30kΩ @10lx)"), 25e3, 0.8, 2e6),
            m(QStringLiteral("GL5539 (30~50kΩ @10lx)"), 40e3, 0.8, 5e6),
            m(QStringLiteral("GL5549 (45~140kΩ @10lx)"), 90e3, 0.8, 10e6) }, 2);
        d.params << Num("R10", QStringLiteral("저항 @10lux"), 15e3, QStringLiteral("Ω"), 100, 1e7);
        d.params << Num("gamma", QStringLiteral("감마 γ"), 0.7, "", 0.3, 1.2);
        d.params << Num("Rdark", QStringLiteral("암저항"), 1e6, QStringLiteral("Ω"), 1e3, 1e8);
        ParamDef lux = Num("lux", QStringLiteral("주변 밝기"), 300, "lux", 0.1, 100000, true); lux.logScale = true;
        d.params << lux;
        d.cgGlobal = "const int {N}Pin = {P:2|1};";
        d.cgRead = "int {N}Light = analogRead({N}Pin);   // {L}: 밝을수록 큰 값";
        d.cgPrint = "Serial.print(\"{L}=\"); Serial.print({N}Light); Serial.print(\"  \");";
        m_parts << d;
    }
    {
        PartDef d; d.id = "ntc"; d.category = CAT_SENSOR; d.name = QStringLiteral("NTC 서미스터");
        d.desc = QStringLiteral("온도가 오르면 저항이 감소. 분압 회로로 아날로그 핀에 연결 (분압 저항 = R25 추천).");
        d.prefix = "TH"; d.varPrefix = "ntc"; d.shape = "ntc"; d.size = QSizeF(30, 40);
        d.pins << P("1", 10, 40, PinRole::DividerHigh) << P("2", 20, 40, PinRole::DividerOut);
        auto m = [](const QString& l, double r25, double b) { QVariantMap s; s["R25"] = r25; s["B"] = b; return O(l, l, s); };
        d.params << Choice("model", QStringLiteral("모델"), {
            m(QStringLiteral("MF52 10K B3950"), 10e3, 3950), m(QStringLiteral("NTC 10K B3435"), 10e3, 3435),
            m(QStringLiteral("NTC 5K B3470"), 5e3, 3470), m(QStringLiteral("NTC 47K B4050"), 47e3, 4050),
            m(QStringLiteral("NTC 100K B3950 (3D프린터)"), 100e3, 3950) }, 0);
        d.params << Num("R25", QStringLiteral("R25 (25°C 저항)"), 10e3, QStringLiteral("Ω"), 100, 1e7);
        d.params << Num("B", QStringLiteral("B 상수"), 3950, "K", 1000, 6000);
        d.params << Num("temp", QStringLiteral("온도"), 25, QStringLiteral("°C"), -40, 125, true);
        d.cgGlobal = "const int {N}Pin = {P:2|1};";
        d.cgRead = "int {N}Raw = analogRead({N}Pin);\n"
                   "float {N}Res = {RDIV} * (1023.0 / max({N}Raw, 1) - 1.0);\n"
                   "float {N}TempC = 1.0 / (1.0 / 298.15 + log({N}Res / {A:R25}) / {A:B}) - 273.15;";
        d.cgPrint = "Serial.print(\"{L}=\"); Serial.print({N}TempC, 1); Serial.print(\"C  \");";
        m_parts << d;
    }
    {
        PartDef d; d.id = "analog_temp"; d.category = CAT_SENSOR; d.name = QStringLiteral("아날로그 온도센서 (LM35 / TMP36)");
        d.desc = QStringLiteral("LM35: 10mV/°C, TMP36: 500mV@0°C + 10mV/°C. TO-92 핀: +Vs, Vout, GND.");
        d.prefix = "U"; d.varPrefix = "temp"; d.shape = "to92"; d.size = QSizeF(40, 50);
        d.pins << P("+Vs", 10, 50, PinRole::Vcc) << P("Vout", 20, 50, PinRole::SigAnalogOut) << P("GND", 30, 50, PinRole::Gnd);
        auto m = [](const QString& l, double off, double slope, double tmin, double tmax) {
            QVariantMap s; s["offset"] = off; s["slope"] = slope; s["tmin"] = tmin; s["tmax"] = tmax; return O(l, l, s); };
        d.params << Choice("model", QStringLiteral("모델"), {
            m(QStringLiteral("LM35DZ (0~100°C, 10mV/°C)"), 0.0, 0.01, 0, 100),
            m(QStringLiteral("LM35CZ (-40~110°C)"), 0.0, 0.01, -40, 110),
            m(QStringLiteral("TMP36 (-40~125°C, 0.5V 오프셋)"), 0.5, 0.01, -40, 125),
            m(QStringLiteral("TMP35 (10~125°C)"), 0.0, 0.01, 10, 125),
            m(QStringLiteral("TMP37 (5~100°C, 20mV/°C)"), 0.0, 0.02, 5, 100) }, 0);
        d.params << Num("temp", QStringLiteral("온도"), 25, QStringLiteral("°C"), -40, 150, true);
        d.params << Hidden("offset", 0.0) << Hidden("slope", 0.01) << Hidden("tmin", 0) << Hidden("tmax", 100);
        d.cgGlobal = "const int {N}Pin = {P:Vout};";
        d.cgRead = "float {N}TempC = (analogRead({N}Pin) * 5.0 / 1023.0 - {A:offset}) / {A:slope};";
        d.cgPrint = "Serial.print(\"{L}=\"); Serial.print({N}TempC, 1); Serial.print(\"C  \");";
        m_parts << d;
    }
    {
        PartDef d; d.id = "dht"; d.category = CAT_SENSOR; d.name = QStringLiteral("온습도 센서 (DHT11/DHT22)");
        d.desc = QStringLiteral("디지털 온습도 센서. 핀: VCC, DATA, NC, GND. 라이브러리: DHT sensor library (DHT dht(pin, DHT22)).");
        d.prefix = "U"; d.varPrefix = "dht"; d.shape = "dht"; d.size = QSizeF(50, 70);
        d.pins << P("VCC", 10, 70, PinRole::Vcc) << P("DATA", 20, 70, PinRole::SigBidir) << P("NC", 30, 70, PinRole::NC) << P("GND", 40, 70, PinRole::Gnd);
        auto m = [](const QString& l, const QString& t, const char* col) { QVariantMap s; s["dhtType"] = t; s["body"] = QString::fromLatin1(col); return O(l, t, s); };
        d.params << Choice("model", QStringLiteral("모델"), {
            m(QStringLiteral("DHT11 (0~50°C ±2°C, 20~90%)"), "DHT11", "#2f7fd8"),
            m(QStringLiteral("DHT22 / AM2302 (-40~80°C ±0.5°C)"), "DHT22", "#f2f2f2"),
            m(QStringLiteral("DHT21 / AM2301 (-40~80°C)"), "DHT21", "#f2f2f2") }, 1);
        d.params << Num("temp", QStringLiteral("온도"), 24, QStringLiteral("°C"), -40, 80, true);
        d.params << Num("hum", QStringLiteral("습도"), 55, "%", 0, 100, true);
        ParamDef t; t.key = "dhtType"; t.type = ParamType::Choice; t.def = "DHT22"; t.hidden = true; d.params << t;
        ParamDef b; b.key = "body"; b.type = ParamType::Choice; b.def = "#f2f2f2"; b.hidden = true; d.params << b;
        d.cgInclude = "#include <DHT.h>";
        d.cgGlobal = "DHT {N}({P:DATA}, {A:dhtType});";
        d.cgSetup = "{N}.begin();";
        d.cgRead = "float {N}Temp = {N}.readTemperature();\nfloat {N}Hum = {N}.readHumidity();";
        d.cgPrint = "Serial.print(\"{L} \"); Serial.print({N}Temp, 1); Serial.print(\"C \"); Serial.print({N}Hum, 0); Serial.print(\"%  \");";
        m_parts << d;
    }
    {
        PartDef d; d.id = "hcsr04"; d.category = CAT_SENSOR; d.name = QStringLiteral("초음파 거리 센서 (HC-SR04)");
        d.desc = QStringLiteral("Trig 에 10µs 펄스 → Echo 펄스폭(µs) / 58 = 거리(cm). pulseIn() 또는 NewPing 라이브러리 사용.");
        d.prefix = "U"; d.varPrefix = "sonar"; d.shape = "hcsr04"; d.size = QSizeF(90, 60);
        d.pins << P("VCC", 30, 60, PinRole::Vcc) << P("Trig", 40, 60, PinRole::SigDigitalIn) << P("Echo", 50, 60, PinRole::SigDigitalOut) << P("GND", 60, 60, PinRole::Gnd);
        auto m = [](const QString& l, double mn, double mx) { QVariantMap s; s["minCm"] = mn; s["maxCm"] = mx; return O(l, l, s); };
        d.params << Choice("model", QStringLiteral("모델"), {
            m(QStringLiteral("HC-SR04 (2~400cm, 5V)"), 2, 400), m(QStringLiteral("HC-SR04P (2~450cm, 3~5.5V)"), 2, 450),
            m(QStringLiteral("US-100 (2~450cm, Trig/Echo 모드)"), 2, 450), m(QStringLiteral("JSN-SR04T 방수형 (25~450cm)"), 25, 450) }, 0);
        d.params << Num("dist", QStringLiteral("물체까지 거리"), 50, "cm", 1, 500, true);
        d.params << Hidden("minCm", 2) << Hidden("maxCm", 400);
        d.cgGlobal = "const int {N}Trig = {P:Trig};\nconst int {N}Echo = {P:Echo};";
        d.cgSetup = "pinMode({N}Trig, OUTPUT);\npinMode({N}Echo, INPUT);";
        d.cgRead = "digitalWrite({N}Trig, LOW);\ndelayMicroseconds(2);\ndigitalWrite({N}Trig, HIGH);\ndelayMicroseconds(10);\n"
                   "digitalWrite({N}Trig, LOW);\nlong {N}Dur = pulseIn({N}Echo, HIGH, 30000);\nfloat {N}Cm = {N}Dur * 0.0343 / 2.0;";
        d.cgPrint = "Serial.print(\"{L}=\"); Serial.print({N}Cm, 1); Serial.print(\"cm  \");";
        m_parts << d;
    }
    {
        PartDef d; d.id = "pir"; d.category = CAT_SENSOR; d.name = QStringLiteral("PIR 인체 감지 센서");
        d.desc = QStringLiteral("적외선 움직임 감지. 감지 시 OUT=HIGH(3.3V). 클릭으로 움직임 토글.");
        d.prefix = "U"; d.varPrefix = "pir"; d.shape = "module"; d.icon = "pir"; d.color = QColor(30, 110, 60);
        moduleLayout(d, 70, 60, { P("VCC", 0, 0, PinRole::Vcc), P("OUT", 0, 0, PinRole::SigDigitalOut), P("GND", 0, 0, PinRole::Gnd) });
        d.params << Choice("model", QStringLiteral("모델"), { O(QStringLiteral("HC-SR501 (3~7m, 지연 조절)"), "HC-SR501"),
                                                           O(QStringLiteral("HC-SR505 미니 (3m)"), "HC-SR505"), O(QStringLiteral("AM312 미니 (3~5m)"), "AM312") });
        d.params << Bool("motion", QStringLiteral("움직임 감지"), false);
        d.clickParam = "motion";
        d.cgGlobal = "const int {N}Pin = {P:OUT};";
        d.cgSetup = "pinMode({N}Pin, INPUT);";
        d.cgRead = "bool {N}Motion = digitalRead({N}Pin) == HIGH;";
        d.cgPrint = "Serial.print(\"{L}=\"); Serial.print({N}Motion ? \"감지\" : \"-\"); Serial.print(\"  \");";
        m_parts << d;
    }
    // 아날로그 출력 모듈 공통 생성기
    auto analogModule = [this](const QString& id, const QString& name, const QString& desc, const QString& var,
                               const QString& icon, QColor col, const QVector<PinDef>& pins, const QString& liveKey,
                               const QString& liveLabel, double liveDef, const QVector<ParamOption>& models,
                               const QString& readComment) {
        PartDef d; d.id = id; d.category = CAT_SENSOR; d.name = name; d.desc = desc; d.prefix = "U"; d.varPrefix = var;
        d.shape = "module"; d.icon = icon; d.color = col;
        moduleLayout(d, qMax(60.0, pins.size() * 10.0 + 30.0), 50, pins);
        if (!models.isEmpty()) d.params << Choice("model", QStringLiteral("모델"), models);
        d.params << Num(liveKey, liveLabel, liveDef, "%", 0, 100, true);
        d.params << Num("thr", QStringLiteral("DO 문턱값 (가변저항)"), 50, "%", 0, 100);
        QString ao = d.pinIndex("AO") >= 0 ? "AO" : (d.pinIndex("S") >= 0 ? "S" : "OUT");
        d.cgGlobal = QString("const int {N}Pin = {P:%1};").arg(ao);
        d.cgRead = QString("int {N}Value = analogRead({N}Pin);   // {L}: %1").arg(readComment);
        d.cgPrint = "Serial.print(\"{L}=\"); Serial.print({N}Value); Serial.print(\"  \");";
        m_parts << d;
    };
    analogModule("sound", QStringLiteral("소리 감지 센서 (KY-038)"), QStringLiteral("마이크 + LM393 비교기. AO=소리 크기(아날로그), DO=문턱값 초과시 HIGH."),
                 "sound", "mic", QColor(30, 70, 160),
                 { P("AO", 0, 0, PinRole::SigAnalogOut), P("GND", 0, 0, PinRole::Gnd), P("VCC", 0, 0, PinRole::Vcc), P("DO", 0, 0, PinRole::SigDigitalOut, true) },
                 "level", QStringLiteral("소리 크기"), 10,
                 { O(QStringLiteral("KY-038 (소형 마이크)"), "KY-038"), O(QStringLiteral("KY-037 (고감도 대형 마이크)"), "KY-037"), O(QStringLiteral("LM393 사운드 모듈"), "LM393") },
                 QStringLiteral("소리가 클수록 큰 값"));
    analogModule("soil", QStringLiteral("토양 수분 센서"), QStringLiteral("FC-28(저항식): 건조할수록 AO 높음. 정전용량식 v1.2: 건조 약 2.9V ~ 젖음 약 1.2V."),
                 "soil", "soil", QColor(40, 40, 120),
                 { P("VCC", 0, 0, PinRole::Vcc), P("GND", 0, 0, PinRole::Gnd), P("DO", 0, 0, PinRole::SigDigitalOut, true), P("AO", 0, 0, PinRole::SigAnalogOut) },
                 "moist", QStringLiteral("토양 수분"), 40,
                 { O(QStringLiteral("FC-28 / YL-69 (저항식)"), "FC-28"), O(QStringLiteral("정전용량식 v1.2"), "CAP") },
                 QStringLiteral("건조할수록 큰 값"));
    analogModule("gas", QStringLiteral("가스 센서 (MQ 시리즈)"), QStringLiteral("MQ 가스 센서 모듈. 히터 전류 약 150mA. AO=농도, DO=문턱값 초과 시 LOW."),
                 "gas", "gas", QColor(20, 90, 50),
                 { P("VCC", 0, 0, PinRole::Vcc), P("GND", 0, 0, PinRole::Gnd), P("DO", 0, 0, PinRole::SigDigitalOut, true), P("AO", 0, 0, PinRole::SigAnalogOut) },
                 "level", QStringLiteral("가스 농도"), 10,
                 { O(QStringLiteral("MQ-2 (LPG, 연기, 수소)"), "MQ-2"), O(QStringLiteral("MQ-3 (알코올)"), "MQ-3"), O(QStringLiteral("MQ-4 (메탄, CNG)"), "MQ-4"),
                   O(QStringLiteral("MQ-5 (LPG, 천연가스)"), "MQ-5"), O(QStringLiteral("MQ-6 (LPG, 부탄)"), "MQ-6"), O(QStringLiteral("MQ-7 (일산화탄소)"), "MQ-7"),
                   O(QStringLiteral("MQ-8 (수소)"), "MQ-8"), O(QStringLiteral("MQ-9 (CO, 가연성 가스)"), "MQ-9"), O(QStringLiteral("MQ-135 (공기질, NH3, CO2)"), "MQ-135") },
                 QStringLiteral("농도가 높을수록 큰 값"));
    analogModule("flame", QStringLiteral("불꽃 감지 센서"), QStringLiteral("IR 수광 다이오드 + LM393. 불꽃이 강할수록 AO 낮음, 감지 시 DO=LOW."),
                 "flame", "flame", QColor(30, 70, 160),
                 { P("AO", 0, 0, PinRole::SigAnalogOut), P("GND", 0, 0, PinRole::Gnd), P("VCC", 0, 0, PinRole::Vcc), P("DO", 0, 0, PinRole::SigDigitalOut, true) },
                 "flame", QStringLiteral("불꽃 세기"), 0,
                 { O(QStringLiteral("KY-026 / 760~1100nm 불꽃 모듈"), "KY-026") }, QStringLiteral("불꽃이 강할수록 작은 값"));
    analogModule("water", QStringLiteral("수위 센서"), QStringLiteral("빗살형 수위 감지. 물에 잠긴 깊이에 비례한 전압(S) 출력."),
                 "water", "water", QColor(170, 30, 30),
                 { P("S", 0, 0, PinRole::SigAnalogOut), P("+", 0, 0, PinRole::Vcc), P("-", 0, 0, PinRole::Gnd) },
                 "level", QStringLiteral("수위"), 30, {}, QStringLiteral("수위가 높을수록 큰 값"));
    analogModule("rain", QStringLiteral("빗물 감지 센서 (FC-37)"), QStringLiteral("빗물 감지판 + LM393. 젖을수록 AO 낮음, 감지 시 DO=LOW."),
                 "rain", "rain", QColor(40, 40, 120),
                 { P("VCC", 0, 0, PinRole::Vcc), P("GND", 0, 0, PinRole::Gnd), P("DO", 0, 0, PinRole::SigDigitalOut, true), P("AO", 0, 0, PinRole::SigAnalogOut) },
                 "wet", QStringLiteral("젖은 정도"), 0, { O(QStringLiteral("FC-37 / YL-83"), "FC-37"), O(QStringLiteral("MH-RD"), "MH-RD") },
                 QStringLiteral("젖을수록 작은 값"));
    {
        PartDef d; d.id = "ir_obstacle"; d.category = CAT_SENSOR; d.name = QStringLiteral("적외선 장애물 감지 (FC-51)");
        d.desc = QStringLiteral("IR LED + 수광부 + LM393. 장애물이 있으면 OUT=LOW. 클릭으로 장애물 토글.");
        d.prefix = "U"; d.varPrefix = "ir"; d.shape = "module"; d.icon = "ir"; d.color = QColor(30, 70, 160);
        moduleLayout(d, 60, 50, { P("OUT", 0, 0, PinRole::SigDigitalOut), P("GND", 0, 0, PinRole::Gnd), P("VCC", 0, 0, PinRole::Vcc) });
        d.params << Choice("model", QStringLiteral("모델"), { O(QStringLiteral("FC-51 (2~30cm)"), "FC-51"), O(QStringLiteral("TCRT5000 라인센서"), "TCRT5000") });
        d.params << Bool("obstacle", QStringLiteral("장애물/검은선 감지"), false);
        d.clickParam = "obstacle";
        d.cgGlobal = "const int {N}Pin = {P:OUT};";
        d.cgSetup = "pinMode({N}Pin, INPUT);";
        d.cgRead = "bool {N}Detected = digitalRead({N}Pin) == LOW;";
        d.cgPrint = "Serial.print(\"{L}=\"); Serial.print({N}Detected ? \"감지\" : \"-\"); Serial.print(\"  \");";
        m_parts << d;
    }
    {
        PartDef d; d.id = "hall"; d.category = CAT_SENSOR; d.name = QStringLiteral("홀 센서 (KY-003 / A3144)");
        d.desc = QStringLiteral("자기장 감지 (오픈 컬렉터 + 풀업). 자석이 있으면 SIG=LOW. 클릭으로 자석 토글.");
        d.prefix = "U"; d.varPrefix = "hall"; d.shape = "module"; d.icon = "hall"; d.color = QColor(20, 20, 20);
        moduleLayout(d, 60, 50, { P("SIG", 0, 0, PinRole::SigDigitalOut), P("VCC", 0, 0, PinRole::Vcc), P("GND", 0, 0, PinRole::Gnd) });
        d.params << Bool("magnet", QStringLiteral("자석 접근"), false);
        d.clickParam = "magnet";
        d.cgGlobal = "const int {N}Pin = {P:SIG};";
        d.cgSetup = "pinMode({N}Pin, INPUT_PULLUP);";
        d.cgRead = "bool {N}Magnet = digitalRead({N}Pin) == LOW;";
        d.cgPrint = "Serial.print(\"{L}=\"); Serial.print({N}Magnet ? \"자석\" : \"-\"); Serial.print(\"  \");";
        m_parts << d;
    }
    {
        PartDef d; d.id = "bme280"; d.category = CAT_SENSOR; d.name = QStringLiteral("기압·온습도 센서 (BME280/BMP280)");
        d.desc = QStringLiteral("I2C 환경 센서 (주소 0x76/0x77). 라이브러리: Adafruit_BME280 / Adafruit_BMP280.");
        d.prefix = "U"; d.varPrefix = "bme"; d.shape = "module"; d.icon = "chip"; d.color = QColor(110, 40, 140);
        moduleLayout(d, 60, 50, { P("VIN", 0, 0, PinRole::Vcc), P("GND", 0, 0, PinRole::Gnd), P("SCL", 0, 0, PinRole::I2cScl), P("SDA", 0, 0, PinRole::I2cSda) });
        d.params << Choice("model", QStringLiteral("모델"), { O(QStringLiteral("BME280 (온도·습도·기압)"), "BME280", { { "cls", "Adafruit_BME280" } }),
                                                           O(QStringLiteral("BMP280 (온도·기압)"), "BMP280", { { "cls", "Adafruit_BMP280" } }) });
        d.params << Choice("addr", QStringLiteral("I2C 주소"), { O("0x76 (SDO=GND)", "0x76"), O("0x77 (SDO=VCC)", "0x77") });
        d.params << Num("temp", QStringLiteral("온도"), 23, QStringLiteral("°C"), -40, 85, true);
        d.params << Num("hum", QStringLiteral("습도"), 45, "%", 0, 100, true);
        d.params << Num("press", QStringLiteral("기압"), 1013.25, "hPa", 300, 1100, true);
        ParamDef c; c.key = "cls"; c.type = ParamType::Choice; c.def = "Adafruit_BME280"; c.hidden = true; d.params << c;
        d.cgInclude = "#include <Wire.h>\n#include <{A:cls}.h>";
        d.cgGlobal = "{A:cls} {N};";
        d.cgSetup = "if (!{N}.begin({A:addr})) Serial.println(\"{L}: 센서를 찾을 수 없습니다\");";
        d.cgRead = "float {N}Temp = {N}.readTemperature();\nfloat {N}Press = {N}.readPressure() / 100.0;";
        d.cgPrint = "Serial.print(\"{L} \"); Serial.print({N}Temp, 1); Serial.print(\"C \"); Serial.print({N}Press, 1); Serial.print(\"hPa  \");";
        m_parts << d;
    }

    // ===================== 모터·출력 =====================
    {
        PartDef d; d.id = "buzzer"; d.category = CAT_OUTPUT; d.name = QStringLiteral("부저 (능동/수동)");
        d.desc = QStringLiteral("능동 부저: HIGH 를 주면 소리. 수동 부저: tone(pin, 주파수) 필요.");
        d.prefix = "BZ"; d.varPrefix = "buzzer"; d.shape = "buzzer"; d.size = QSizeF(40, 50);
        d.pins << P("+", 10, 50, PinRole::SigDigitalIn) << P("-", 30, 50, PinRole::Gnd);
        auto m = [](const QString& l, const QString& v, bool active, double r) { QVariantMap s; s["active"] = active; s["R"] = r; return O(l, v, s); };
        d.params << Choice("model", QStringLiteral("모델"), {
            m(QStringLiteral("능동 부저 5V (12mm, 약 30mA)"), "act5", true, 170),
            m(QStringLiteral("능동 부저 3.3V"), "act3", true, 110),
            m(QStringLiteral("수동 부저 (압전 피에조)"), "pas", false, 2000),
            m(QStringLiteral("수동 부저 (전자기식 16Ω)"), "pas16", false, 16) }, 0);
        d.params << Hidden("active", true) << Hidden("R", 170);
        d.cgGlobal = "const int {N}Pin = {P:+};";
        d.cgSetup = "pinMode({N}Pin, OUTPUT);";
        d.cgAct = "digitalWrite({N}Pin, (millis() / 1000) % 2 ? HIGH : LOW);   // {L}: 1초 간격";
        m_parts << d;
    }
    {
        PartDef d; d.id = "dc_motor"; d.category = CAT_OUTPUT; d.name = QStringLiteral("DC 모터");
        d.desc = QStringLiteral("소형 DC 모터. 아두이노 핀으로 직접 구동 불가 (전류 부족) → 트랜지스터/MOSFET/모터드라이버 사용.");
        d.prefix = "M"; d.shape = "motor"; d.size = QSizeF(50, 60);
        d.pins << P("+", 10, 60) << P("-", 40, 60);
        auto m = [](const QString& l, double r, double v) { QVariantMap s; s["R"] = r; s["Vr"] = v; return O(l, l, s); };
        d.params << Choice("model", QStringLiteral("모델"), {
            m(QStringLiteral("130 모터 (3~6V)"), 8, 5), m(QStringLiteral("TT 기어모터 (3~6V)"), 10, 5),
            m(QStringLiteral("N20 기어모터 6V"), 30, 6), m(QStringLiteral("코인 진동모터 3V"), 40, 3),
            m(QStringLiteral("RS-385 모터 12V"), 4, 12) }, 0);
        d.params << Num("R", QStringLiteral("권선 저항(등가)"), 8, QStringLiteral("Ω"), 0.5, 1000);
        d.params << Num("Vr", QStringLiteral("정격 전압"), 5, "V", 1, 24);
        m_parts << d;
    }
    {
        PartDef d; d.id = "servo"; d.category = CAT_OUTPUT; d.name = QStringLiteral("서보 모터");
        d.desc = QStringLiteral("PWM 각도 제어 서보. 갈색=GND, 빨강=V+, 주황=SIG. 라이브러리: Servo.");
        d.prefix = "M"; d.varPrefix = "servo"; d.shape = "servo"; d.size = QSizeF(90, 60);
        d.pins << P("GND", 10, 60, PinRole::Gnd) << P("V+", 20, 60, PinRole::Vcc) << P("SIG", 30, 60, PinRole::SigPwmIn);
        auto m = [](const QString& l, const QString& v, double range) { QVariantMap s; s["range"] = range; return O(l, v, s); };
        d.params << Choice("model", QStringLiteral("모델"), {
            m(QStringLiteral("SG90 마이크로 9g (1.8kg·cm)"), "SG90", 180), m(QStringLiteral("MG90S 메탈기어 (2.2kg·cm)"), "MG90S", 180),
            m(QStringLiteral("MG996R (11kg·cm)"), "MG996R", 180), m(QStringLiteral("MG995 (10kg·cm)"), "MG995", 180),
            m(QStringLiteral("DS3218 (20kg·cm, 270°)"), "DS3218", 270), m(QStringLiteral("FS90R 360° 연속회전"), "FS90R", 360) }, 0);
        d.params << Hidden("range", 180);
        d.cgInclude = "#include <Servo.h>";
        d.cgGlobal = "Servo {N};";
        d.cgSetup = "{N}.attach({P:SIG});";
        d.cgAct = "{N}.write(abs((int)((millis() / 15) % 360) - 180));   // {L}: 0~180도 왕복";
        m_parts << d;
    }
    {
        PartDef d; d.id = "relay"; d.category = CAT_OUTPUT; d.name = QStringLiteral("릴레이 모듈 (1채널)");
        d.desc = QStringLiteral("SRD-05VDC-SL-C 릴레이 모듈. IN 신호로 COM-NO/NC 접점 전환. 접점은 다른 회로 스위칭용.");
        d.prefix = "RL"; d.varPrefix = "relay"; d.shape = "relay"; d.size = QSizeF(100, 70);
        d.pins << P("VCC", 10, 70, PinRole::Vcc) << P("GND", 20, 70, PinRole::Gnd) << P("IN", 30, 70, PinRole::SigDigitalIn)
               << P("NO", 60, 0, PinRole::NC) << P("COM", 70, 0, PinRole::NC) << P("NC", 80, 0, PinRole::NC);
        d.params << Choice("trigger", QStringLiteral("트리거"), { O(QStringLiteral("HIGH 트리거 (KY-019)"), "HIGH"), O(QStringLiteral("LOW 트리거 (옵토커플러형)"), "LOW") });
        d.params << Choice("rating", QStringLiteral("접점 정격"), { O(QStringLiteral("10A 250VAC / 10A 30VDC"), "10A"), O(QStringLiteral("30A 대전류형"), "30A") });
        d.cgGlobal = "const int {N}Pin = {P:IN};";
        d.cgSetup = "pinMode({N}Pin, OUTPUT);";
        d.cgAct = "digitalWrite({N}Pin, (millis() / 2000) % 2 ? HIGH : LOW);   // {L}: 2초마다 전환";
        m_parts << d;
    }
    {
        PartDef d; d.id = "stepper"; d.category = CAT_OUTPUT; d.name = QStringLiteral("스텝 모터 28BYJ-48 + ULN2003");
        d.desc = QStringLiteral("5V 감속 스텝모터 (1회전 2048스텝, 하프스텝 4096). 라이브러리: Stepper(2048, IN1, IN3, IN2, IN4).");
        d.prefix = "M"; d.varPrefix = "stepper"; d.shape = "stepper"; d.size = QSizeF(120, 90);
        d.pins << P("IN1", 10, 90, PinRole::SigDigitalIn) << P("IN2", 20, 90, PinRole::SigDigitalIn) << P("IN3", 30, 90, PinRole::SigDigitalIn)
               << P("IN4", 40, 90, PinRole::SigDigitalIn) << P("+", 60, 90, PinRole::Vcc) << P("-", 70, 90, PinRole::Gnd);
        d.params << Choice("model", QStringLiteral("모델"), { O(QStringLiteral("28BYJ-48 5V"), "5V"), O(QStringLiteral("28BYJ-48 12V"), "12V") });
        d.params << Hidden("steps", 2048);
        d.cgInclude = "#include <Stepper.h>";
        d.cgGlobal = "Stepper {N}({A:steps}, {P:IN1}, {P:IN3}, {P:IN2}, {P:IN4});";
        d.cgSetup = "{N}.setSpeed(10);   // rpm";
        d.cgAct = "{N}.step(32);   // {L}: 조금씩 회전";
        m_parts << d;
    }

    m_builtinCount = m_parts.size();
    m_parts.reserve(m_builtinCount + 256);   // 사용자 부품 추가 시 포인터 무효화 방지
}
