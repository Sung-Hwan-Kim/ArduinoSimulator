#include "CircuitScene.h"
#include "Catalog.h"
#include "PartItem.h"
#include "Theme.h"
#include "WireItem.h"

#include <QGraphicsSceneContextMenuEvent>
#include <QGraphicsSceneDragDropEvent>
#include <QGraphicsSceneMouseEvent>
#include <QJsonArray>
#include <QKeyEvent>
#include <QMenu>
#include <QMimeData>
#include <QPainter>
#include <cmath>

static const char* kPartMime = "application/x-arduinosim-part";

CircuitScene::CircuitScene(QObject* parent)
    : QGraphicsScene(parent)
{
    setSceneRect(-2000, -1500, 4000, 3000);
    setItemIndexMethod(QGraphicsScene::NoIndex);
}

QList<QPair<QString, QColor>> CircuitScene::wireColors()
{
    return {
        { QStringLiteral("빨강 (전원)"), QColor(220, 30, 30) },
        { QStringLiteral("검정 (GND)"), QColor(40, 40, 40) },
        { QStringLiteral("주황"), QColor(255, 140, 0) },
        { QStringLiteral("노랑"), QColor(240, 200, 0) },
        { QStringLiteral("초록"), QColor(30, 160, 60) },
        { QStringLiteral("파랑"), QColor(30, 120, 220) },
        { QStringLiteral("보라"), QColor(150, 60, 200) },
        { QStringLiteral("회색"), QColor(140, 140, 140) },
        { QStringLiteral("흰색"), QColor(245, 245, 245) },
        { QStringLiteral("갈색"), QColor(130, 70, 20) },
        { QStringLiteral("청록"), QColor(0, 170, 170) },
    };
}

// ---------------------------------------------------------------------------
// 부품
// ---------------------------------------------------------------------------
QString CircuitScene::nextRef(const QString& prefix) const
{
    int maxN = 0;
    for (PartItem* p : parts()) {
        if (p->def()->prefix != prefix) continue;
        bool ok = false;
        int n = p->ref().mid(prefix.size()).toInt(&ok);
        if (ok) maxN = qMax(maxN, n);
    }
    return prefix + QString::number(maxN + 1);
}

PartItem* CircuitScene::addPart(const QString& defId, const QPointF& pos, const QString& ref)
{
    const PartDef* def = Catalog::instance().find(defId);
    if (!def) return nullptr;
    auto* item = new PartItem(def, ref.isEmpty() ? nextRef(def->prefix) : ref);
    addItem(item);
    item->setPos(snap(pos));
    item->setSimulating(m_simulating);
    emit partAdded(item);
    notifyModified();
    return item;
}

void CircuitScene::removePart(PartItem* part)
{
    if (!part) return;
    const QList<WireItem*> ws = part->wires();
    for (WireItem* w : ws) removeWire(w);
    removeItem(part);
    delete part;
    notifyModified();
}

QList<PartItem*> CircuitScene::parts() const
{
    QList<PartItem*> out;
    for (QGraphicsItem* it : items(Qt::AscendingOrder))
        if (it->type() == PartItem::Type) out << static_cast<PartItem*>(it);
    return out;
}

PartItem* CircuitScene::board() const
{
    for (PartItem* p : parts()) if (p->def()->isBoard) return p;
    return nullptr;
}

PartItem* CircuitScene::findPart(const QString& uid) const
{
    for (PartItem* p : parts()) if (p->uid() == uid) return p;
    return nullptr;
}

