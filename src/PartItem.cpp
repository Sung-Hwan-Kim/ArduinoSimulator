#include "PartItem.h"
#include "CircuitScene.h"
#include "Theme.h"
#include "Units.h"
#include "WireItem.h"

#include <QGraphicsSceneHoverEvent>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsSceneWheelEvent>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <QStyleOptionGraphicsItem>
#include <QUuid>
#include <cmath>

namespace {
const QColor kLeg(170, 170, 175);
const QColor kHeader(25, 25, 25);

QFont pixelFont(int px, bool bold = false)
{
    return Theme::pixelFont(px, bold);
}

QColor bandColor(int digit)
{
    static const QColor c[] = { QColor(0, 0, 0), QColor(140, 70, 20), QColor(220, 20, 20), QColor(255, 130, 0),
                                QColor(250, 220, 0), QColor(20, 160, 40), QColor(20, 70, 220), QColor(140, 40, 200),
                                QColor(130, 130, 130), QColor(250, 250, 250) };
    return (digit >= 0 && digit <= 9) ? c[digit] : QColor(0, 0, 0);
}
} // namespace

PartItem::PartItem(const PartDef* def, const QString& ref)
    : m_def(def), m_ref(ref)
{
    m_uid = QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
    m_params = def->defaultParams();
    setFlags(ItemIsMovable | ItemIsSelectable | ItemSendsGeometryChanges);
    setAcceptHoverEvents(true);
    setTransformOriginPoint(def->size.width() / 2.0, def->size.height() / 2.0);
    setZValue(def->isBoard ? 0 : 10);
}

PartItem::~PartItem() = default;

QString PartItem::varName() const
{
    QString base = m_def->varPrefix.isEmpty() ? m_def->prefix.toLower() : m_def->varPrefix;
    QString num;
    for (QChar c : m_ref) if (c.isDigit()) num += c;
    return base + (num.isEmpty() ? QStringLiteral("1") : num);
}

void PartItem::setParams(const QVariantMap& p)
{
    for (auto it = p.begin(); it != p.end(); ++it) m_params[it.key()] = it.value();
    update();
}

double PartItem::paramD(const QString& key, double def) const
{
    auto it = m_params.find(key);
    if (it == m_params.end()) return def;
    bool ok = false;
    double v = it.value().toDouble(&ok);
    return ok ? v : def;
}

QStringList PartItem::setParam(const QString& key, const QVariant& v)
{
    QStringList changed;
    m_params[key] = v;
    changed << key;
    if (const ParamDef* pd = m_def->param(key)) {
        if (pd->type == ParamType::Choice) {
            for (const ParamOption& o : pd->options) {
                if (o.value == v) {
                    for (auto it = o.sets.begin(); it != o.sets.end(); ++it) {
                        m_params[it.key()] = it.value();
                        changed << it.key();
                    }
                }
            }
        }
    }
    update();
    return changed;
}

QString PartItem::specSummary() const
{
    const QString id = m_def->id;
    if (id == "resistor")
        return Units::format(paramD("R"), QStringLiteral("Ω")) + " " + Units::powerLabel(paramD("P", 0.25));
    if (id == "led")
        return paramS("size") + " " + paramS("color").section(' ', 0, 0) + QString(" Vf%1V").arg(paramD("Vf"));
    if (id == "potentiometer") return Units::format(paramD("R"), QStringLiteral("Ω"));
    if (id == "cap_ceramic" || id == "cap_elec") return Units::format(paramD("C"), "F");
    const ParamDef* m = m_def->param("model");
    if (m) {
        QString v = paramS("model");
        for (const ParamOption& o : m->options) if (o.value.toString() == v) return o.label;
        return v;
    }
    return QString();
}

// ---------------------------------------------------------------------------
// 핀 / 기하
// ---------------------------------------------------------------------------
QRectF PartItem::bodyRect() const
{
    QSizeF s = m_def->size;
    QRectF r(0, 0, s.width(), s.height());
    if (m_def->isBoard) return r;
    // 다리가 있는 쪽을 몸체에서 제외
    bool top = false, bottom = false, left = false, right = false;
    for (const PinDef& pin : m_def->pins) {
        if (pin.pos.y() >= s.height() - 0.5) bottom = true;
        else if (pin.pos.y() <= 0.5) top = true;
        else if (pin.pos.x() <= 0.5) left = true;
        else if (pin.pos.x() >= s.width() - 0.5) right = true;
    }
    if (bottom) r.setBottom(r.bottom() - 10);
    if (top) r.setTop(r.top() + 10);
    if (left) r.setLeft(r.left() + 10);
    if (right) r.setRight(r.right() - 10);
    return r;
}

QRectF PartItem::boundingRect() const
{
    return QRectF(QPointF(0, 0), m_def->size).adjusted(-8, -16, 8, 8);
}

QPainterPath PartItem::shape() const
{
    QPainterPath path;
    path.addRect(QRectF(QPointF(0, 0), m_def->size).adjusted(-4, -4, 4, 4));
    return path;
}

QPointF PartItem::pinScenePos(int idx) const
{
    if (idx < 0 || idx >= m_def->pins.size()) return scenePos();
    return mapToScene(m_def->pins[idx].pos);
}

int PartItem::pinAt(const QPointF& scenePos, double radius) const
{
    QPointF local = mapFromScene(scenePos);
    int best = -1;
    double bestD = radius * radius;
    for (int i = 0; i < m_def->pins.size(); ++i) {
        QPointF d = m_def->pins[i].pos - local;
        double dd = d.x() * d.x() + d.y() * d.y();
        if (dd <= bestD) { bestD = dd; best = i; }
    }
    return best;
}

void PartItem::setHoverPin(int idx)
{
    if (idx == m_hoverPin) return;
    m_hoverPin = idx;
    update();
}

bool PartItem::pinHasWire(int pin) const
{
    for (WireItem* w : m_wires)
        if ((w->partA() == this && w->pinA() == pin) || (w->partB() == this && w->pinB() == pin)) return true;
    return false;
}

void PartItem::rotate90()
{
    setRotationDeg((m_rot + 90) % 360);
}

void PartItem::setRotationDeg(int deg)
{
    m_rot = ((deg % 360) + 360) % 360;
    setRotation(m_rot);
}

void PartItem::setSimulating(bool on)
{
    m_simulating = on;
    setFlag(ItemIsMovable, !on);
    if (!on) m_sim.clear();
    setCursor(on && !m_def->clickParam.isEmpty() ? Qt::PointingHandCursor : Qt::ArrowCursor);
    update();
}

void PartItem::setSimState(const QVariantMap& s)
{
    m_sim = s;
    double speed = s.value("motor").toDouble();
    if (speed != 0.0) m_anim += speed * 25.0;
    update();
}

QVariant PartItem::itemChange(GraphicsItemChange change, const QVariant& value)
{
    if (change == ItemPositionChange && scene()) {
        auto* cs = qobject_cast<CircuitScene*>(scene());
        if (cs && cs->snapToGrid()) {
            QPointF p = value.toPointF();
            const double g = CircuitScene::kGrid;
            return QPointF(std::round(p.x() / g) * g, std::round(p.y() / g) * g);
        }
    }
    if (change == ItemPositionHasChanged || change == ItemRotationHasChanged || change == ItemTransformHasChanged) {
        for (WireItem* w : m_wires) w->updatePath();
        if (auto* cs = qobject_cast<CircuitScene*>(scene())) cs->notifyModified();
    }
    return QGraphicsItem::itemChange(change, value);
}

void PartItem::hoverMoveEvent(QGraphicsSceneHoverEvent* e)
{
    int pin = pinAt(e->scenePos(), 7);
    setHoverPin(pin);
    if (auto* cs = qobject_cast<CircuitScene*>(scene())) {
        if (pin >= 0) setToolTip(cs->pinToolTip(this, pin));
        else setToolTip(QString("<b>%1</b> %2<br>%3").arg(m_ref, m_def->name, specSummary()));
    }
    QGraphicsItem::hoverMoveEvent(e);
}

void PartItem::hoverLeaveEvent(QGraphicsSceneHoverEvent* e)
{
    setHoverPin(-1);
    QGraphicsItem::hoverLeaveEvent(e);
}

