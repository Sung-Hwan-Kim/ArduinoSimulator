#include "AutoWire.h"
#include "AppSettings.h"
#include "Catalog.h"
#include "CircuitScene.h"
#include "Netlist.h"
#include "PartItem.h"
#include "ResistorAdvisor.h"
#include "Units.h"
#include "WireItem.h"

#include <QDateTime>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>
#include <cmath>

namespace {

PinRole roleOf(PartItem* p, int pin)
{
    const PinDef& pd = p->def()->pins[pin];
    const QString& id = p->def()->id;
    if ((id == "rgb_led" || id == "seven_seg") && pd.name == "COM")
        return p->paramS("type") == "CA" ? PinRole::Vcc : PinRole::Gnd;
    if (pd.optional) return PinRole::NC;
    return pd.role;
}

bool isAutoRole(PinRole r)
{
    switch (r) {
    case PinRole::NC: case PinRole::Passive: case PinRole::Board: return false;
    default: return true;
    }
}

bool isAutoPart(PartItem* p)
{
    if (p->def()->isBoard || p->paramB("_auto")) return false;
    for (int i = 0; i < p->pinCount(); ++i) if (isAutoRole(roleOf(p, i))) return true;
    return false;
}

struct Conn {
    PartItem* part = nullptr;
    int pin = -1;
    PinRole role = PinRole::NC;
    int boardPin = -1;
    bool seriesRes = false;
    bool divider = false;
    ResistorAdvice adv;
};

class Allocator
{
public:
    Allocator(PartItem* board, const Netlist& nl, bool needI2c) : m_board(board), m_bd(board->def())
    {
        // 이미 사용 중인 핀
        int bi = nl.index.value(board, -1);
        for (int i = 0; i < m_bd->pins.size(); ++i) {
            int net = bi >= 0 ? nl.pinNet[bi][i] : -1;
            if (net >= 0 && nl.nonBoardPinCount(net) > 0 && m_bd->pins[i].ardPin >= 0) m_usedArd.insert(m_bd->pins[i].ardPin);
        }
        for (int i = 0; i < m_bd->pins.size(); ++i) {
            const PinDef& pd = m_bd->pins[i];
            if (pd.bkind == BoardPinKind::Gnd && m_gnd < 0 && pd.pos.y() < m_bd->size.height() / 2) m_gnd = i;
            if (pd.bkind == BoardPinKind::P5V && m_v5 < 0) m_v5 = i;
            if (pd.name == "SDA") m_sda = i;
            if (pd.name == "SCL") m_scl = i;
        }
        if (m_gnd < 0) for (int i = 0; i < m_bd->pins.size(); ++i) if (m_bd->pins[i].bkind == BoardPinKind::Gnd) { m_gnd = i; break; }
        if (m_sda < 0) for (int i = 0; i < m_bd->pins.size(); ++i) if (m_bd->pins[i].func == "SDA") { m_sda = i; break; }
        if (m_scl < 0) for (int i = 0; i < m_bd->pins.size(); ++i) if (m_bd->pins[i].func == "SCL") { m_scl = i; break; }
        if (needI2c) {
            if (m_sda >= 0) m_usedArd.insert(m_bd->pins[m_sda].ardPin);
            if (m_scl >= 0) m_usedArd.insert(m_bd->pins[m_scl].ardPin);
        }
        for (int i = 0; i < m_bd->pins.size(); ++i) {
            const PinDef& pd = m_bd->pins[i];
            if (pd.ardPin < 0 || pd.name == "SDA" || pd.name == "SCL") continue;
            if (pd.bkind == BoardPinKind::AnalogOnly) { m_analog << i; continue; }
            if (pd.bkind != BoardPinKind::Digital) continue;
            if (pd.adc >= 0 && pd.name.startsWith('A')) { m_analog << i; m_analogDigital << i; continue; }
            if (pd.ardPin == 0 || pd.ardPin == 1) continue;
            if (pd.pwm) m_pwm << i; else m_digital << i;
        }
        auto byPin = [this](int a, int b) { return m_bd->pins[a].ardPin < m_bd->pins[b].ardPin; };
        std::sort(m_pwm.begin(), m_pwm.end(), byPin);
        std::sort(m_digital.begin(), m_digital.end(), byPin);
        std::sort(m_analogDigital.begin(), m_analogDigital.end(), byPin);
        std::sort(m_analog.begin(), m_analog.end(), [this](int a, int b) { return m_bd->pins[a].adc < m_bd->pins[b].adc; });
    }