// ---------------------------------------------------------------------------
// 배선
// ---------------------------------------------------------------------------
QColor CircuitScene::autoWireColor(PartItem* a, int pinA, PartItem* b, int pinB) const
{
    auto kind = [](PartItem* p, int pin) -> int {
        const PinDef& d = p->def()->pins[pin];
        if (d.bkind == BoardPinKind::Gnd || d.role == PinRole::Gnd || d.role == PinRole::SwitchToGnd) return 1;
        if (d.bkind == BoardPinKind::P5V || d.bkind == BoardPinKind::Vin || d.role == PinRole::Vcc || d.role == PinRole::DividerHigh) return 2;
        if (d.bkind == BoardPinKind::P3V3) return 3;
        return 0;
    };
    int ka = kind(a, pinA), kb = kind(b, pinB);
    if (ka == 1 || kb == 1) return QColor(40, 40, 40);
    if (ka == 3 || kb == 3) return QColor(255, 140, 0);
    if (ka == 2 || kb == 2) return QColor(220, 30, 30);
    if (m_wireColor.isValid()) return m_wireColor;
    static const QColor cycle[] = { QColor(30, 120, 220), QColor(240, 200, 0), QColor(30, 160, 60), QColor(150, 60, 200),
                                    QColor(0, 170, 170), QColor(255, 140, 0), QColor(130, 70, 20) };
    return cycle[wires().size() % 7];
}

WireItem* CircuitScene::addWire(PartItem* a, int pinA, PartItem* b, int pinB, const QVector<QPointF>& bends, const QColor& color)
{
    if (!a || !b || (a == b && pinA == pinB)) return nullptr;
    if (hasWire(a, pinA, b, pinB)) return nullptr;
    auto* w = new WireItem(a, pinA, b, pinB, bends);
    w->setColor(color.isValid() ? color : autoWireColor(a, pinA, b, pinB));
    addItem(w);
    notifyModified();
    return w;
}

void CircuitScene::removeWire(WireItem* w)
{
    if (!w) return;
    w->detach();
    removeItem(w);
    delete w;
    notifyModified();
}

QList<WireItem*> CircuitScene::wires() const
{
    QList<WireItem*> out;
    for (QGraphicsItem* it : items(Qt::AscendingOrder))
        if (it->type() == WireItem::Type) out << static_cast<WireItem*>(it);
    return out;
}

bool CircuitScene::hasWire(PartItem* a, int pinA, PartItem* b, int pinB) const
{
    for (WireItem* w : wires()) {
        if (w->partA() == a && w->pinA() == pinA && w->partB() == b && w->pinB() == pinB) return true;
        if (w->partA() == b && w->pinA() == pinB && w->partB() == a && w->pinB() == pinA) return true;
    }
    return false;
}

void CircuitScene::cancelWiring()
{
    m_wiring = false;
    m_wireFrom = nullptr;
    m_wireBends.clear();
    update();
}

void CircuitScene::deleteSelection()
{
    if (m_simulating) return;
    const QList<QGraphicsItem*> sel = selectedItems();
    QList<WireItem*> ws;
    QList<PartItem*> ps;
    for (QGraphicsItem* it : sel) {
        if (it->type() == WireItem::Type) ws << static_cast<WireItem*>(it);
        else if (it->type() == PartItem::Type) ps << static_cast<PartItem*>(it);
    }
    for (WireItem* w : ws) {
        bool ownedByDeletedPart = false;
        for (PartItem* p : ps) if (w->partA() == p || w->partB() == p) ownedByDeletedPart = true;
        if (!ownedByDeletedPart) removeWire(w);
    }
    for (PartItem* p : ps) removePart(p);
}

void CircuitScene::rotateSelection()
{
    if (m_simulating) return;
    for (QGraphicsItem* it : selectedItems())
        if (it->type() == PartItem::Type) static_cast<PartItem*>(it)->rotate90();
}

void CircuitScene::duplicateSelection()
{
    if (m_simulating) return;
    QList<PartItem*> src;
    for (QGraphicsItem* it : selectedItems())
        if (it->type() == PartItem::Type) src << static_cast<PartItem*>(it);
    clearSelection();
    for (PartItem* p : src) {
        PartItem* n = addPart(p->def()->id, p->pos() + QPointF(40, 40));
        if (!n) continue;
        n->setParams(p->params());
        n->setRotationDeg(p->rotationDeg());
        n->setSelected(true);
    }
}