void PartItem::notifyParam(const QString& key)
{
    if (auto* cs = qobject_cast<CircuitScene*>(scene())) cs->notifyParamChanged(this, key);
}

void PartItem::mousePressEvent(QGraphicsSceneMouseEvent* e)
{
    if (m_simulating && e->button() == Qt::LeftButton && !m_def->clickParam.isEmpty()) {
        const QString& k = m_def->clickParam;
        if (m_def->clickMomentary) setParam(k, true);
        else setParam(k, !paramB(k));
        m_pressedByMouse = true;
        notifyParam(k);
        e->accept();
        return;
    }
    QGraphicsItem::mousePressEvent(e);
}

void PartItem::mouseReleaseEvent(QGraphicsSceneMouseEvent* e)
{
    if (m_pressedByMouse) {
        m_pressedByMouse = false;
        if (m_def->clickMomentary) {
            setParam(m_def->clickParam, false);
            notifyParam(m_def->clickParam);
        }
        e->accept();
        return;
    }
    QGraphicsItem::mouseReleaseEvent(e);
}

void PartItem::wheelEvent(QGraphicsSceneWheelEvent* e)
{
    // 시뮬레이션 중 마우스 휠로 주요 센서값 조절
    if (!m_simulating) { e->ignore(); return; }
    QString key;
    for (const ParamDef& p : m_def->params)
        if (p.live && p.type == ParamType::Number && !p.hidden) { key = p.key; break; }
    if (key.isEmpty()) { e->ignore(); return; }
    const ParamDef* pd = m_def->param(key);
    double v = paramD(key);
    double step = (pd->maxV - pd->minV) / 50.0;
    if (pd->logScale) v *= (e->delta() > 0 ? 1.25 : 0.8);
    else v += (e->delta() > 0 ? step : -step);
    v = qBound(pd->minV, v, pd->maxV);
    setParam(key, v);
    notifyParam(key);
    e->accept();
}

// ---------------------------------------------------------------------------
// 그리기
// ---------------------------------------------------------------------------
void PartItem::paint(QPainter* p, const QStyleOptionGraphicsItem*, QWidget*)
{
    p->setRenderHint(QPainter::Antialiasing, true);
    const QString& s = m_def->shape;
    if (!m_def->isBoard) drawLegs(p);

    if (s == "uno" || s == "nano" || s == "mega") drawBoard(p);
    else if (s == "resistor") drawResistor(p);
    else if (s == "led") drawLed(p);
    else if (s == "rgb_led") drawRgbLed(p);
    else if (s == "seven_seg") drawSevenSeg(p);
    else if (s == "button") drawButton(p);
    else if (s == "slide") drawSlide(p);
    else if (s == "pot") drawPot(p);
    else if (s == "ldr") drawLdr(p);
    else if (s == "ntc") drawNtc(p);
    else if (s == "cap_ceramic") drawCapCeramic(p);
    else if (s == "cap_elec") drawCapElec(p);
    else if (s == "diode") drawDiode(p);
    else if (s == "to92") drawTo92(p);
    else if (s == "to220") drawTo220(p);
    else if (s == "buzzer") drawBuzzer(p);
    else if (s == "motor") drawMotor(p);
    else if (s == "servo") drawServo(p);
    else if (s == "lcd") drawLcd(p);
    else if (s == "neopixel_ring") drawNeoRing(p);
    else if (s == "neopixel_strip") drawNeoStrip(p);
    else if (s == "dht") drawDht(p);
    else if (s == "hcsr04") drawHcsr04(p);
    else if (s == "relay") drawRelay(p);
    else if (s == "stepper") drawStepper(p);
    else if (s == "joystick") drawJoystick(p);
    else if (s == "tilt") drawTilt(p);
    else if (s == "reed") drawReed(p);
    else drawModule(p);

    drawPins(p);

    // 참조 기호
    if (!m_def->isBoard) {
        p->setPen(Theme::partLabel());
        p->setFont(pixelFont(9, true));
        p->drawText(QRectF(-6, -15, m_def->size.width() + 12, 12), Qt::AlignHCenter | Qt::AlignBottom, m_ref);
    }

    if (isSelected()) {
        QPen sel(QColor(0, 120, 215), 1.5, Qt::DashLine);
        p->setPen(sel);
        p->setBrush(Qt::NoBrush);
        p->drawRect(QRectF(QPointF(0, 0), m_def->size).adjusted(-4, -4, 4, 4));
    }
    drawWarning(p);
}

void PartItem::drawLegs(QPainter* p)
{
    QRectF b = bodyRect();
    p->setPen(QPen(kLeg, 2.0, Qt::SolidLine, Qt::RoundCap));
    for (const PinDef& pin : m_def->pins) {
        QPointF a = pin.pos, e = pin.pos;
        if (pin.pos.y() >= m_def->size.height() - 0.5) e.setY(b.bottom());
        else if (pin.pos.y() <= 0.5) e.setY(b.top());
        else if (pin.pos.x() <= 0.5) e.setX(b.left());
        else if (pin.pos.x() >= m_def->size.width() - 0.5) e.setX(b.right());
        p->drawLine(a, e);
    }
}

void PartItem::drawPins(QPainter* p)
{
    for (int i = 0; i < m_def->pins.size(); ++i) {
        const PinDef& pin = m_def->pins[i];
        bool connected = pinHasWire(i);
        if (m_def->isBoard) {
            p->setPen(Qt::NoPen);
            p->setBrush(connected ? QColor(230, 190, 60) : QColor(70, 70, 70));
            p->drawRect(QRectF(pin.pos.x() - 2.5, pin.pos.y() - 2.5, 5, 5));
        } else {
            p->setPen(QPen(QColor(90, 90, 90), 1));
            p->setBrush(connected ? QColor(230, 190, 60) : QColor(210, 210, 215));
            p->drawEllipse(pin.pos, 2.5, 2.5);
        }
        if (i == m_hoverPin) {
            p->setPen(QPen(QColor(0, 160, 255), 2));
            p->setBrush(Qt::NoBrush);
            p->drawEllipse(pin.pos, 5.5, 5.5);
            // 핀 이름 라벨
            QString label = pin.name;
            if (m_def->isBoard && !pin.func.isEmpty() && pin.func != pin.name) label += " (" + pin.func + ")";
            if (pin.pwm) label += " ~PWM";
            p->setFont(pixelFont(10, true));
            QFontMetricsF fm(p->font());
            QRectF tr(pin.pos.x() + 7, pin.pos.y() - 18, fm.horizontalAdvance(label) + 8, 14);
            p->setPen(Qt::NoPen);
            p->setBrush(QColor(255, 255, 220, 235));
            p->drawRoundedRect(tr, 3, 3);
            p->setPen(Qt::black);
            p->drawText(tr, Qt::AlignCenter, label);
        }
    }
}

void PartItem::drawGlow(QPainter* p, const QPointF& c, double radius, const QColor& col, double level)
{
    if (level <= 0.01) return;
    level = qBound(0.0, level, 1.0);
    QRadialGradient g(c, radius);
    QColor c0 = col; c0.setAlphaF(0.85 * level);
    QColor c1 = col; c1.setAlphaF(0.0);
    g.setColorAt(0.0, c0);
    g.setColorAt(1.0, c1);
    p->setPen(Qt::NoPen);
    p->setBrush(g);
    p->drawEllipse(c, radius, radius);
}

void PartItem::drawWarning(QPainter* p)
{
    QString warn = m_sim.value("warn").toString();
    if (warn.isEmpty()) return;
    QPointF c(m_def->size.width(), -4);
    p->setPen(QPen(Qt::white, 1.5));
    p->setBrush(QColor(220, 40, 30));
    QPolygonF tri;
    tri << c + QPointF(0, -9) << c + QPointF(9, 7) << c + QPointF(-9, 7);
    p->drawPolygon(tri);
    p->setFont(pixelFont(11, true));
    p->drawText(QRectF(c.x() - 5, c.y() - 6, 10, 13), Qt::AlignCenter, "!");
}

