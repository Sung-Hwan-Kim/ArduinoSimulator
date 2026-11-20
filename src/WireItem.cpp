#include "WireItem.h"
#include "PartItem.h"
#include "Theme.h"

#include <QPainter>
#include <QPainterPathStroker>
#include <QStyleOptionGraphicsItem>
#include <cmath>

WireItem::WireItem(PartItem* a, int pinA, PartItem* b, int pinB, const QVector<QPointF>& bends)
    : m_a(a), m_b(b), m_pinA(pinA), m_pinB(pinB), m_bends(bends)
{
    setFlags(ItemIsSelectable);
    setAcceptHoverEvents(true);
    setZValue(20);
    if (m_a) m_a->addWire(this);
    if (m_b) m_b->addWire(this);
    updatePath();
}

WireItem::~WireItem()
{
    detach();
}

void WireItem::detach()
{
    if (m_a) m_a->removeWire(this);
    if (m_b) m_b->removeWire(this);
    m_a = m_b = nullptr;
}

void WireItem::setColor(const QColor& c)
{
    m_color = c;
    update();
}

void WireItem::updatePath()
{
    if (!m_a || !m_b) return;
    QPointF pa = m_a->pinScenePos(m_pinA);
    QPointF pb = m_b->pinScenePos(m_pinB);

    // 끝점 이동 시, 인접 꺾임점을 따라 움직여 직교 형태 유지
    if (m_hasLast && !m_bends.isEmpty()) {
        auto follow = [](const QPointF& oldEnd, const QPointF& newEnd, QPointF& bend) {
            if (std::fabs(bend.x() - oldEnd.x()) < 0.5) bend.setX(newEnd.x());
            else if (std::fabs(bend.y() - oldEnd.y()) < 0.5) bend.setY(newEnd.y());
        };
        if (pa != m_lastA) follow(m_lastA, pa, m_bends.first());
        if (pb != m_lastB) follow(m_lastB, pb, m_bends.last());
    }
    m_lastA = pa; m_lastB = pb; m_hasLast = true;

    QPainterPath path(pa);
    for (const QPointF& b : m_bends) path.lineTo(b);
    path.lineTo(pb);
    prepareGeometryChange();
    setPath(path);
}

QPainterPath WireItem::shape() const
{
    QPainterPathStroker s;
    s.setWidth(8);
    return s.createStroke(path());
}

void WireItem::paint(QPainter* p, const QStyleOptionGraphicsItem*, QWidget*)
{
    p->setRenderHint(QPainter::Antialiasing, true);
    bool sel = isSelected();
    if (sel || m_hover) {
        p->setPen(QPen(sel ? QColor(0, 160, 255, 140) : QColor(255, 255, 255, 160), 7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p->drawPath(path());
    }
    // 다크 모드: 검정/갈색 같은 어두운 전선이 배경에 묻히지 않도록 밝은 테두리
    if (Theme::dark() && m_color.lightness() < 90) {
        p->setPen(QPen(Theme::wireHalo(), 5.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p->drawPath(path());
    }
    p->setPen(QPen(m_color.darker(150), 4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p->drawPath(path());
    p->setPen(QPen(m_color, 2.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p->drawPath(path());
    // 꺾임점
    p->setPen(Qt::NoPen);
    p->setBrush(m_color.darker(130));
    for (const QPointF& b : m_bends) p->drawEllipse(b, 2.2, 2.2);
}

QVariant WireItem::itemChange(GraphicsItemChange change, const QVariant& value)
{
    return QGraphicsPathItem::itemChange(change, value);
}

void WireItem::hoverEnterEvent(QGraphicsSceneHoverEvent* e)
{
    m_hover = true;
    if (m_a && m_b) {
        QString t = QString("%1.%2  ↔  %3.%4")
                        .arg(m_a->ref(), m_a->def()->pins[m_pinA].name, m_b->ref(), m_b->def()->pins[m_pinB].name);
        if (std::fabs(m_current) > 1e-6) t += QString("\n전류: %1 mA").arg(m_current * 1000.0, 0, 'f', 2);
        setToolTip(t);
    }
    update();
    QGraphicsPathItem::hoverEnterEvent(e);
}

void WireItem::hoverLeaveEvent(QGraphicsSceneHoverEvent* e)
{
    m_hover = false;
    update();
    QGraphicsPathItem::hoverLeaveEvent(e);
}