void CircuitScene::clearAll()
{
    cancelWiring();
    for (WireItem* w : wires()) removeWire(w);
    for (PartItem* p : parts()) removePart(p);
}

void CircuitScene::setSimulating(bool on)
{
    m_simulating = on;
    cancelWiring();
    for (PartItem* p : parts()) p->setSimulating(on);
    if (!on) for (WireItem* w : wires()) w->setCurrentInfo(0);
    update();
}

QPair<PartItem*, int> CircuitScene::pinUnder(const QPointF& scenePos) const
{
    // 부품(보드 제외)을 우선, 위에 있는 것부터
    PartItem* best = nullptr;
    int bestPin = -1;
    for (QGraphicsItem* it : items(QRectF(scenePos - QPointF(8, 8), QSizeF(16, 16)), Qt::IntersectsItemBoundingRect, Qt::DescendingOrder)) {
        if (it->type() != PartItem::Type) continue;
        auto* p = static_cast<PartItem*>(it);
        int pin = p->pinAt(scenePos, 6.5);
        if (pin >= 0) {
            if (!best || (best->def()->isBoard && !p->def()->isBoard)) { best = p; bestPin = pin; }
        }
    }
    return { best, bestPin };
}

QPointF CircuitScene::snap(const QPointF& p) const
{
    if (!m_snap) return p;
    return QPointF(std::round(p.x() / kGrid) * kGrid, std::round(p.y() / kGrid) * kGrid);
}

QString CircuitScene::pinToolTip(PartItem* part, int pin) const
{
    const PinDef& d = part->def()->pins[pin];
    QString t = QString("<b>%1 · %2</b>").arg(part->ref(), d.name);
    if (part->def()->isBoard) {
        QStringList info;
        if (d.ardPin >= 0) info << QString("핀 번호 %1").arg(d.ardPin);
        if (d.adc >= 0) info << QString("아날로그 A%1").arg(d.adc);
        if (d.pwm) info << "PWM";
        if (d.irq >= 0) info << QString("인터럽트 %1").arg(d.irq);
        if (!d.func.isEmpty()) info << d.func;
        if (!info.isEmpty()) t += "<br>" + info.join(", ");
    } else {
        static const QMap<PinRole, QString> roleText = {
            { PinRole::Gnd, "GND 에 연결" }, { PinRole::Vcc, "전원(5V) 에 연결" },
            { PinRole::SigDigitalOut, "디지털 출력 → 아두이노 디지털 입력" }, { PinRole::SigAnalogOut, "아날로그 출력 → 아두이노 아날로그 입력" },
            { PinRole::SigDigitalIn, "디지털 입력 ← 아두이노 출력" }, { PinRole::SigPwmIn, "PWM 입력 ← 아두이노 PWM 핀" },
            { PinRole::SigBidir, "데이터 (양방향)" }, { PinRole::I2cSda, "I2C SDA" }, { PinRole::I2cScl, "I2C SCL" },
            { PinRole::LedAnode, "애노드(+) → 저항 → 아두이노 핀" }, { PinRole::LedCathode, "캐소드(-) → GND" },
            { PinRole::SwitchToPin, "스위치 → 디지털 핀 (INPUT_PULLUP)" }, { PinRole::SwitchToGnd, "스위치 → GND" },
            { PinRole::DividerHigh, "분압 상단 → 5V" }, { PinRole::DividerOut, "분압 출력 → 아날로그 핀 (+ 풀다운 저항)" },
        };
        if (roleText.contains(d.role)) t += "<br>" + roleText.value(d.role);
    }
    if (m_pinInfo) {
        QString extra = m_pinInfo(part, pin);
        if (!extra.isEmpty()) t += "<br><span style='color:#0050c0'>" + extra + "</span>";
    }
    return t;
}