// ----------------------------- 보드 -----------------------------------------
void PartItem::drawBoard(QPainter* p)
{
    const QString& s = m_def->shape;
    QRectF r(0, 0, m_def->size.width(), m_def->size.height());
    p->setPen(QPen(m_def->color.darker(140), 1.5));
    p->setBrush(m_def->color);
    p->drawRoundedRect(r, 8, 8);

    // 장착 구멍
    p->setBrush(Qt::white);
    p->setPen(QPen(QColor(200, 200, 200), 1));
    if (s != "nano") {
        for (QPointF h : { QPointF(55, 12), QPointF(r.width() - 15, 40), QPointF(r.width() - 15, 180), QPointF(60, 195) })
            p->drawEllipse(h, 5, 5);
    }

    p->setFont(pixelFont(7));
    if (s == "nano") {
        // USB 미니
        p->setPen(Qt::NoPen);
        p->setBrush(QColor(190, 190, 195));
        p->drawRect(QRectF(-6, 24, 22, 22));
        // 칩
        p->setBrush(QColor(30, 30, 30));
        p->save(); p->translate(110, 35); p->rotate(45);
        p->drawRect(QRectF(-12, -12, 24, 24)); p->restore();
        p->setPen(Qt::white);
        p->setFont(pixelFont(9, true));
        p->drawText(QRectF(140, 25, 55, 20), Qt::AlignCenter, "NANO");
    } else {
        // USB-B
        p->setPen(QPen(QColor(120, 120, 120), 1));
        p->setBrush(QColor(200, 200, 205));
        p->drawRect(QRectF(-10, 30, 48, 46));
        // DC 잭
        p->setBrush(QColor(25, 25, 25));
        p->drawRect(QRectF(-6, 140, 50, 36));
        // 리셋 버튼
        p->setBrush(QColor(220, 220, 220));
        p->drawRect(QRectF(20, 8, 14, 14));
        p->setBrush(QColor(200, 30, 30));
        p->drawEllipse(QPointF(27, 15), 4, 4);
        // MCU
        p->setPen(Qt::NoPen);
        p->setBrush(QColor(30, 30, 30));
        if (s == "uno") {
            p->drawRect(QRectF(150, 140, 120, 28));
            p->setBrush(QColor(200, 200, 200));
            for (int i = 0; i < 14; ++i) {
                p->drawRect(QRectF(153 + i * 8.3, 136, 3, 4));
                p->drawRect(QRectF(153 + i * 8.3, 168, 3, 4));
            }
            p->setPen(QColor(200, 200, 200));
            p->setFont(pixelFont(8));
            p->drawText(QRectF(150, 140, 120, 28), Qt::AlignCenter, "ATMEGA328P-PU");
        } else {
            p->save(); p->translate(250, 115); p->rotate(45);
            p->drawRect(QRectF(-26, -26, 52, 52));
            p->restore();
            p->setPen(QColor(200, 200, 200));
            p->setFont(pixelFont(7));
            p->drawText(QRectF(215, 105, 70, 20), Qt::AlignCenter, "ATMEGA2560");
        }
        // 로고
        p->setPen(Qt::white);
        p->setFont(pixelFont(16, true));
        p->drawText(QRectF(70, 60, 160, 24), Qt::AlignLeft, s == "uno" ? "UNO" : "MEGA 2560");
        p->setFont(pixelFont(11, true));
        p->drawText(QRectF(70, 84, 160, 18), Qt::AlignLeft, "ARDUINO");
        // 헤더 그룹 라벨
        p->setFont(pixelFont(7, true));
        p->drawText(QRectF(130, 34, 150, 10), Qt::AlignLeft, "DIGITAL (PWM ~)");
        p->drawText(QRectF(100, 168, 80, 10), Qt::AlignLeft, "POWER");
        p->drawText(QRectF(190, 168, 80, 10), Qt::AlignLeft, "ANALOG IN");
    }

    // 헤더 (검은 띠)
    p->setPen(Qt::NoPen);
    p->setBrush(kHeader);
    for (const PinDef& pin : m_def->pins)
        p->drawRect(QRectF(pin.pos.x() - 5, pin.pos.y() - 5, 10, 10));

    // 핀 라벨
    p->setPen(Qt::white);
    p->setFont(pixelFont(6));
    for (const PinDef& pin : m_def->pins) {
        QString t = pin.name;
        if (t.startsWith('D') && t.size() <= 3 && !pin.pwm) t = t.mid(1);
        else if (t.startsWith('D') && pin.pwm) t = "~" + t.mid(1);
        if (s == "mega" && pin.pos.x() >= 375) {
            if (pin.pos.x() > 385) continue;
            p->save(); p->translate(pin.pos.x() - 8, pin.pos.y());
            p->drawText(QRectF(-26, -4, 24, 8), Qt::AlignRight | Qt::AlignVCenter, t);
            p->restore();
            continue;
        }
        bool top = pin.pos.y() < m_def->size.height() / 2;
        p->save();
        p->translate(pin.pos.x(), pin.pos.y() + (top ? 8 : -8));
        p->rotate(-90);
        QRectF tr = top ? QRectF(-24, -4, 24, 8) : QRectF(0, -4, 24, 8);
        p->drawText(tr, (top ? Qt::AlignRight : Qt::AlignLeft) | Qt::AlignVCenter, t);
        p->restore();
    }

    // 상태 LED (ON, L, TX, RX)
    QPointF ledOn, ledL, ledTx, ledRx;
    if (s == "nano") { ledOn = QPointF(60, 35); ledL = QPointF(60, 45); ledTx = QPointF(70, 35); ledRx = QPointF(70, 45); }
    else { ledL = QPointF(118, 46); ledTx = QPointF(118, 58); ledRx = QPointF(118, 70); ledOn = QPointF(r.width() - 40, 60); }
    auto smallLed = [&](const QPointF& c, const QColor& col, bool on, const QString& label, double level = 1.0) {
        p->setPen(Qt::NoPen);
        p->setBrush(on ? col : col.darker(300));
        p->drawRect(QRectF(c.x() - 3, c.y() - 2, 6, 4));
        if (on) drawGlow(p, c, 9, col, level);
        p->setPen(Qt::white);
        p->setFont(pixelFont(6));
        p->drawText(QRectF(c.x() - 18, c.y() - 4, 13, 8), Qt::AlignRight | Qt::AlignVCenter, label);
    };
    bool sim = m_simulating;
    smallLed(ledOn, QColor(80, 255, 80), sim, "ON");
    double lv = m_sim.value("boardL").toDouble();
    smallLed(ledL, QColor(255, 200, 40), sim && lv > 0.02, "L", lv);
    smallLed(ledTx, QColor(255, 200, 40), sim && m_sim.value("tx").toBool(), "TX");
    smallLed(ledRx, QColor(255, 200, 40), sim && m_sim.value("rx").toBool(), "RX");

    // 시뮬레이션 중 핀 상태 표시
    if (sim) {
        QVariantList states = m_sim.value("pins").toList();
        for (int i = 0; i < states.size() && i < m_def->pins.size(); ++i) {
            QVariantList st = states[i].toList();
            if (st.isEmpty()) continue;
            int mode = st[0].toInt();          // 0=없음 1=출력 2=PWM 3=입력 4=풀업
            double level = st.size() > 1 ? st[1].toDouble() : 0.0;
            if (mode == 0) continue;
            const PinDef& pin = m_def->pins[i];
            QPointF c = pin.pos;
            QColor col;
            if (mode == 1) col = level > 0.5 ? QColor(60, 255, 60) : QColor(200, 30, 30);
            else if (mode == 2) col = QColor(255, 220, 0);
            else col = level > 0.5 ? QColor(80, 200, 255) : QColor(30, 60, 160);
            p->setPen(QPen(col, mode >= 3 ? 1.5 : 0));
            p->setBrush(mode >= 3 ? Qt::NoBrush : QBrush(col));
            p->drawEllipse(c, 2.8, 2.8);
            if (mode == 2) drawGlow(p, c, 7, col, level);
        }
    }
}