    int take(const QVector<const QVector<int>*>& pools)
    {
        for (const QVector<int>* pool : pools)
            for (int i : *pool) {
                int ap = m_bd->pins[i].ardPin;
                if (!m_usedArd.contains(ap)) { m_usedArd.insert(ap); return i; }
            }
        return -1;
    }

    int forRole(PinRole r, QString* note)
    {
        switch (r) {
        case PinRole::Gnd: case PinRole::SwitchToGnd: case PinRole::LedCathode: return m_gnd;
        case PinRole::Vcc: case PinRole::DividerHigh: return m_v5;
        case PinRole::I2cSda: return m_sda;
        case PinRole::I2cScl: return m_scl;
        case PinRole::LedAnode: return take({ &m_pwm, &m_digital, &m_analogDigital });
        case PinRole::SigPwmIn: {
            int p = take({ &m_pwm });
            if (p < 0) { p = take({ &m_digital, &m_analogDigital }); if (note && p >= 0) *note = QStringLiteral("PWM 핀이 부족하여 일반 디지털 핀 사용"); }
            return p;
        }
        case PinRole::SigDigitalIn: case PinRole::SigDigitalOut: case PinRole::SigBidir: case PinRole::SwitchToPin:
            return take({ &m_digital, &m_pwm, &m_analogDigital });
        case PinRole::SigAnalogOut: case PinRole::DividerOut:
            return take({ &m_analog });
        default: return -1;
        }
    }
    int gnd() const { return m_gnd; }

private:
    PartItem* m_board;
    const PartDef* m_bd;
    QSet<int> m_usedArd;
    QVector<int> m_pwm, m_digital, m_analog, m_analogDigital;
    int m_gnd = -1, m_v5 = -1, m_sda = -1, m_scl = -1;
};

QVector<Conn> planConnections(CircuitScene* scene, PartItem* board, const QList<PartItem*>& targets, QStringList& log)
{
    bool needI2c = false;
    for (PartItem* p : targets)
        for (int i = 0; i < p->pinCount(); ++i) {
            PinRole r = roleOf(p, i);
            if (r == PinRole::I2cSda || r == PinRole::I2cScl) needI2c = true;
        }
    Netlist nl = Netlist::build(scene->parts(), scene->wires());
    Allocator alloc(board, nl, needI2c);
    const AppSettings& cfg = AppSettings::instance();
    QVector<Conn> conns;
    for (PartItem* p : targets) {
        for (int i = 0; i < p->pinCount(); ++i) {
            PinRole r = roleOf(p, i);
            if (!isAutoRole(r)) continue;
            Conn c;
            c.part = p; c.pin = i; c.role = r;
            QString note;
            c.boardPin = alloc.forRole(r, &note);
            if (c.boardPin < 0) {
                log << QStringLiteral("%1.%2: 남은 %3 핀이 없어 연결하지 못했습니다.")
                           .arg(p->ref(), p->def()->pins[i].name,
                                (r == PinRole::SigAnalogOut || r == PinRole::DividerOut) ? QStringLiteral("아날로그") : QStringLiteral("디지털"));
                continue;
            }
            if (!note.isEmpty()) log << p->ref() + ": " + note;
            if (r == PinRole::LedAnode && cfg.autoInsertResistors) {
                const QString pn = p->def()->pins[i].name;
                double vf = p->def()->id == "rgb_led" ? p->paramD("Vf" + pn, 2.5) : p->paramD("Vf", 2.0);
                c.adv = ResistorAdvisor::ledSeries(5.0, vf, p->paramD("If", 20), p->ref() + (p->def()->id == "led" ? QString() : "(" + pn + ")"));
                c.seriesRes = c.adv.ok;
            }
            if (r == PinRole::DividerOut && cfg.autoInsertResistors) {
                if (p->def()->id == "ntc") c.adv = ResistorAdvisor::ntcDivider(p->paramD("R25", 10000));
                else c.adv = ResistorAdvisor::ldrDivider(p->paramD("R10", 15000), p->paramD("gamma", 0.7), p->paramD("Rdark", 1e6));
                c.divider = c.adv.ok;
            }
            conns << c;
        }
    }
    return conns;
}

QString boardPinName(PartItem* board, int bp)
{
    return bp >= 0 ? board->def()->pins[bp].name : QStringLiteral("?");
}

PartItem* makeResistor(CircuitScene* scene, const ResistorAdvice& a, const QPointF& pin1At)
{
    PartItem* r = scene->addPart("resistor", QPointF(0, 0));
    r->setParam("R", a.ohms);
    r->setParam("P", qMax(0.25, a.watts));
    r->setParam("_auto", true);
    r->setRotationDeg(90);
    QPointF p1 = r->pinScenePos(0);
    r->moveBy(pin1At.x() - p1.x(), pin1At.y() - p1.y());
    return r;
}

} // namespace