// ---------------------------------------------------------------------------
// 그리기
// ---------------------------------------------------------------------------
void CircuitScene::drawBackground(QPainter* p, const QRectF& rect)
{
    p->fillRect(rect, Theme::canvasBg());
    if (!m_showGrid) return;
    const double g = kGrid;
    double left = std::floor(rect.left() / g) * g;
    double top = std::floor(rect.top() / g) * g;
    QVector<QPointF> dots;
    QVector<QLineF> major;
    for (double x = left; x < rect.right(); x += g) {
        for (double y = top; y < rect.bottom(); y += g) dots << QPointF(x, y);
    }
    for (double x = std::floor(rect.left() / 100) * 100; x < rect.right(); x += 100) major << QLineF(x, rect.top(), x, rect.bottom());
    for (double y = std::floor(rect.top() / 100) * 100; y < rect.bottom(); y += 100) major << QLineF(rect.left(), y, rect.right(), y);
    p->setPen(QPen(Theme::gridMajor(), 0));
    p->drawLines(major);
    if (dots.size() < 60000) {
        p->setPen(QPen(Theme::gridDot(), 1.2));
        p->drawPoints(dots.data(), dots.size());
    }
}

void CircuitScene::drawForeground(QPainter* p, const QRectF&)
{
    if (!m_wiring || !m_wireFrom) return;
    p->setRenderHint(QPainter::Antialiasing, true);
    QPainterPath path(m_wireFrom->pinScenePos(m_wireFromPin));
    for (const QPointF& b : m_wireBends) path.lineTo(b);
    QPointF last = path.currentPosition();
    QPointF target = m_mousePos;
    auto hit = pinUnder(m_mousePos);
    if (hit.first) target = hit.first->pinScenePos(hit.second);
    else target = snap(target);
    path.lineTo(QPointF(target.x(), last.y()));
    path.lineTo(target);
    p->setPen(QPen(QColor(0, 140, 255, 200), 2.5, Qt::DashLine, Qt::RoundCap, Qt::RoundJoin));
    p->drawPath(path);
    if (hit.first) {
        p->setPen(QPen(QColor(0, 200, 80), 2));
        p->setBrush(Qt::NoBrush);
        p->drawEllipse(target, 6, 6);
    }
}

// ---------------------------------------------------------------------------
// 마우스 / 키보드
// ---------------------------------------------------------------------------
void CircuitScene::mousePressEvent(QGraphicsSceneMouseEvent* e)
{
    m_mousePos = e->scenePos();
    if (m_simulating) { QGraphicsScene::mousePressEvent(e); return; }

    if (e->button() == Qt::RightButton && m_wiring) {
        cancelWiring();
        emit statusMessage(QStringLiteral("배선을 취소했습니다."));
        e->accept();
        return;
    }
    if (e->button() != Qt::LeftButton) { QGraphicsScene::mousePressEvent(e); return; }

    auto hit = pinUnder(e->scenePos());
    if (m_wiring) {
        QPointF last = m_wireBends.isEmpty() ? m_wireFrom->pinScenePos(m_wireFromPin) : m_wireBends.last();
        if (hit.first) {
            if (hit.first == m_wireFrom && hit.second == m_wireFromPin) { cancelWiring(); e->accept(); return; }
            QPointF target = hit.first->pinScenePos(hit.second);
            QVector<QPointF> bends = m_wireBends;
            QPointF corner(target.x(), last.y());
            if (corner != last && corner != target) bends << corner;
            // 같은 직선 위 중간점 제거
            QVector<QPointF> pts;
            pts << m_wireFrom->pinScenePos(m_wireFromPin) << bends << target;
            QVector<QPointF> clean;
            for (int i = 0; i < pts.size(); ++i) {
                if (!clean.isEmpty() && clean.last() == pts[i]) continue;
                if (clean.size() >= 2) {
                    QPointF a = clean[clean.size() - 2], b = clean.last(), c = pts[i];
                    bool col = (std::fabs(a.x() - b.x()) < 0.1 && std::fabs(b.x() - c.x()) < 0.1) ||
                               (std::fabs(a.y() - b.y()) < 0.1 && std::fabs(b.y() - c.y()) < 0.1);
                    if (col) clean.removeLast();
                }
                clean << pts[i];
            }
            clean.removeFirst();
            clean.removeLast();
            WireItem* w = addWire(m_wireFrom, m_wireFromPin, hit.first, hit.second, clean);
            if (w) emit statusMessage(QString("연결: %1.%2 ↔ %3.%4").arg(m_wireFrom->ref(), m_wireFrom->def()->pins[m_wireFromPin].name,
                                                                        hit.first->ref(), hit.first->def()->pins[hit.second].name));
            cancelWiring();
        } else {
            QPointF pt = snap(e->scenePos());
            QPointF corner(pt.x(), last.y());
            if (corner != last && corner != pt) m_wireBends << corner;
            if (pt != last) m_wireBends << pt;
            update();
        }
        e->accept();
        return;
    }
    if (hit.first) {
        m_wiring = true;
        m_wireFrom = hit.first;
        m_wireFromPin = hit.second;
        m_wireBends.clear();
        emit statusMessage(QStringLiteral("배선 중: 연결할 핀을 클릭하세요 (빈 곳 클릭=꺾임점, 우클릭/ESC=취소)"));
        e->accept();
        return;
    }
    QGraphicsScene::mousePressEvent(e);
}