// ----------------------------- 수동 소자 -------------------------------------
void PartItem::drawResistor(QPainter* p)
{
    QRectF b(12, 4, 36, 12);
    QPainterPath path;
    path.addRoundedRect(b, 5, 5);
    bool metal = paramD("tol", 5) <= 1.0;
    p->setPen(QPen(QColor(120, 100, 70), 1));
    p->setBrush(metal ? QColor(110, 170, 220) : QColor(225, 195, 140));
    p->drawPath(path);

    // 색 띠 계산
    double R = paramD("R", 220);
    int sig = metal ? 3 : 2;
    QVector<int> digits;
    int mult = 0;
    if (R > 0) {
        double e = std::floor(std::log10(R)) - (sig - 1);
        double mant = std::round(R / std::pow(10.0, e));
        if (mant >= std::pow(10.0, sig)) { mant /= 10; e += 1; }
        QString ds = QString::number((long long)mant);
        for (QChar c : ds) digits << c.digitValue();
        mult = (int)e;
    }
    QVector<QColor> bands;
    for (int d : digits) bands << bandColor(d);
    if (mult >= 0) bands << bandColor(mult);
    else if (mult == -1) bands << QColor(212, 175, 55);
    else bands << QColor(192, 192, 192);
    double tol = paramD("tol", 5);
    bands << (tol <= 1 ? bandColor(1) : (tol <= 5 ? QColor(212, 175, 55) : QColor(192, 192, 192)));
    double x = b.left() + 5;
    for (int i = 0; i < bands.size(); ++i) {
        if (i == bands.size() - 1) x = b.right() - 7;
        p->setPen(Qt::NoPen);
        p->setBrush(bands[i]);
        p->drawRect(QRectF(x, b.top(), 3, b.height()));
        x += 5;
    }
    if (m_sim.value("hot").toBool()) {
        p->setPen(QPen(QColor(255, 80, 0, 180), 2));
        p->setBrush(Qt::NoBrush);
        p->drawRoundedRect(b.adjusted(-2, -2, 2, 2), 6, 6);
    }
    // 값 라벨
    p->setPen(Theme::partLabel());
    p->setFont(pixelFont(8));
    p->drawText(QRectF(0, 15, 60, 12), Qt::AlignHCenter, Units::format(R, QStringLiteral("Ω")));
}

void PartItem::drawPot(QPainter* p)
{
    QRectF b = bodyRect();
    QString kind = paramS("kind");
    p->setPen(QPen(QColor(40, 40, 40), 1));
    if (kind == "trim") {
        p->setBrush(QColor(30, 80, 200));
        p->drawRect(b.adjusted(4, 8, -4, -4));
        p->setBrush(QColor(240, 240, 240));
        p->drawEllipse(b.center() + QPointF(0, 2), 7, 7);
    } else if (kind == "slide") {
        p->setBrush(QColor(40, 40, 40));
        p->drawRect(b.adjusted(2, 10, -2, -10));
        double pos = paramD("pos", 50) / 100.0;
        p->setBrush(QColor(220, 220, 220));
        p->drawRect(QRectF(b.left() + 2 + pos * (b.width() - 14), b.center().y() - 8, 10, 16));
        return;
    } else {
        p->setBrush(QColor(60, 120, 200));
        p->drawRect(b.adjusted(2, 14, -2, 0));
        p->setBrush(QColor(200, 200, 200));
        p->drawEllipse(QPointF(b.center().x(), b.top() + 16), 15, 15);
        p->setBrush(QColor(60, 60, 60));
        p->drawEllipse(QPointF(b.center().x(), b.top() + 16), 10, 10);
    }
    // 노브 표시선
    double pos = paramD("pos", 50) / 100.0;
    double ang = (-135 + pos * 270) * M_PI / 180.0;
    QPointF c = (kind == "trim") ? b.center() + QPointF(0, 2) : QPointF(b.center().x(), b.top() + 16);
    p->setPen(QPen(Qt::white, 2, Qt::SolidLine, Qt::RoundCap));
    p->drawLine(c, c + QPointF(std::sin(ang) * 8, -std::cos(ang) * 8));
}

void PartItem::drawCapCeramic(QPainter* p)
{
    QRectF b = bodyRect();
    p->setPen(QPen(QColor(150, 100, 20), 1));
    p->setBrush(QColor(230, 160, 40));
    p->drawEllipse(QRectF(b.left() + 3, b.top() + 4, b.width() - 6, b.height() - 8));
    p->setPen(QColor(60, 40, 0));
    p->setFont(pixelFont(7));
    double c = paramD("C");
    int code = 0;
    if (c >= 10e-12) {
        double pf = c * 1e12;
        int e = (int)std::floor(std::log10(pf)) - 1;
        int mant = (int)std::round(pf / std::pow(10.0, e));
        code = mant * 10 + qMax(0, e);
    }
    p->drawText(b, Qt::AlignCenter, QString::number(code));
}

void PartItem::drawCapElec(QPainter* p)
{
    QRectF b = bodyRect().adjusted(2, 0, -2, 0);
    p->setPen(QPen(QColor(10, 10, 60), 1));
    p->setBrush(QColor(30, 50, 140));
    p->drawRoundedRect(b, 4, 4);
    p->setPen(Qt::NoPen);
    p->setBrush(QColor(200, 200, 220));
    p->drawRect(QRectF(b.right() - 6, b.top() + 2, 4, b.height() - 4));
    p->setPen(Qt::white);
    p->setFont(pixelFont(7));
    p->drawText(b.adjusted(0, 0, -6, 0), Qt::AlignCenter, Units::format(paramD("C"), "F", 2));
}

void PartItem::drawDiode(QPainter* p)
{
    QRectF b(14, 5, 32, 10);
    bool zener = paramD("Vz") > 0;
    bool glass = paramS("model").startsWith("1N4148") || zener;
    p->setPen(QPen(QColor(50, 50, 50), 1));
    p->setBrush(glass ? QColor(230, 120, 80) : QColor(30, 30, 30));
    p->drawRoundedRect(b, 3, 3);
    p->setPen(Qt::NoPen);
    p->setBrush(glass ? QColor(20, 20, 20) : QColor(220, 220, 220));
    p->drawRect(QRectF(b.right() - 6, b.top(), 3, b.height()));
    p->setPen(Theme::partLabel());
    p->setFont(pixelFont(7));
    p->drawText(QRectF(0, 15, 60, 10), Qt::AlignHCenter, paramS("model").section(' ', 0, 0));
}

void PartItem::drawTo92(QPainter* p)
{
    QRectF b = bodyRect();
    QPainterPath path;
    path.moveTo(b.left() + 4, b.bottom());
    path.lineTo(b.left() + 4, b.top() + 14);
    path.arcTo(QRectF(b.left() + 4, b.top() + 2, b.width() - 8, 26), 180, -180);
    path.lineTo(b.right() - 4, b.bottom());
    path.closeSubpath();
    p->setPen(QPen(QColor(10, 10, 10), 1));
    p->setBrush(QColor(40, 40, 40));
    p->drawPath(path);
    p->setPen(QColor(220, 220, 220));
    p->setFont(pixelFont(7));
    QString t = m_def->id == "analog_temp" ? paramS("model").section(' ', 0, 0) : paramS("model").section(' ', 0, 0);
    p->drawText(b.adjusted(0, 10, 0, 0), Qt::AlignCenter, t);
    // 핀 라벨
    p->setFont(pixelFont(6));
    for (const PinDef& pin : m_def->pins)
        p->drawText(QRectF(pin.pos.x() - 8, b.bottom() - 9, 16, 8), Qt::AlignCenter, pin.name.left(4));
}

void PartItem::drawTo220(QPainter* p)
{
    QRectF b = bodyRect();
    p->setPen(QPen(QColor(120, 120, 120), 1));
    p->setBrush(QColor(190, 190, 195));
    p->drawRect(QRectF(b.left() + 2, b.top(), b.width() - 4, 16));
    p->setBrush(Qt::white);
    p->drawEllipse(QPointF(b.center().x(), b.top() + 8), 4, 4);
    p->setBrush(QColor(30, 30, 30));
    p->drawRect(QRectF(b.left() + 2, b.top() + 16, b.width() - 4, b.height() - 16));
    p->setPen(QColor(220, 220, 220));
    p->setFont(pixelFont(7));
    p->drawText(QRectF(b.left(), b.top() + 16, b.width(), b.height() - 26), Qt::AlignCenter, paramS("model").section(' ', 0, 0));
    p->setFont(pixelFont(6));
    for (const PinDef& pin : m_def->pins)
        p->drawText(QRectF(pin.pos.x() - 6, b.bottom() - 9, 12, 8), Qt::AlignCenter, pin.name);
    if (m_sim.value("on").toBool()) drawGlow(p, b.center(), 14, QColor(120, 255, 120), 0.6);
}