namespace AutoWire {

PartItem* ensureBoard(CircuitScene* scene)
{
    if (PartItem* b = scene->board()) return b;
    QString id = AppSettings::instance().defaultBoard;
    if (!Catalog::instance().find(id)) id = "arduino_uno";
    return scene->addPart(id, QPointF(0, 0));
}

Result placeAndWireAll(CircuitScene* scene)
{
    Result res;
    PartItem* board = ensureBoard(scene);
    board->setRotationDeg(0);

    // 기존 자동 저항 제거, 대상 부품의 전선 제거
    for (PartItem* p : scene->parts()) if (p->paramB("_auto")) scene->removePart(p);
    QList<PartItem*> targets;
    for (PartItem* p : scene->parts()) if (isAutoPart(p)) targets << p;
    if (targets.isEmpty()) { res.log << QStringLiteral("자동 배선할 센서/부품이 없습니다. 부품 목록에서 센서를 추가하세요."); return res; }
    std::sort(targets.begin(), targets.end(), [](PartItem* a, PartItem* b) { return a->scenePos().x() < b->scenePos().x(); });
    for (PartItem* p : targets) {
        const QList<WireItem*> ws = p->wires();
        for (WireItem* w : ws) scene->removeWire(w);
        p->setRotationDeg(0);
    }

    QVector<Conn> conns = planConnections(scene, board, targets, res.log);

    // ---- 레이아웃 ----
    const QRectF br(board->pos(), board->def()->size);
    int lanes = 0;
    bool anyRes = false;
    for (const Conn& c : conns) { lanes += c.divider ? 2 : 1; if (c.seriesRes || c.divider) anyRes = true; }
    const double laneGap = 7.0;
    double rowBottom = br.top() - 30 - lanes * laneGap - 20 - (anyRes ? 180 : 0);
    rowBottom = std::round(rowBottom / 10.0) * 10.0;

    // 각 부품의 슬롯 폭
    QVector<double> widths;
    double total = 0;
    for (PartItem* p : targets) {
        int sidePins = 0;
        for (const Conn& c : conns) {
            if (c.part != p) continue;
            QPointF lp = p->def()->pins[c.pin].pos;
            if (lp.y() < p->def()->size.height() - 0.5) ++sidePins;
        }
        double w = p->def()->size.width() + 40 + sidePins * 10;
        widths << w;
        total += w;
    }
    double x = std::round((br.center().x() - total / 2) / 10.0) * 10.0;
    for (int k = 0; k < targets.size(); ++k) {
        PartItem* p = targets[k];
        p->setPos(x + 20, rowBottom - p->def()->size.height());
        x += widths[k];
    }

    // ---- 배선 ----
    int lane = 0;
    int leftLane = 0, botLane = 0, rightLane = 0;
    auto routeToBoard = [&](const QPointF& s, int bp, QVector<QPointF> bends) -> QVector<QPointF> {
        const PinDef& pd = board->def()->pins[bp];
        QPointF b = board->pinScenePos(bp);
        double chanY = br.top() - 25 - (lane++) * laneGap;
        QPointF last = bends.isEmpty() ? s : bends.last();
        bends << QPointF(last.x(), chanY);
        if (pd.pos.x() >= board->def()->size.width() - 40 && pd.pos.y() > 15 && pd.pos.y() < board->def()->size.height() - 15) {
            double rx = br.right() + 20 + (rightLane++) * laneGap;
            bends << QPointF(rx, chanY) << QPointF(rx, b.y());
        } else if (pd.pos.y() < board->def()->size.height() / 2) {
            bends << QPointF(b.x(), chanY);
        } else {
            double lx = br.left() - 25 - (leftLane++) * laneGap;
            double by = br.bottom() + 20 + (botLane++) * laneGap;
            bends << QPointF(lx, chanY) << QPointF(lx, by) << QPointF(b.x(), by);
        }
        // 중복/일직선 정리
        QVector<QPointF> clean;
        QVector<QPointF> pts; pts << s << bends << b;
        for (const QPointF& pt : pts) {
            if (!clean.isEmpty() && QLineF(clean.last(), pt).length() < 0.5) continue;
            if (clean.size() >= 2) {
                QPointF a = clean[clean.size() - 2], m = clean.last();
                if ((std::fabs(a.x() - m.x()) < 0.5 && std::fabs(m.x() - pt.x()) < 0.5) || (std::fabs(a.y() - m.y()) < 0.5 && std::fabs(m.y() - pt.y()) < 0.5))
                    clean.removeLast();
            }
            clean << pt;
        }
        clean.removeFirst();
        clean.removeLast();
        return clean;
    };

    // 이웃 핀의 저항이 겹치지 않도록 위/아래 단으로 엇갈려 배치
    QVector<QPair<double, int>> placedRes;
    auto resY = [&](double tx) {
        int level = 0;
        for (;;) {
            bool clash = false;
            for (const auto& pr : placedRes)
                if (pr.second == level && std::fabs(pr.first - tx) < 16) clash = true;
            if (!clash || level >= 1) break;
            ++level;
        }
        placedRes.push_back({ tx, level });
        return rowBottom + 20 + level * 75;
    };

    QHash<PartItem*, int> sideIdx;
    for (const Conn& c : conns) {
        PartItem* p = c.part;
        QPointF P = p->pinScenePos(c.pin);
        QPointF lp = p->def()->pins[c.pin].pos;
        QVector<QPointF> pre;
        double tx = P.x();
        if (lp.y() < p->def()->size.height() - 0.5) {
            // 위/옆 핀 → 부품 오른쪽으로 우회
            int j = sideIdx[p]++;
            double right = p->scenePos().x() + p->def()->size.width();
            tx = right + 10 + j * 10;
            if (lp.y() <= 0.5) {
                double up = P.y() - 10 - j * 4;
                pre << QPointF(P.x(), up) << QPointF(tx, up);
            } else {
                pre << QPointF(tx, P.y());
            }
            pre << QPointF(tx, rowBottom);
        }
        QPointF exitPt(tx, rowBottom);
        if (c.seriesRes) {
            PartItem* r = makeResistor(scene, c.adv, QPointF(tx, resY(tx)));
            ++res.resistors;
            QVector<QPointF> b1 = pre;
            if (!b1.isEmpty() && QLineF(b1.last(), r->pinScenePos(0)).length() < 0.5) b1.removeLast();
            if (scene->addWire(p, c.pin, r, 0, b1)) ++res.wires;
            QPointF s = r->pinScenePos(1);
            if (scene->addWire(r, 1, board, c.boardPin, routeToBoard(s, c.boardPin, {}))) ++res.wires;
            res.log << QStringLiteral("%1.%2 → %3 (%4) → %5").arg(p->ref(), p->def()->pins[c.pin].name, r->ref(), Units::format(c.adv.ohms, "Ω"), boardPinName(board, c.boardPin));
        } else if (c.divider) {
            PartItem* r = makeResistor(scene, c.adv, QPointF(tx, resY(tx)));
            ++res.resistors;
            QVector<QPointF> b1 = pre;
            if (!b1.isEmpty() && QLineF(b1.last(), r->pinScenePos(0)).length() < 0.5) b1.removeLast();
            if (scene->addWire(p, c.pin, r, 0, b1)) ++res.wires;
            // 하단 저항 → GND
            int gnd = Allocator(board, Netlist::build(scene->parts(), scene->wires()), false).gnd();
            if (gnd >= 0 && scene->addWire(r, 1, board, gnd, routeToBoard(r->pinScenePos(1), gnd, {}))) ++res.wires;
            // 분압점 → 아날로그 핀 (옆으로 비켜서)
            QPointF s = r->pinScenePos(0);
            QVector<QPointF> stub{ QPointF(s.x() + 10, s.y()) };
            if (scene->addWire(r, 0, board, c.boardPin, routeToBoard(s, c.boardPin, stub))) ++res.wires;
            res.log << QStringLiteral("%1 분압: %2 + %3(%4) → GND, 분압점 → %5").arg(p->ref(), p->def()->pins[c.pin].name, r->ref(), Units::format(c.adv.ohms, "Ω"), boardPinName(board, c.boardPin));
        } else {
            QVector<QPointF> bends = pre;
            if (scene->addWire(p, c.pin, board, c.boardPin, routeToBoard(P, c.boardPin, bends))) ++res.wires;
            res.log << QStringLiteral("%1.%2 → %3").arg(p->ref(), p->def()->pins[c.pin].name, boardPinName(board, c.boardPin));
        }
        Q_UNUSED(exitPt);
    }
    scene->notifyModified();
    return res;
}

Result wireUnconnected(CircuitScene* scene)
{
    Result res;
    PartItem* board = ensureBoard(scene);
    QList<PartItem*> targets;
    for (PartItem* p : scene->parts())
        if (isAutoPart(p) && p->wires().isEmpty()) targets << p;
    if (targets.isEmpty()) { res.log << QStringLiteral("연결되지 않은 센서/부품이 없습니다."); return res; }
    QVector<Conn> conns = planConnections(scene, board, targets, res.log);
    int lane = 0;
    auto simpleRoute = [&](const QPointF& s, const QPointF& b) -> QVector<QPointF> {
        double midY = std::round(((s.y() + b.y()) / 2 + (lane++ % 8) * 6) / 2.0) * 2.0;
        return { QPointF(s.x(), midY), QPointF(b.x(), midY) };
    };
    for (const Conn& c : conns) {
        PartItem* p = c.part;
        QPointF P = p->pinScenePos(c.pin);
        QPointF B = board->pinScenePos(c.boardPin);
        if (c.seriesRes || c.divider) {
            PartItem* r = makeResistor(scene, c.adv, P + QPointF(0, 20));
            ++res.resistors;
            if (scene->addWire(p, c.pin, r, 0, {})) ++res.wires;
            if (c.seriesRes) {
                QPointF s = r->pinScenePos(1);
                if (scene->addWire(r, 1, board, c.boardPin, simpleRoute(s, B))) ++res.wires;
            } else {
                int gnd = Allocator(board, Netlist::build(scene->parts(), scene->wires()), false).gnd();
                if (gnd >= 0 && scene->addWire(r, 1, board, gnd, simpleRoute(r->pinScenePos(1), board->pinScenePos(gnd)))) ++res.wires;
                QPointF s = r->pinScenePos(0) + QPointF(10, 0);
                QVector<QPointF> b{ s };
                b << simpleRoute(s, B);
                if (scene->addWire(r, 0, board, c.boardPin, b)) ++res.wires;
            }
        } else {
            if (scene->addWire(p, c.pin, board, c.boardPin, simpleRoute(P, B))) ++res.wires;
        }
        res.log << QStringLiteral("%1.%2 → %3").arg(p->ref(), p->def()->pins[c.pin].name, boardPinName(board, c.boardPin));
    }
    scene->notifyModified();
    return res;
}

} // namespace AutoWire