void CircuitScene::mouseMoveEvent(QGraphicsSceneMouseEvent* e)
{
    m_mousePos = e->scenePos();
    if (m_wiring) update();
    QGraphicsScene::mouseMoveEvent(e);
}

void CircuitScene::mouseReleaseEvent(QGraphicsSceneMouseEvent* e)
{
    QGraphicsScene::mouseReleaseEvent(e);
}

void CircuitScene::keyPressEvent(QKeyEvent* e)
{
    if (e->key() == Qt::Key_Escape && m_wiring) { cancelWiring(); return; }
    if (focusItem()) { QGraphicsScene::keyPressEvent(e); return; }
    if (e->key() == Qt::Key_Delete || e->key() == Qt::Key_Backspace) { deleteSelection(); return; }
    if (e->key() == Qt::Key_R && !(e->modifiers() & Qt::ControlModifier)) { rotateSelection(); return; }
    QGraphicsScene::keyPressEvent(e);
}

void CircuitScene::contextMenuEvent(QGraphicsSceneContextMenuEvent* e)
{
    if (m_simulating || m_wiring) return;
    QGraphicsItem* it = itemAt(e->scenePos(), QTransform());
    QMenu menu;
    if (it && it->type() == WireItem::Type) {
        auto* w = static_cast<WireItem*>(it);
        QMenu* colors = menu.addMenu(QStringLiteral("전선 색상"));
        for (const auto& c : wireColors()) {
            QPixmap pm(12, 12); pm.fill(c.second);
            colors->addAction(QIcon(pm), c.first, [w, col = c.second] { w->setColor(col); });
        }
        menu.addAction(QStringLiteral("꺾임점 정리 (직각 경로)"), [w] {
            QPointF a = w->partA()->pinScenePos(w->pinA()), b = w->partB()->pinScenePos(w->pinB());
            w->setBends({ QPointF(b.x(), a.y()) });
        });
        menu.addSeparator();
        menu.addAction(QStringLiteral("전선 삭제"), [this, w] { removeWire(w); });
    } else if (it && it->type() == PartItem::Type) {
        auto* p = static_cast<PartItem*>(it);
        if (!p->isSelected()) { clearSelection(); p->setSelected(true); }
        menu.addAction(QStringLiteral("90° 회전 (R)"), [this] { rotateSelection(); });
        menu.addAction(QStringLiteral("복제"), [this] { duplicateSelection(); });
        menu.addSeparator();
        menu.addAction(QStringLiteral("연결된 전선 모두 삭제"), [this, p] {
            const QList<WireItem*> ws = p->wires();
            for (WireItem* w : ws) removeWire(w);
        });
        menu.addAction(QStringLiteral("삭제 (Del)"), [this] { deleteSelection(); });
    } else {
        return;
    }
    menu.exec(e->screenPos());
}