// ----------------------------- LED ------------------------------------------
void PartItem::drawLed(QPainter* p)
{
    QRectF b = bodyRect();
    QColor col(paramS("rgb"));
    if (!col.isValid()) col = Qt::red;
    double level = m_sim.value("glow").toDouble();
    bool burned = m_sim.value("burned").toBool();
    QString size = paramS("size");
    double scale = size == "3mm" ? 0.8 : (size == "10mm" ? 1.15 : (size == "8mm" ? 1.05 : 1.0));
    double w = 18 * scale, h = 26 * scale;
    QRectF dome(b.center().x() - w / 2, b.bottom() - h - 4, w, h);
    QPainterPath path;
    path.moveTo(dome.left(), dome.bottom());
    path.lineTo(dome.left(), dome.top() + w / 2);
    path.arcTo(QRectF(dome.left(), dome.top(), w, w), 180, -180);
    path.lineTo(dome.right(), dome.bottom());
    path.closeSubpath();
    QColor body = col; body.setAlpha(150);
    if (burned) body = QColor(60, 50, 40);
    else if (level > 0.01) body = col.lighter(100 + (int)(60 * level));
    p->setPen(QPen(col.darker(160), 1));
    p->setBrush(body);
    p->drawPath(path);
    // 림
    p->setBrush(body.darker(110));
    p->drawRect(QRectF(dome.left() - 2, dome.bottom() - 3, w + 4, 4));
    if (!burned) drawGlow(p, QPointF(dome.center().x(), dome.top() + w / 2), 22 * scale + 10 * level, col, level);
    else {
        p->setPen(QPen(QColor(40, 40, 40), 2));
        p->drawLine(dome.topLeft() + QPointF(3, 5), dome.bottomRight() - QPointF(3, 8));
        p->drawLine(dome.topRight() + QPointF(-3, 5), dome.bottomLeft() + QPointF(3, -8));
    }
    // 반사광
    p->setPen(Qt::NoPen);
    p->setBrush(QColor(255, 255, 255, 120));
    p->drawEllipse(QRectF(dome.left() + 3, dome.top() + 3, w * 0.3, h * 0.35));
}

void PartItem::drawRgbLed(QPainter* p)
{
    QRectF b = bodyRect();
    QRectF dome(b.center().x() - 12, b.top() + 2, 24, 34);
    QPainterPath path;
    path.moveTo(dome.left(), dome.bottom());
    path.lineTo(dome.left(), dome.top() + 12);
    path.arcTo(QRectF(dome.left(), dome.top(), 24, 24), 180, -180);
    path.lineTo(dome.right(), dome.bottom());
    path.closeSubpath();
    double r = m_sim.value("glowR").toDouble(), g = m_sim.value("glowG").toDouble(), bl = m_sim.value("glowB").toDouble();
    double mx = qMax(r, qMax(g, bl));
    QColor mix(qBound(0, (int)(r / qMax(mx, 1e-6) * 255), 255), qBound(0, (int)(g / qMax(mx, 1e-6) * 255), 255),
               qBound(0, (int)(bl / qMax(mx, 1e-6) * 255), 255));
    p->setPen(QPen(QColor(150, 150, 150), 1));
    p->setBrush(mx > 0.01 ? mix.lighter(110) : QColor(235, 235, 240, 200));
    p->drawPath(path);
    if (mx > 0.01) drawGlow(p, QPointF(dome.center().x(), dome.top() + 12), 26, mix, mx);
    p->setPen(Theme::partLabel());
    p->setFont(pixelFont(6));
    p->drawText(QRectF(b.left(), b.bottom() - 9, b.width(), 8), Qt::AlignCenter, paramS("type"));
}

void PartItem::drawSevenSeg(QPainter* p)
{
    QRectF b = bodyRect();
    p->setPen(QPen(QColor(60, 60, 60), 1));
    p->setBrush(QColor(30, 30, 30));
    p->drawRect(b);
    QColor col(paramS("rgb"));
    if (!col.isValid()) col = Qt::red;
    QVariantList seg = m_sim.value("seg").toList();
    auto lv = [&](int i) { return i < seg.size() ? seg[i].toDouble() : 0.0; };
    // a b c d e f g dp 위치
    double x0 = b.left() + 14, x1 = b.right() - 18, y0 = b.top() + 10, ym = b.center().y(), y1 = b.bottom() - 12;
    QLineF lines[7] = { QLineF(x0 + 3, y0, x1 - 3, y0), QLineF(x1, y0 + 3, x1 - 2, ym - 3), QLineF(x1 - 2, ym + 3, x1 - 4, y1 - 3),
                        QLineF(x0 - 1, y1, x1 - 7, y1), QLineF(x0 - 2, ym + 3, x0 - 4, y1 - 3), QLineF(x0, y0 + 3, x0 - 2, ym - 3),
                        QLineF(x0 + 1, ym, x1 - 5, ym) };
    for (int i = 0; i < 7; ++i) {
        double l = lv(i);
        QColor c = l > 0.01 ? col.lighter(100 + (int)(40 * l)) : QColor(60, 50, 50);
        p->setPen(QPen(c, 5, Qt::SolidLine, Qt::RoundCap));
        p->drawLine(lines[i]);
        if (l > 0.01) {
            p->setPen(QPen(QColor(col.red(), col.green(), col.blue(), (int)(70 * l)), 10, Qt::SolidLine, Qt::RoundCap));
            p->drawLine(lines[i]);
        }
    }
    double dp = lv(7);
    p->setPen(Qt::NoPen);
    p->setBrush(dp > 0.01 ? col : QColor(60, 50, 50));
    p->drawEllipse(QPointF(b.right() - 9, y1), 3, 3);
}

void PartItem::drawNeoRing(QPainter* p)
{
    QRectF b = bodyRect();
    int n = paramD("count", 16);
    QPointF c = b.center();
    double R = qMin(b.width(), b.height()) / 2 - 2;
    QPainterPath ring;
    ring.addEllipse(c, R, R);
    ring.addEllipse(c, R - 22, R - 22);
    p->setPen(QPen(QColor(10, 10, 10), 1));
    p->setBrush(QColor(20, 20, 20));
    p->drawPath(ring);
    QVariantList px = m_sim.value("pixels").toList();
    for (int i = 0; i < n; ++i) {
        double a = (i * 360.0 / n - 90) * M_PI / 180.0;
        QPointF pc = c + QPointF(std::cos(a), std::sin(a)) * (R - 11);
        QColor col = i < px.size() ? QColor::fromRgb(px[i].toUInt()) : QColor(0, 0, 0);
        p->setPen(QPen(QColor(200, 200, 200), 0.8));
        p->setBrush(col.value() > 5 ? col : QColor(235, 235, 235));
        p->drawRect(QRectF(pc.x() - 5, pc.y() - 5, 10, 10));
        if (col.value() > 5) drawGlow(p, pc, 12, col, col.valueF());
    }
}

void PartItem::drawNeoStrip(QPainter* p)
{
    QRectF b = bodyRect();
    int n = paramD("count", 8);
    double w = qMax(b.width(), n * 14.0 + 10);
    QRectF strip(b.left(), b.top(), w, b.height());
    p->setPen(QPen(QColor(10, 10, 10), 1));
    p->setBrush(QColor(25, 25, 25));
    p->drawRoundedRect(strip, 3, 3);
    QVariantList px = m_sim.value("pixels").toList();
    double step = (strip.width() - 10) / n;
    for (int i = 0; i < n; ++i) {
        QPointF pc(strip.left() + 5 + step * (i + 0.5), strip.center().y());
        QColor col = i < px.size() ? QColor::fromRgb(px[i].toUInt()) : QColor(0, 0, 0);
        p->setPen(QPen(QColor(200, 200, 200), 0.8));
        p->setBrush(col.value() > 5 ? col : QColor(235, 235, 235));
        p->drawRect(QRectF(pc.x() - 5, pc.y() - 5, 10, 10));
        if (col.value() > 5) drawGlow(p, pc, 12, col, col.valueF());
    }
}