// ===========================================================================
// 코드 생성
// ===========================================================================
namespace CodeGen {

namespace {

QString boardSignalLabel(const Netlist& nl, int net)
{
    if (net < 0) return QString();
    for (int bp : nl.boardPinsOnNet(net)) {
        const PinDef* pd = nl.boardPin(bp);
        if (!pd || pd->ardPin < 0) continue;
        if (pd->bkind != BoardPinKind::Digital && pd->bkind != BoardPinKind::AnalogOnly) continue;
        if (pd->adc >= 0) return QString("A%1").arg(pd->adc);
        return QString::number(pd->ardPin);
    }
    return QString();
}

QString resolvePin(const Netlist& nl, PartItem* part, const QString& alts)
{
    for (const QString& name : alts.split('|')) {
        int idx = part->def()->pinIndex(name.trimmed());
        if (idx < 0) continue;
        int net = nl.net(part, idx);
        QString l = boardSignalLabel(nl, net);
        if (!l.isEmpty()) return l;
        // 직렬 저항 하나 건너서
        if (net < 0) continue;
        for (const auto& pp : nl.netPins[net]) {
            PartItem* r = nl.parts[pp.first];
            if (r == part || r->def()->id != "resistor") continue;
            int other = nl.pinNet[pp.first][1 - pp.second];
            l = boardSignalLabel(nl, other);
            if (!l.isEmpty()) return l;
        }
    }
    return QString();
}

QString paramText(const QVariant& v)
{
    bool ok = false;
    double d = v.toDouble(&ok);
    if (ok && v.typeId() != QMetaType::QString) {
        if (std::fabs(d - std::round(d)) < 1e-9) return QString::number((long long)std::llround(d));
        return QString::number(d, 'g', 8);
    }
    QString s = v.toString();
    if (ok && !s.isEmpty()) {
        if (std::fabs(d - std::round(d)) < 1e-9 && !s.startsWith("0x")) return QString::number((long long)std::llround(d));
    }
    return s;
}

double dividerResistor(const Netlist& nl, PartItem* part)
{
    for (int i = 0; i < part->pinCount(); ++i) {
        if (part->def()->pins[i].role != PinRole::DividerOut && part->def()->pins[i].role != PinRole::DividerHigh) continue;
        int net = nl.net(part, i);
        if (net < 0) continue;
        for (const auto& pp : nl.netPins[net]) {
            PartItem* r = nl.parts[pp.first];
            if (r == part || r->def()->id != "resistor") continue;
            if (nl.pinNet[pp.first][1 - pp.second] == 0) return r->paramD("R", 10000);
        }
    }
    return part->paramD("R25", 10000);
}

QString indent(const QString& code, int spaces)
{
    QString pad(spaces, ' ');
    QStringList lines = code.split('\n');
    for (QString& l : lines) if (!l.isEmpty()) l = pad + l;
    return lines.join('\n');
}

} // namespace

QString resolvePinTokens(CircuitScene* scene, const QString& code)
{
    Netlist nl = Netlist::build(scene->parts(), scene->wires());
    QString out = code;
    static const QRegularExpression re(QStringLiteral("\\{PIN:([A-Za-z0-9_]+)\\.([^}]+)\\}"));
    auto it = re.globalMatch(code);
    QHash<QString, QString> repl;
    while (it.hasNext()) {
        auto m = it.next();
        QString ref = m.captured(1), pins = m.captured(2);
        PartItem* part = nullptr;
        for (PartItem* p : scene->parts()) if (p->ref() == ref) part = p;
        QString label = part ? resolvePin(nl, part, pins) : QString();
        repl[m.captured(0)] = label.isEmpty() ? QStringLiteral("-1 /* 미연결 */") : label;
    }
    for (auto r = repl.begin(); r != repl.end(); ++r) out.replace(r.key(), r.value());
    return out;
}

QString generate(CircuitScene* scene, QStringList* notes)
{
    Netlist nl = Netlist::build(scene->parts(), scene->wires());
    PartItem* board = scene->board();
    QStringList includes, globals, setups, reads, acts, prints;
    QList<PartItem*> parts = scene->parts();
    std::sort(parts.begin(), parts.end(), [](PartItem* a, PartItem* b) {
        if (std::fabs(a->scenePos().x() - b->scenePos().x()) > 1) return a->scenePos().x() < b->scenePos().x();
        return a->scenePos().y() < b->scenePos().y();
    });
    static const QRegularExpression tokRe(QStringLiteral("\\{(N|L|CA|ON|OFF|RDIV|P:[^}]+|A:[^}]+)\\}"));

    for (PartItem* p : parts) {
        const PartDef* d = p->def();
        if (d->isBoard) continue;
        QString all = d->cgInclude + d->cgGlobal + d->cgSetup + d->cgRead + d->cgAct + d->cgPrint;
        if (all.isEmpty()) continue;
        QString act = d->cgAct;
        if (d->id == "buzzer" && !p->paramB("active"))
            act = "if ((millis() / 1000) % 2) tone({N}Pin, 440 + ((millis() / 250) % 4) * 110);   // {L}: 수동 부저 멜로디\nelse noTone({N}Pin);";

        bool anyPin = false, missingPin = false;
        auto subst = [&](const QString& tmpl) {
            QString out;
            int last = 0;
            auto it = tokRe.globalMatch(tmpl);
            while (it.hasNext()) {
                auto m = it.next();
                out += tmpl.mid(last, m.capturedStart() - last);
                QString tok = m.captured(1);
                if (tok == "N") out += p->varName();
                else if (tok == "L") out += p->ref();
                else if (tok == "CA") out += p->paramS("type") == "CA" ? "255 - " : "";
                else if (tok == "ON") out += p->paramS("type") == "CA" ? "LOW" : "HIGH";
                else if (tok == "OFF") out += p->paramS("type") == "CA" ? "HIGH" : "LOW";
                else if (tok == "RDIV") out += QString::number(dividerResistor(nl, p), 'f', 1);
                else if (tok.startsWith("P:")) {
                    QString l = resolvePin(nl, p, tok.mid(2));
                    if (l.isEmpty()) { missingPin = true; out += "-1"; }
                    else { anyPin = true; out += l; }
                } else if (tok.startsWith("A:")) out += paramText(p->param(tok.mid(2)));
                last = m.capturedEnd();
            }
            out += tmpl.mid(last);
            return out;
        };
        QString g = subst(d->cgGlobal), s = subst(d->cgSetup), r = subst(d->cgRead), a = subst(act), pr = subst(d->cgPrint), inc = subst(d->cgInclude);
        bool needsPin = (d->cgGlobal + d->cgSetup + d->cgRead + act).contains("{P:");
        bool i2c = d->id.contains("i2c") || d->id == "bme280";
        if (needsPin && !anyPin && !i2c) {
            if (notes) *notes << QStringLiteral("%1 (%2): 아두이노 핀에 연결되지 않아 코드에서 제외").arg(p->ref(), d->name);
            continue;
        }
        if (missingPin && notes) *notes << QStringLiteral("%1: 일부 핀이 연결되지 않음 (-1 로 표시)").arg(p->ref());
        for (const QString& line : inc.split('\n', Qt::SkipEmptyParts)) if (!includes.contains(line)) includes << line;
        if (!g.isEmpty()) globals << QString("// %1 - %2 %3\n%4").arg(p->ref(), d->name, p->specSummary(), g);
        if (!s.isEmpty()) setups << s;
        if (!r.isEmpty()) reads << r;
        if (!a.isEmpty()) acts << a;
        if (!pr.isEmpty()) prints << pr;
    }

    QString code;
    code += QStringLiteral("// ===========================================================\n");
    code += QStringLiteral("// 자동 생성된 스케치 (%1)\n").arg(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm"));
    code += QStringLiteral("// 보드: %1\n").arg(board ? board->def()->name : QStringLiteral("(없음)"));
    code += QStringLiteral("// ===========================================================\n");
    if (!includes.isEmpty()) code += includes.join('\n') + "\n";
    code += "\n";
    if (!globals.isEmpty()) code += globals.join("\n\n") + "\n\n";
    code += "void setup() {\n  Serial.begin(9600);\n";
    if (!setups.isEmpty()) code += indent(setups.join('\n'), 2) + "\n";
    code += QStringLiteral("  Serial.println(\"시뮬레이션 시작!\");\n}\n\n");
    code += "void loop() {\n";
    if (!reads.isEmpty()) code += QStringLiteral("  // ---- 센서/입력 읽기 ----\n") + indent(reads.join('\n'), 2) + "\n\n";
    if (!acts.isEmpty()) code += QStringLiteral("  // ---- 출력 제어 ----\n") + indent(acts.join('\n'), 2) + "\n\n";
    if (!prints.isEmpty()) {
        code += QStringLiteral("  // ---- 0.5초마다 상태 출력 ----\n");
        code += "  static unsigned long lastPrint = 0;\n";
        code += "  if (millis() - lastPrint >= 500) {\n";
        code += "    lastPrint = millis();\n";
        code += indent(prints.join('\n'), 4) + "\n";
        code += "    Serial.println();\n";
        code += "  }\n";
    }
    code += "  delay(10);\n}\n";
    return code;
}

} // namespace CodeGen