void CircuitScene::dragEnterEvent(QGraphicsSceneDragDropEvent* e)
{
    if (e->mimeData()->hasFormat(kPartMime) && !m_simulating) e->acceptProposedAction();
    else e->ignore();
}

void CircuitScene::dragMoveEvent(QGraphicsSceneDragDropEvent* e)
{
    if (e->mimeData()->hasFormat(kPartMime) && !m_simulating) e->acceptProposedAction();
    else e->ignore();
}

void CircuitScene::dropEvent(QGraphicsSceneDragDropEvent* e)
{
    if (!e->mimeData()->hasFormat(kPartMime) || m_simulating) return;
    QString id = QString::fromUtf8(e->mimeData()->data(kPartMime));
    const PartDef* def = Catalog::instance().find(id);
    if (!def) return;
    PartItem* p = addPart(id, e->scenePos() - QPointF(def->size.width() / 2, def->size.height() / 2));
    if (p) { clearSelection(); p->setSelected(true); }
    e->acceptProposedAction();
}

// ---------------------------------------------------------------------------
// JSON
// ---------------------------------------------------------------------------
QJsonObject CircuitScene::toJson() const
{
    QJsonObject root;
    root["format"] = "ArduinoSimulator";
    root["version"] = 1;
    QJsonArray ps;
    for (PartItem* p : parts()) {
        QJsonObject o;
        o["uid"] = p->uid();
        o["type"] = p->def()->id;
        o["ref"] = p->ref();
        o["x"] = p->pos().x();
        o["y"] = p->pos().y();
        o["rot"] = p->rotationDeg();
        o["params"] = QJsonObject::fromVariantMap(p->params());
        ps.append(o);
    }
    root["parts"] = ps;
    QJsonArray ws;
    for (WireItem* w : wires()) {
        QJsonObject o;
        o["a"] = w->partA()->uid();
        o["pa"] = w->pinA();
        o["b"] = w->partB()->uid();
        o["pb"] = w->pinB();
        o["color"] = w->color().name();
        QJsonArray pts;
        for (const QPointF& b : w->bends()) pts.append(QJsonArray{ b.x(), b.y() });
        o["bends"] = pts;
        ws.append(o);
    }
    root["wires"] = ws;
    return root;
}

bool CircuitScene::fromJson(const QJsonObject& root, QString* error)
{
    m_loading = true;
    clearAll();
    QStringList missing;
    for (const QJsonValue& v : root["parts"].toArray()) {
        QJsonObject o = v.toObject();
        QString type = o["type"].toString();
        const PartDef* def = Catalog::instance().find(type);
        if (!def) { missing << type; continue; }
        auto* item = new PartItem(def, o["ref"].toString());
        item->setUid(o["uid"].toString());
        QVariantMap params = def->defaultParams();
        QVariantMap saved = o["params"].toObject().toVariantMap();
        for (auto it = saved.begin(); it != saved.end(); ++it) params[it.key()] = it.value();
        item->setParams(params);
        addItem(item);
        item->setPos(o["x"].toDouble(), o["y"].toDouble());
        item->setRotationDeg(o["rot"].toInt());
    }
    for (const QJsonValue& v : root["wires"].toArray()) {
        QJsonObject o = v.toObject();
        PartItem* a = findPart(o["a"].toString());
        PartItem* b = findPart(o["b"].toString());
        int pa = o["pa"].toInt(), pb = o["pb"].toInt();
        if (!a || !b || pa >= a->pinCount() || pb >= b->pinCount()) continue;
        QVector<QPointF> bends;
        for (const QJsonValue& pv : o["bends"].toArray()) {
            QJsonArray xy = pv.toArray();
            bends << QPointF(xy[0].toDouble(), xy[1].toDouble());
        }
        auto* w = new WireItem(a, pa, b, pb, bends);
        w->setColor(QColor(o["color"].toString()));
        addItem(w);
    }
    m_loading = false;
    if (!missing.isEmpty() && error) *error = QStringLiteral("알 수 없는 부품: ") + missing.join(", ");
    return true;
}