void PartItem::drawLcd(QPainter* p)
{
    QRectF b = bodyRect();
    int cols = (int)paramD("cols", 16), rows = (int)paramD("rows", 2);
    p->setPen(QPen(QColor(0, 60, 0), 1));
    p->setBrush(QColor(0, 120, 60));
    p->drawRect(b);
    QRectF frame = b.adjusted(8, 8, -8, -14);
    p->setBrush(QColor(20, 20, 20));
    p->drawRect(frame);
    bool blue = paramS("backlight") != "green";
    bool on = m_simulating && m_sim.value("lcdOn", true).toBool() && m_sim.value("powered").toBool();
    QColor bg = blue ? (on ? QColor(40, 90, 255) : QColor(20, 40, 110)) : (on ? QColor(160, 220, 40) : QColor(70, 100, 30));
    QColor fg = blue ? QColor(235, 240, 255) : QColor(20, 30, 10);
    QRectF scr = frame.adjusted(5, 5, -5, -5);
    p->setBrush(bg);
    p->setPen(Qt::NoPen);
    p->drawRect(scr);
    double cw = scr.width() / cols, ch = scr.height() / rows;
    QStringList lines = m_sim.value("lcd").toStringList();
    QFont f = Theme::monoFont(9);
    f.setPixelSize(qMax(8, (int)(ch * 0.8)));
    p->setFont(f);
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            QRectF cell(scr.left() + c * cw + 0.5, scr.top() + r * ch + 1, cw - 1, ch - 2);
            p->setPen(Qt::NoPen);
            p->setBrush(QColor(255, 255, 255, blue ? 18 : 25));
            p->drawRect(cell);
            QString line = r < lines.size() ? lines[r] : QString();
            if (c < line.size() && line[c] != ' ') {
                p->setPen(fg);
                p->drawText(cell, Qt::AlignCenter, QString(line[c]));
            }
        }
    }
    p->setPen(QColor(220, 255, 220));
    p->setFont(pixelFont(7));
    p->drawText(QRectF(b.left() + 50, b.bottom() - 12, b.width() - 55, 10), Qt::AlignRight, m_def->name.section(' ', 0, 2));
    for (const PinDef& pin : m_def->pins) {
        p->save();
        p->translate(pin.pos.x(), b.bottom() - 2);
        p->rotate(-90);
        p->setFont(pixelFont(5));
        p->drawText(QRectF(0, -3, 12, 6), Qt::AlignLeft | Qt::AlignVCenter, pin.name);
        p->restore();
    }
}

// ----------------------------- 입력 장치 --------------------------------------
void PartItem::drawButton(QPainter* p)
{
    QRectF b = bodyRect();
    bool pressed = paramB("pressed");
    p->setPen(QPen(QColor(60, 60, 60), 1));
    p->setBrush(QColor(40, 40, 40));
    p->drawRect(b.adjusted(2, 2, -2, -2));
    p->setBrush(QColor(170, 170, 170));
    for (QPointF c : { b.topLeft() + QPointF(6, 6), b.topRight() + QPointF(-6, 6), b.bottomLeft() + QPointF(6, -6), b.bottomRight() + QPointF(-6, -6) })
        p->drawEllipse(c, 2, 2);
    QColor cap(paramS("cap"));
    if (!cap.isValid()) cap = Qt::red;
    double r = pressed ? 9 : 11;
    p->setBrush(pressed ? cap.darker(130) : cap);
    p->setPen(QPen(cap.darker(160), 1.5));
    p->drawEllipse(b.center(), r, r);
}

void PartItem::drawSlide(QPainter* p)
{
    QRectF b = bodyRect();
    bool on = paramB("on");
    p->setPen(QPen(QColor(60, 60, 60), 1));
    p->setBrush(QColor(190, 190, 195));
    p->drawRect(QRectF(b.left() + 2, b.center().y() - 8, b.width() - 4, 16));
    p->setBrush(QColor(30, 30, 30));
    double x = on ? b.left() + 6 : b.right() - 18;
    p->drawRect(QRectF(x, b.center().y() - 14, 12, 16));
    p->setPen(Theme::partLabel());
    p->setFont(pixelFont(7));
    p->drawText(QRectF(b.left(), b.bottom() - 10, b.width(), 9), Qt::AlignCenter, on ? "ON(1)" : "OFF(2)");
}

void PartItem::drawJoystick(QPainter* p)
{
    QRectF b = bodyRect();
    p->setPen(QPen(QColor(10, 10, 10), 1));
    p->setBrush(QColor(20, 20, 20));
    p->drawRect(b);
    QPointF c = b.center() - QPointF(0, 2);
    p->setBrush(QColor(60, 60, 60));
    p->drawEllipse(c, 24, 24);
    double x = (paramD("x", 50) - 50) / 50.0, y = (paramD("y", 50) - 50) / 50.0;
    QPointF k = c + QPointF(x * 12, y * 12);
    bool pr = paramB("pressed");
    p->setBrush(pr ? QColor(50, 50, 50) : QColor(80, 80, 80));
    p->setPen(QPen(QColor(120, 120, 120), 1));
    p->drawEllipse(k, 14, 14);
    p->setPen(QColor(200, 200, 200));
    p->setFont(pixelFont(5));
    for (const PinDef& pin : m_def->pins)
        p->drawText(QRectF(pin.pos.x() - 7, b.bottom() - 7, 14, 6), Qt::AlignCenter, pin.name);
}

void PartItem::drawTilt(QPainter* p)
{
    QRectF b = bodyRect();
    bool t = paramB("tilted");
    p->save();
    p->translate(b.center());
    if (t) p->rotate(35);
    p->setPen(QPen(QColor(80, 80, 80), 1));
    p->setBrush(QColor(30, 30, 30));
    p->drawRoundedRect(QRectF(-9, -14, 18, 28), 4, 4);
    p->restore();
}

void PartItem::drawReed(QPainter* p)
{
    QRectF b(10, 4, 40, 12);
    bool m = paramB("magnet");
    p->setPen(QPen(QColor(150, 180, 160), 1));
    p->setBrush(QColor(210, 240, 220, 160));
    p->drawRoundedRect(b, 6, 6);
    p->setPen(QPen(QColor(120, 120, 120), 1.5));
    p->drawLine(QPointF(10, 10), QPointF(29, m ? 10 : 8));
    p->drawLine(QPointF(50, 10), QPointF(31, m ? 10 : 12));
    if (m) {
        p->setPen(Qt::NoPen);
        p->setBrush(QColor(220, 30, 30));
        p->drawRect(QRectF(18, -8, 12, 8));
        p->setBrush(QColor(30, 60, 220));
        p->drawRect(QRectF(30, -8, 12, 8));
    }
}

// ----------------------------- 센서 ------------------------------------------
void PartItem::drawLdr(QPainter* p)
{
    QRectF b = bodyRect();
    QPointF c = b.center();
    p->setPen(QPen(QColor(150, 150, 150), 1));
    p->setBrush(QColor(240, 230, 200));
    p->drawEllipse(c, 12, 12);
    p->setPen(QPen(QColor(200, 70, 30), 1.6));
    QPainterPath zig;
    zig.moveTo(c.x() - 7, c.y() - 7);
    for (int i = 0; i < 5; ++i) {
        double y = c.y() - 7 + i * 3.5;
        zig.lineTo(c.x() + 7, y);
        zig.lineTo(c.x() - 7, y + 1.75);
    }
    p->setBrush(Qt::NoBrush);
    p->drawPath(zig);
    if (m_simulating) {
        double lux = paramD("lux", 300);
        double lv = qBound(0.0, std::log10(qMax(lux, 1.0)) / 5.0, 1.0);
        drawGlow(p, c - QPointF(0, 12), 18, QColor(255, 240, 120), lv);
    }
}

void PartItem::drawNtc(QPainter* p)
{
    QRectF b = bodyRect();
    QPointF c = b.center();
    p->setPen(QPen(QColor(10, 10, 10), 1));
    p->setBrush(QColor(30, 30, 30));
    p->drawEllipse(c, 9, 11);
    if (m_simulating) {
        double t = paramD("temp", 25);
        QColor col = t > 25 ? QColor(255, 80, 30) : QColor(60, 140, 255);
        drawGlow(p, c, 18, col, qBound(0.0, std::fabs(t - 25) / 60.0, 1.0));
    }
}

