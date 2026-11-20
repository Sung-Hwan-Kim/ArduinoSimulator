#include "ResistorAdvisor.h"
#include "AppSettings.h"
#include "Catalog.h"
#include "CircuitScene.h"
#include "Netlist.h"
#include "PartItem.h"
#include "Units.h"
#include "WireItem.h"

#include <cmath>

namespace ResistorAdvisor {

namespace {

QString ohm(double v) { return Units::format(v, QStringLiteral("Ω")); }

struct LedPinInfo { double vf = 2.0; double ifmA = 20.0; QString what; };

bool ledPinInfo(PartItem* p, int pin, LedPinInfo* info)
{
    const QString& id = p->def()->id;
    const QString name = p->def()->pins[pin].name;
    if (id == "led" && (name == "A" || name == "K")) {
        info->vf = p->paramD("Vf", 2.0); info->ifmA = p->paramD("If", 20); info->what = p->ref();
        return true;
    }
    if (id == "rgb_led" && (name == "R" || name == "G" || name == "B")) {
        info->vf = p->paramD(QString("Vf") + name, 2.5); info->ifmA = p->paramD("If", 20); info->what = p->ref() + "(" + name + ")";
        return true;
    }
    if (id == "seven_seg" && name != "COM" && name != "COM2") {
        info->vf = p->paramD("Vf", 2.0); info->ifmA = p->paramD("If", 20); info->what = p->ref() + " 세그먼트 " + name;
        return true;
    }
    return false;
}

// 넷의 전원 성격: 0=GND, 5=5V/디지털핀, 3.3=3.3V, -1=알수없음
double netSupply(const Netlist& nl, int net)
{
    if (net == 0 && nl.hasGround) return 0.0;
    for (int bp : nl.boardPinsOnNet(net)) {
        const PinDef* pd = nl.boardPin(bp);
        if (!pd) continue;
        if (pd->bkind == BoardPinKind::P3V3) return 3.3;
        if (pd->bkind == BoardPinKind::P5V || pd->bkind == BoardPinKind::IoRef || pd->bkind == BoardPinKind::Vin ||
            pd->bkind == BoardPinKind::Digital) return 5.0;
    }
    return -1.0;
}

bool netHasBoardSignal(const Netlist& nl, int net)
{
    for (int bp : nl.boardPinsOnNet(net)) {
        const PinDef* pd = nl.boardPin(bp);
        if (pd && (pd->bkind == BoardPinKind::Digital || pd->bkind == BoardPinKind::AnalogOnly)) return true;
    }
    return false;
}

bool netHasSupplyOrSignal(const Netlist& nl, int net)
{
    for (int bp : nl.boardPinsOnNet(net)) {
        const PinDef* pd = nl.boardPin(bp);
        if (!pd) continue;
        if (pd->bkind == BoardPinKind::Digital || pd->bkind == BoardPinKind::P5V || pd->bkind == BoardPinKind::P3V3 ||
            pd->bkind == BoardPinKind::Vin || pd->bkind == BoardPinKind::IoRef) return true;
    }
    return false;
}

} // namespace

double roundToSeries(double ohms, bool up)
{
    const auto& s = AppSettings::instance().resistorSeries == "E24" ? Units::e24() : Units::e12();
    return up ? Units::nextStandardUp(ohms, s) : Units::nearestStandard(ohms, s);
}

ResistorAdvice ledSeries(double vs, double vf, double ratedmA, const QString& what)
{
    ResistorAdvice a;
    a.purpose = QStringLiteral("LED 전류 제한");
    double target = std::min(AppSettings::instance().ledTargetCurrent, ratedmA * 0.75);
    if (target <= 0.1) target = 10;
    if (vs <= vf + 0.05) {
        a.reason = QStringLiteral("공급 전압 %1V 가 LED Vf %2V 보다 낮아 LED 를 켤 수 없습니다").arg(vs).arg(vf);
        return a;
    }
    double r = (vs - vf) / (target / 1000.0);
    double rs = roundToSeries(r, true);
    double i = (vs - vf) / rs;
    double p = i * i * rs;
    a.ok = true;
    a.ohms = rs;
    a.watts = Units::standardPowerRating(p, 2.0);
    a.reason = QStringLiteral("%1(%2V - Vf %3V) ÷ %4mA = %5 → 표준값 %6 (실제 %7mA, %8mW → %9)")
                   .arg(what.isEmpty() ? QString() : what + ": ")
                   .arg(vs).arg(vf).arg(target, 0, 'f', 0).arg(ohm(r)).arg(ohm(rs))
                   .arg(i * 1000, 0, 'f', 1).arg(p * 1000, 0, 'f', 0).arg(Units::powerLabel(a.watts));
    return a;
}

ResistorAdvice ldrDivider(double r10, double gamma, double rdark)
{
    ResistorAdvice a;
    a.purpose = QStringLiteral("조도센서 분압");
    double rLow = std::min(rdark, r10);                           // 10 lux
    double rHigh = std::min(rdark, r10 * std::pow(100.0, -gamma)); // 1000 lux
    double r = std::sqrt(rLow * rHigh);
    a.ok = true;
    a.ohms = roundToSeries(r, false);
    a.watts = 0.125;
    a.reason = QStringLiteral("10lux(실내 어두움) %1 · 1000lux(밝은 실내) %2 → √(곱) = %3 → %4 (이 범위에서 감도 최대)")
                   .arg(ohm(rLow)).arg(ohm(rHigh)).arg(ohm(r)).arg(ohm(a.ohms));
    return a;
}

ResistorAdvice ntcDivider(double r25)
{
    ResistorAdvice a;
    a.purpose = QStringLiteral("서미스터 분압");
    a.ok = true;
    a.ohms = roundToSeries(r25, false);
    a.watts = 0.125;
    a.reason = QStringLiteral("분압 저항 = R25(%1) → 25°C 부근에서 출력 2.5V, 감도 최대").arg(ohm(r25));
    return a;
}

ResistorAdvice adviseFor(CircuitScene* scene, PartItem* res)
{
    ResistorAdvice a;
    if (!res || res->def()->id != "resistor") return a;
    Netlist nl = Netlist::build(scene->parts(), scene->wires());
    const AppSettings& cfg = AppSettings::instance();
    int n[2] = { nl.net(res, 0), nl.net(res, 1) };

    for (int side = 0; side < 2; ++side) {
        int here = n[side], other = n[1 - side];
        if (here < 0) continue;
        double otherSupply = netSupply(nl, other);
        for (const auto& pp : nl.netPins[here]) {
            PartItem* p = nl.parts[pp.first];
            if (p == res) continue;
            const PinDef& pd = p->def()->pins[pp.second];
            const QString& id = p->def()->id;
            LedPinInfo li;
            // ---- LED 직렬 ----
            if (ledPinInfo(p, pp.second, &li)) {
                double vs = otherSupply > 0 ? otherSupply : 5.0;
                if (otherSupply == 0.0) {
                    // 캐소드 쪽 저항: 애노드 쪽 전원 확인
                    int an = nl.net(p, p->def()->pinIndex(id == "led" ? "A" : "COM"));
                    double s = netSupply(nl, an);
                    vs = s > 0 ? s : 5.0;
                }
                a = ledSeries(vs, li.vf, li.ifmA, li.what);
                if (id == "seven_seg" && (pd.name == "COM" || pd.name == "COM2"))
                    a.reason += QStringLiteral("\n※ 공통 핀에 저항 1개만 쓰면 켜진 세그먼트 수에 따라 밝기가 달라집니다.");
                return a;
            }
            if (id == "seven_seg" && (pd.name == "COM" || pd.name == "COM2")) {
                a = ledSeries(5.0, p->paramD("Vf", 2.0), p->paramD("If", 20), p->ref());
                a.reason += QStringLiteral("\n※ 공통 핀 저항은 권장하지 않습니다. 세그먼트마다 저항을 넣으세요.");
                return a;
            }
            // ---- 조도센서 / 서미스터 분압 ----
            if (id == "ldr" && (otherSupply == 0.0 || otherSupply > 0)) {
                a = ldrDivider(p->paramD("R10", 15000), p->paramD("gamma", 0.7), p->paramD("Rdark", 1e6));
                a.reason = p->ref() + " " + p->specSummary() + ": " + a.reason;
                return a;
            }
            if (id == "ntc" && (otherSupply == 0.0 || otherSupply > 0)) {
                a = ntcDivider(p->paramD("R25", 10000));
                a.reason = p->ref() + ": " + a.reason;
                return a;
            }
            // ---- 트랜지스터 베이스 ----
            if (id == "npn" && pd.name == "B") {
                // 컬렉터 부하 추정
                int cn = nl.net(p, p->def()->pinIndex("C"));
                double ic = 0.0;
                QStringList loads;
                if (cn >= 0) {
                    for (const auto& q : nl.netPins[cn]) {
                        PartItem* lp = nl.parts[q.first];
                        if (lp == p) continue;
                        const QString& lid = lp->def()->id;
                        if (lid == "dc_motor") { ic += lp->paramD("Vr", 5) / std::max(0.5, lp->paramD("R", 8)) * 1000.0; loads << lp->ref(); }
                        else if (lid == "led") { ic += lp->paramD("If", 20); loads << lp->ref(); }
                        else if (lid == "buzzer") { ic += 5.0 / std::max(1.0, lp->paramD("R", 170)) * 1000.0; loads << lp->ref(); }
                        else if (lid == "resistor" && lp != res) { ic += 5.0 / std::max(1.0, lp->paramD("R", 1000)) * 1000.0; loads << lp->ref(); }
                    }
                }
                bool guessed = ic <= 0.0;
                if (guessed) ic = 100.0;
                ic = std::min(ic, p->paramD("Ic", 800));
                double beta = p->paramD("beta", 100), vbe = p->paramD("Vbe", 0.7);
                double ib = std::max(ic / 20.0, ic / beta * 5.0);   // 확실한 포화를 위해 과구동
                ib = std::min(ib, 15.0);                             // 아두이노 핀 보호
                double r = (5.0 - vbe) / (ib / 1000.0);
                a.ok = true;
                a.purpose = QStringLiteral("트랜지스터 베이스 저항");
                a.ohms = roundToSeries(std::max(100.0, std::min(r, 10000.0)), false);
                a.watts = 0.25;
                a.reason = QStringLiteral("%1 부하 전류 %2mA%3 → 베이스 전류 %4mA (포화 구동) → (5V - Vbe %5V) ÷ Ib = %6 → %7")
                               .arg(p->ref()).arg(ic, 0, 'f', 0).arg(guessed ? QStringLiteral("(추정)") : " (" + loads.join(",") + ")")
                               .arg(ib, 0, 'f', 1).arg(vbe).arg(ohm(r)).arg(ohm(a.ohms));
                return a;
            }
            // ---- MOSFET 게이트 ----
            if (id == "mosfet" && pd.name == "G") {
                a.ok = true;
                a.watts = 0.125;
                if (otherSupply == 0.0) {
                    a.purpose = QStringLiteral("MOSFET 게이트 풀다운");
                    a.ohms = 10000;
                    a.reason = QStringLiteral("게이트-GND 10kΩ: 리셋/부팅 중 게이트가 떠서 MOSFET 이 켜지는 것을 방지");
                } else {
                    a.purpose = QStringLiteral("MOSFET 게이트 저항");
                    a.ohms = 220;
                    a.reason = QStringLiteral("핀-게이트 220Ω: 게이트 충전 돌입전류 제한 (핀 보호)");
                    if (p->paramD("Vth", 2) > 3.0) a.reason += QStringLiteral("\n※ 이 MOSFET 은 Vth 가 높아 5V 로 완전히 켜지지 않습니다. IRLZ44N 같은 로직레벨 MOSFET 권장");
                }
                return a;
            }
            // ---- 스위치 풀업/풀다운 ----
            bool sw = id == "button" || id == "slide_switch" || id == "tilt" || id == "reed" || (id == "joystick" && pd.name == "SW");
            if (sw && (otherSupply == 0.0 || otherSupply > 0)) {
                a.ok = true;
                a.ohms = cfg.pullResistor;
                a.watts = 0.125;
                bool up = otherSupply > 0;
                a.purpose = up ? QStringLiteral("스위치 풀업") : QStringLiteral("스위치 풀다운");
                a.reason = QStringLiteral("%1 입력을 %2 쪽으로 고정 (%3). 누르면 %4 → 전류 %5mA. ※ 저항 없이 pinMode(INPUT_PULLUP) 도 가능")
                               .arg(p->ref()).arg(up ? "HIGH" : "LOW").arg(ohm(a.ohms)).arg(up ? "LOW" : "HIGH")
                               .arg(5.0 / a.ohms * 1000, 0, 'f', 2);
                return a;
            }
            // ---- I2C 풀업 ----
            if ((pd.role == PinRole::I2cSda || pd.role == PinRole::I2cScl || pd.func == "SDA" || pd.func == "SCL") && otherSupply > 0) {
                a.ok = true; a.ohms = 4700; a.watts = 0.125;
                a.purpose = QStringLiteral("I2C 풀업");
                a.reason = QStringLiteral("I2C 표준 풀업 4.7kΩ (100kHz). 대부분의 모듈에는 이미 풀업이 내장되어 있습니다.");
                return a;
            }
        }
    }
    // ---- 일반 풀업/풀다운 (보드 핀 ↔ 전원/GND) ----
    for (int side = 0; side < 2; ++side) {
        if (netHasBoardSignal(nl, n[side])) {
            double os = netSupply(nl, n[1 - side]);
            if (os == 0.0 || os > 0) {
                a.ok = true; a.ohms = cfg.pullResistor; a.watts = 0.125;
                a.purpose = os > 0 ? QStringLiteral("풀업") : QStringLiteral("풀다운");
                a.reason = QStringLiteral("입력 핀 %1 저항: 일반적으로 %2").arg(a.purpose).arg(ohm(a.ohms));
                return a;
            }
        }
    }
    a.reason = QStringLiteral("연결된 부품으로 용도를 판단할 수 없습니다. LED·센서·스위치·트랜지스터와 연결한 뒤 다시 시도하세요.");
    return a;
}

QString ledHint(CircuitScene* scene, PartItem* p)
{
    const QString& id = p->def()->id;
    if (id != "led" && id != "rgb_led" && id != "seven_seg") return QString();
    QStringList lines;
    if (id == "led") {
        ResistorAdvice a5 = ledSeries(5.0, p->paramD("Vf", 2), p->paramD("If", 20));
        ResistorAdvice a3 = ledSeries(3.3, p->paramD("Vf", 2), p->paramD("If", 20));
        lines << QStringLiteral("권장 직렬 저항: 5V → <b>%1</b> (%2)").arg(ohm(a5.ohms), Units::powerLabel(a5.watts));
        if (a3.ok) lines << QStringLiteral("3.3V → <b>%1</b>").arg(ohm(a3.ohms));
        else lines << QStringLiteral("3.3V 로는 구동 불가 (Vf 가 높음)");
    } else if (id == "rgb_led") {
        for (const char* c : { "R", "G", "B" }) {
            ResistorAdvice a = ledSeries(5.0, p->paramD(QString("Vf") + c, 2.5), p->paramD("If", 20));
            lines << QStringLiteral("%1: <b>%2</b>").arg(c).arg(ohm(a.ohms));
        }
        lines = QStringList{ QStringLiteral("권장 직렬 저항 (5V): ") + lines.join(", ") };
    } else {
        ResistorAdvice a = ledSeries(5.0, p->paramD("Vf", 2), p->paramD("If", 20));
        lines << QStringLiteral("세그먼트마다 <b>%1</b> 저항 (8개) 권장").arg(ohm(a.ohms));
    }
    // 저항 누락 검사
    if (scene) {
        Netlist nl = Netlist::build(scene->parts(), scene->wires());
        QStringList missing;
        for (int i = 0; i < p->pinCount(); ++i) {
            LedPinInfo li;
            if (!ledPinInfo(p, i, &li)) continue;
            if (p->def()->pins[i].name == "K") continue;
            int net = nl.net(p, i);
            if (net > 0 && netHasSupplyOrSignal(nl, net)) missing << p->def()->pins[i].name;
        }
        if (!missing.isEmpty())
            lines << QStringLiteral("<span style='color:#c00'>⚠ 직렬 저항 없이 연결된 핀: %1 → [저항 자동 삽입] 버튼 사용</span>").arg(missing.join(", "));
    }
    return lines.join("<br>");
}

QString partHint(PartItem* p)
{
    const QString& id = p->def()->id;
    if (id == "ldr") {
        ResistorAdvice a = ldrDivider(p->paramD("R10", 15000), p->paramD("gamma", 0.7), p->paramD("Rdark", 1e6));
        return QStringLiteral("분압 저항 추천: <b>%1</b><br><small>%2</small>").arg(ohm(a.ohms), a.reason);
    }
    if (id == "ntc") {
        ResistorAdvice a = ntcDivider(p->paramD("R25", 10000));
        return QStringLiteral("분압 저항 추천: <b>%1</b><br><small>%2</small>").arg(ohm(a.ohms), a.reason);
    }
    if (id == "button" || id == "tilt" || id == "reed" || id == "slide_switch")
        return QStringLiteral("풀업/풀다운 저항: <b>%1</b> 또는 pinMode(핀, INPUT_PULLUP) 사용 (저항 불필요)").arg(ohm(AppSettings::instance().pullResistor));
    if (id == "npn")
        return QStringLiteral("베이스 저항: 보통 <b>1kΩ</b> (부하 전류에 따라 자동 계산 — 저항 선택 후 [추천값 적용])<br>모터/릴레이 코일에는 역기전력 다이오드(1N4007) 병렬 연결");
    if (id == "mosfet")
        return QStringLiteral("게이트 저항 <b>220Ω</b> + 게이트-GND 풀다운 <b>10kΩ</b> 권장");
    if (id == "dc_motor")
        return QStringLiteral("아두이노 핀으로 직접 구동 금지 → NPN(TIP120)/MOSFET + 다이오드(1N4007) 사용");
    if (id == "buzzer" && p->paramD("R", 170) < 50)
        return QStringLiteral("전자기식 수동 부저는 코일 저항이 작아 <b>100Ω</b> 직렬 저항 또는 트랜지스터 구동 권장");
    return QString();
}

int insertMissingLedResistors(CircuitScene* scene, PartItem* part, QStringList* report)
{
    int inserted = 0;
    for (int i = 0; i < part->pinCount(); ++i) {
        LedPinInfo li;
        if (!ledPinInfo(part, i, &li) || part->def()->pins[i].name == "K") continue;
        Netlist nl = Netlist::build(scene->parts(), scene->wires());
        int net = nl.net(part, i);
        if (net <= 0 || !netHasSupplyOrSignal(nl, net)) continue;
        double vs = netSupply(nl, net);
        ResistorAdvice a = ledSeries(vs > 0 ? vs : 5.0, li.vf, li.ifmA, li.what);
        if (!a.ok) continue;

        // 이 핀에 붙은 전선을 저항 쪽으로 옮김
        QList<WireItem*> ws;
        for (WireItem* w : part->wires())
            if ((w->partA() == part && w->pinA() == i) || (w->partB() == part && w->pinB() == i)) ws << w;
        if (ws.isEmpty()) continue;

        QPointF pinPos = part->pinScenePos(i);
        PartItem* r = scene->addPart("resistor", QPointF(0, 0));
        r->setParam("R", a.ohms);
        r->setParam("P", a.watts);
        r->setParam("_auto", true);
        r->setRotationDeg(90);
        // 회전 후 핀1 이 LED 핀 아래 20px 에 오도록
        QPointF p1 = r->pinScenePos(0);
        r->moveBy(pinPos.x() - p1.x(), pinPos.y() + 20 - p1.y());
        for (WireItem* w : ws) {
            PartItem* op = w->partA() == part && w->pinA() == i ? w->partB() : w->partA();
            int opin = w->partA() == part && w->pinA() == i ? w->pinB() : w->pinA();
            QColor col = w->color();
            scene->removeWire(w);
            QPointF s = r->pinScenePos(1), e = op->pinScenePos(opin);
            scene->addWire(r, 1, op, opin, { QPointF(s.x(), (s.y() + e.y()) / 2), QPointF(e.x(), (s.y() + e.y()) / 2) }, col);
        }
        scene->addWire(part, i, r, 0, {});
        ++inserted;
        if (report) *report << QStringLiteral("%1 삽입: %2 %3 ← %4").arg(r->ref(), ohm(a.ohms), Units::powerLabel(a.watts), a.reason);
    }
    return inserted;
}

int applyAll(CircuitScene* scene, QStringList* report)
{
    int changed = 0;
    for (PartItem* p : scene->parts()) {
        if (p->def()->id != "resistor") continue;
        ResistorAdvice a = adviseFor(scene, p);
        if (!a.ok) {
            if (report) *report << QStringLiteral("%1: 변경 없음 — %2").arg(p->ref(), a.reason);
            continue;
        }
        double old = p->paramD("R");
        p->setParam("R", a.ohms);
        if (p->paramD("P", 0.25) < a.watts) p->setParam("P", a.watts);
        if (std::fabs(old - a.ohms) > 1e-6) ++changed;
        if (report) *report << QStringLiteral("%1 [%2]: %3 → %4 ← %5").arg(p->ref(), a.purpose, ohm(old), ohm(a.ohms), a.reason);
    }
    const QList<PartItem*> ps = scene->parts();
    for (PartItem* p : ps) {
        const QString& id = p->def()->id;
        if (id == "led" || id == "rgb_led" || id == "seven_seg") changed += insertMissingLedResistors(scene, p, report);
    }
    scene->notifyModified();
    return changed;
}

} // namespace ResistorAdvisor