void PartItem::drawDht(QPainter* p)
{
    QRectF b = bodyRect();
    QColor body(paramS("body"));
    if (!body.isValid()) body = QColor(240, 240, 240);
    p->setPen(QPen(body.darker(150), 1));
    p->setBrush(body);
    p->drawRoundedRect(b.adjusted(2, 0, -2, 0), 3, 3);
    // 그릴
    p->setPen(Qt::NoPen);
    p->setBrush(body.darker(160));
    for (int r = 0; r < 5; ++r)
        for (int c = 0; c < 4; ++c)
            p->drawRect(QRectF(b.left() + 8 + c * 9, b.top() + 6 + r * 9, 5, 5));
    p->setPen(body.value() > 128 ? QColor(60, 60, 60) : Qt::white);
    p->setFont(pixelFont(7, true));
    p->drawText(QRectF(b.left(), b.bottom() - 14, b.width(), 12), Qt::AlignCenter, paramS("dhtType"));
}

void PartItem::drawHcsr04(QPainter* p)
{
    QRectF b = bodyRect();
    p->setPen(QPen(QColor(10, 40, 90), 1));
    p->setBrush(QColor(20, 80, 170));
    p->drawRect(b);
    for (double x : { b.left() + 20.0, b.right() - 20.0 }) {
        p->setBrush(QColor(200, 200, 205));
        p->setPen(QPen(QColor(120, 120, 120), 1));
        p->drawEllipse(QPointF(x, b.top() + 22), 17, 17);
        p->setBrush(QColor(40, 40, 40));
        p->drawEllipse(QPointF(x, b.top() + 22), 12, 12);
    }
    p->setPen(Qt::white);
    p->setFont(pixelFont(6, true));
    p->drawText(QRectF(b.left(), b.top(), b.width(), 8), Qt::AlignCenter, "HC-SR04");
    for (const PinDef& pin : m_def->pins)
        p->drawText(QRectF(pin.pos.x() - 8, b.bottom() - 8, 16, 7), Qt::AlignCenter, pin.name);
    if (m_simulating) {
        double d = paramD("dist", 50);
        p->setPen(QPen(QColor(0, 160, 255, 160), 1, Qt::DashLine));
        double len = qMin(d, 150.0) * 0.6;
        for (int i = 1; i <= 3; ++i)
            p->drawArc(QRectF(b.center().x() - 15 * i, b.top() - len * i / 3 - 10, 30 * i, 20), 30 * 16, 120 * 16);
        p->setPen(Theme::accentText());
        p->setFont(pixelFont(9, true));
        p->drawText(QRectF(b.left(), b.top() - 26, b.width(), 12), Qt::AlignCenter, QString("%1 cm").arg(d, 0, 'f', 1));
    }
}

void PartItem::drawModule(QPainter* p)
{
    QRectF b = bodyRect();
    p->setPen(QPen(m_def->color.darker(150), 1));
    p->setBrush(m_def->color);
    p->drawRoundedRect(b, 3, 3);
    QPointF ic(b.center().x(), b.top() + b.height() * 0.42);
    const QString& icon = m_def->icon;
    p->setPen(QPen(QColor(30, 30, 30), 1));
    if (icon == "pir") {
        p->setBrush(QColor(245, 245, 245));
        p->drawEllipse(ic, 20, 18);
        p->setPen(QPen(QColor(210, 210, 210), 1));
        for (int i = -2; i <= 2; ++i) p->drawLine(ic + QPointF(i * 7, -15), ic + QPointF(i * 7, 15));
        if (paramB("motion") && m_simulating) drawGlow(p, ic, 28, QColor(255, 60, 60), 0.6);
    } else if (icon == "mic") {
        p->setBrush(QColor(200, 200, 205));
        p->drawEllipse(ic, 10, 10);
        p->setBrush(QColor(30, 30, 30));
        p->drawEllipse(ic, 6, 6);
    } else if (icon == "soil") {
        p->setBrush(QColor(200, 170, 60));
        p->drawRect(QRectF(b.left() + 8, ic.y() - 4, 8, b.bottom() - ic.y()));
        p->drawRect(QRectF(b.right() - 16, ic.y() - 4, 8, b.bottom() - ic.y()));
    } else if (icon == "gas") {
        p->setBrush(QColor(170, 170, 175));
        p->drawEllipse(ic, 15, 15);
        p->setPen(QPen(QColor(120, 120, 120), 1));
        for (int i = -2; i <= 2; ++i) { p->drawLine(ic + QPointF(i * 5, -12), ic + QPointF(i * 5, 12)); p->drawLine(ic + QPointF(-12, i * 5), ic + QPointF(12, i * 5)); }
    } else if (icon == "ir") {
        p->setBrush(QColor(240, 240, 240)); p->drawEllipse(ic - QPointF(9, 0), 6, 6);
        p->setBrush(QColor(30, 30, 30)); p->drawEllipse(ic + QPointF(9, 0), 6, 6);
    } else if (icon == "flame") {
        p->setBrush(QColor(20, 20, 20)); p->drawEllipse(ic, 7, 7);
        double f = paramD("flame");
        if (m_simulating && f > 1) drawGlow(p, ic - QPointF(0, 12), 22, QColor(255, 140, 0), f / 100.0);
    } else if (icon == "hall") {
        p->setBrush(QColor(30, 30, 30)); p->drawRect(QRectF(ic.x() - 6, ic.y() - 6, 12, 10));
        if (paramB("magnet")) { p->setBrush(QColor(220, 30, 30)); p->drawRect(QRectF(ic.x() - 12, ic.y() - 22, 12, 8)); p->setBrush(QColor(30, 60, 220)); p->drawRect(QRectF(ic.x(), ic.y() - 22, 12, 8)); }
    } else if (icon == "touch") {
        p->setBrush(QColor(240, 240, 240)); p->drawEllipse(ic, 13, 13);
        p->setBrush(paramB("touch") ? QColor(255, 200, 150) : QColor(200, 200, 200)); p->drawEllipse(ic, 7, 7);
    } else if (icon == "water" || icon == "rain") {
        p->setPen(QPen(QColor(220, 200, 120), 2));
        for (int i = 0; i < 4; ++i) p->drawLine(QPointF(b.left() + 8 + i * ((b.width() - 16) / 3.0), b.top() + 4), QPointF(b.left() + 8 + i * ((b.width() - 16) / 3.0), b.bottom() - 14));
        double lv = paramD(icon == "water" ? "level" : "wet") / 100.0;
        if (lv > 0) { p->setPen(Qt::NoPen); p->setBrush(QColor(60, 140, 255, 90)); p->drawRect(QRectF(b.left(), b.bottom() - 14 - (b.height() - 18) * lv, b.width(), (b.height() - 18) * lv)); }
    } else if (icon == "chip") {
        p->setBrush(QColor(200, 200, 200)); p->drawRect(QRectF(ic.x() - 6, ic.y() - 6, 12, 12));
    } else {
        p->setBrush(QColor(30, 30, 30)); p->drawRect(QRectF(ic.x() - 8, ic.y() - 8, 16, 16));
    }
    // 전원 LED / 신호 LED
    if (m_simulating) {
        bool pw = m_sim.value("powered").toBool();
        p->setPen(Qt::NoPen);
        p->setBrush(pw ? QColor(255, 40, 40) : QColor(80, 20, 20));
        p->drawEllipse(QPointF(b.right() - 6, b.top() + 6), 2.5, 2.5);
        if (m_sim.value("sigLed").toBool()) {
            p->setBrush(QColor(60, 255, 60));
            p->drawEllipse(QPointF(b.right() - 6, b.top() + 13), 2.5, 2.5);
        }
    }
    // 이름/핀 라벨
    p->setPen(Qt::white);
    p->setFont(pixelFont(6));
    for (const PinDef& pin : m_def->pins)
        p->drawText(QRectF(pin.pos.x() - 8, b.bottom() - 8, 16, 7), Qt::AlignCenter, pin.name);
    QString title = specSummary().section(' ', 0, 0);
    if (title.isEmpty()) title = m_def->name.section(' ', 0, 0);
    p->setFont(pixelFont(6, true));
    p->drawText(QRectF(b.left() + 2, b.bottom() - 16, b.width() - 4, 8), Qt::AlignCenter, title);
}

// ----------------------------- 출력 장치 --------------------------------------
void PartItem::drawBuzzer(QPainter* p)
{
    QRectF b = bodyRect();
    QPointF c = b.center();
    p->setPen(QPen(QColor(10, 10, 10), 1));
    p->setBrush(QColor(30, 30, 30));
    p->drawEllipse(c, 18, 18);
    p->setBrush(QColor(10, 10, 10));
    p->drawEllipse(c, 4, 4);
    p->setPen(Qt::white);
    p->setFont(pixelFont(8, true));
    p->drawText(QRectF(b.left(), b.top(), 14, 12), Qt::AlignCenter, "+");
    double f = m_sim.value("buzz").toDouble();
    if (f != 0.0) {
        p->setPen(QPen(QColor(255, 140, 0), 1.5));
        p->setBrush(Qt::NoBrush);
        for (int i = 1; i <= 3; ++i) p->drawArc(QRectF(c.x() - 18 - i * 5, c.y() - 18 - i * 5, 36 + i * 10, 36 + i * 10), 20 * 16, 50 * 16);
        p->setPen(QColor(200, 90, 0));
        p->setFont(pixelFont(9, true));
        p->drawText(QRectF(b.right() - 10, b.top() - 14, 60, 12), Qt::AlignLeft, f > 0 ? QString::fromUtf8("♪ %1Hz").arg((int)f) : QString::fromUtf8("♪ 삐-"));
    }
}

void PartItem::drawMotor(QPainter* p)
{
    QRectF b = bodyRect();
    p->setPen(QPen(QColor(110, 110, 110), 1));
    p->setBrush(QColor(200, 200, 205));
    p->drawRoundedRect(b.adjusted(2, 8, -2, 0), 6, 6);
    p->setBrush(QColor(160, 160, 165));
    p->drawRect(QRectF(b.center().x() - 3, b.top(), 6, 10));
    // 회전 표시 (축에 붙은 날개)
    p->save();
    p->translate(b.center().x(), b.top() + 4);
    p->rotate(m_anim);
    p->setPen(QPen(QColor(220, 60, 30), 3, Qt::SolidLine, Qt::RoundCap));
    p->drawLine(QPointF(-12, 0), QPointF(12, 0));
    p->restore();
    p->setPen(QColor(60, 60, 60));
    p->setFont(pixelFont(8, true));
    p->drawText(b.adjusted(0, 10, 0, 0), Qt::AlignCenter, "M");
}

void PartItem::drawServo(QPainter* p)
{
    QRectF b = bodyRect();
    QRectF body(b.left() + 10, b.top() + 12, b.width() - 20, b.height() - 16);
    p->setPen(QPen(QColor(10, 30, 90), 1));
    p->setBrush(paramS("model").startsWith("MG") ? QColor(30, 30, 30) : QColor(30, 70, 180));
    p->drawRect(body);
    p->setBrush(body.isValid() ? p->brush().color().darker(120) : Qt::black);
    p->drawRect(QRectF(body.left() - 8, body.top() + 8, body.width() + 16, 6));
    QPointF hub(body.left() + 20, body.top() + 2);
    p->setBrush(QColor(240, 240, 240));
    p->drawEllipse(hub, 9, 9);
    double angle = m_sim.contains("angle") ? m_sim.value("angle").toDouble() : 90.0;
    p->save();
    p->translate(hub);
    p->rotate(90.0 - angle);   // 0° → 오른쪽, 90° → 위, 180° → 왼쪽
    p->setBrush(Qt::white);
    p->setPen(QPen(QColor(120, 120, 120), 1));
    QPainterPath horn;
    horn.addRoundedRect(QRectF(-4, -30, 8, 34), 4, 4);
    p->drawPath(horn);
    p->restore();
    p->setPen(Qt::white);
    p->setFont(pixelFont(7, true));
    p->drawText(body.adjusted(30, 0, 0, 0), Qt::AlignCenter, paramS("model"));
    if (m_simulating) {
        p->setPen(Theme::accentText());
        p->setFont(pixelFont(9, true));
        p->drawText(QRectF(b.left(), b.top() - 14, 40, 12), Qt::AlignLeft, QString::fromUtf8("%1°").arg((int)angle));
    }
    // 케이블 색
    QColor wires[3] = { QColor(110, 60, 20), QColor(220, 30, 30), QColor(255, 140, 0) };
    for (int i = 0; i < 3 && i < m_def->pins.size(); ++i) {
        p->setPen(QPen(wires[i], 2));
        p->drawLine(m_def->pins[i].pos - QPointF(0, 10), QPointF(m_def->pins[i].pos.x(), body.bottom()));
    }
}

void PartItem::drawRelay(QPainter* p)
{
    QRectF b = bodyRect();
    p->setPen(QPen(QColor(10, 10, 60), 1));
    p->setBrush(QColor(30, 50, 140));
    p->drawRect(b);
    // 릴레이 본체
    p->setBrush(QColor(30, 90, 220));
    p->drawRect(QRectF(b.left() + 30, b.top() + 6, 46, 36));
    p->setPen(Qt::white);
    p->setFont(pixelFont(6));
    p->drawText(QRectF(b.left() + 30, b.top() + 6, 46, 36), Qt::AlignCenter, "SRD-05VDC\nSL-C");
    // 단자대
    p->setPen(QPen(QColor(20, 20, 80), 1));
    p->setBrush(QColor(40, 110, 230));
    p->drawRect(QRectF(b.left() + 52, b.top() - 2, 36, 8));
    bool on = m_sim.value("relayOn").toBool();
    p->setPen(Qt::NoPen);
    p->setBrush(on ? QColor(255, 40, 40) : QColor(80, 20, 20));
    p->drawEllipse(QPointF(b.left() + 12, b.top() + 14), 3, 3);
    p->setPen(Qt::white);
    p->setFont(pixelFont(6));
    for (const PinDef& pin : m_def->pins) {
        if (pin.pos.y() < 1) p->drawText(QRectF(pin.pos.x() - 8, b.top() + 7, 16, 7), Qt::AlignCenter, pin.name);
        else p->drawText(QRectF(pin.pos.x() - 8, b.bottom() - 8, 16, 7), Qt::AlignCenter, pin.name);
    }
    if (on) {
        p->setPen(QColor(255, 220, 120));
        p->setFont(pixelFont(8, true));
        p->drawText(QRectF(b.right() - 20, b.top() - 14, 60, 12), Qt::AlignLeft, "ON");
    }
}

void PartItem::drawStepper(QPainter* p)
{
    QRectF b = bodyRect();
    // ULN2003 보드
    QRectF board(b.left(), b.top() + 30, 80, b.height() - 30);
    p->setPen(QPen(QColor(10, 40, 10), 1));
    p->setBrush(QColor(30, 110, 50));
    p->drawRect(board);
    p->setBrush(QColor(20, 20, 20));
    p->drawRect(QRectF(board.left() + 12, board.top() + 10, 40, 14));
    p->setPen(Qt::white);
    p->setFont(pixelFont(6));
    p->drawText(QRectF(board.left() + 12, board.top() + 10, 40, 14), Qt::AlignCenter, "ULN2003");
    QVariantList coils = m_sim.value("coils").toList();
    for (int i = 0; i < 4; ++i) {
        bool on = i < coils.size() && coils[i].toBool();
        p->setPen(Qt::NoPen);
        p->setBrush(on ? QColor(255, 40, 40) : QColor(90, 20, 20));
        p->drawEllipse(QPointF(board.left() + 62, board.top() + 8 + i * 7), 2.5, 2.5);
    }
    for (const PinDef& pin : m_def->pins)
        p->drawText(QRectF(pin.pos.x() - 8, board.bottom() - 8, 16, 7), Qt::AlignCenter, pin.name);
    // 모터
    QPointF mc(b.right() - 22, b.top() + 26);
    p->setPen(QPen(QColor(130, 130, 130), 1));
    p->setBrush(QColor(200, 200, 205));
    p->drawEllipse(mc, 22, 22);
    p->setBrush(QColor(240, 180, 40));
    p->drawEllipse(mc - QPointF(0, 6), 6, 6);
    double ang = m_sim.value("stepAngle").toDouble();
    p->save();
    p->translate(mc - QPointF(0, 6));
    p->rotate(ang);
    p->setPen(QPen(QColor(200, 60, 20), 3, Qt::SolidLine, Qt::RoundCap));
    p->drawLine(QPointF(0, 0), QPointF(0, -14));
    p->restore();
}
