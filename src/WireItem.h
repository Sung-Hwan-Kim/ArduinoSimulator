#pragma once
#include <QColor>
#include <QGraphicsPathItem>
#include <QVector>

class PartItem;

class WireItem : public QGraphicsPathItem
{
public:
    enum { Type = UserType + 2 };
    WireItem(PartItem* a, int pinA, PartItem* b, int pinB, const QVector<QPointF>& bends = {});
    ~WireItem() override;

    int type() const override { return Type; }
    PartItem* partA() const { return m_a; }
    PartItem* partB() const { return m_b; }
    int pinA() const { return m_pinA; }
    int pinB() const { return m_pinB; }

    QColor color() const { return m_color; }
    void setColor(const QColor& c);

    const QVector<QPointF>& bends() const { return m_bends; }
    void setBends(const QVector<QPointF>& b) { m_bends = b; updatePath(); }

    // 부품이 움직이면 호출됨: 끝점 갱신 + 인접 꺾임점을 직교로 유지
    void updatePath();
    // 부품에서 분리 (삭제 전)
    void detach();

    void setCurrentInfo(double amps) { m_current = amps; update(); }

    QPainterPath shape() const override;
    void paint(QPainter* p, const QStyleOptionGraphicsItem* opt, QWidget* w) override;

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant& value) override;
    void hoverEnterEvent(QGraphicsSceneHoverEvent* e) override;
    void hoverLeaveEvent(QGraphicsSceneHoverEvent* e) override;

private:
    PartItem* m_a;
    PartItem* m_b;
    int m_pinA, m_pinB;
    QVector<QPointF> m_bends;
    QPointF m_lastA, m_lastB;
    bool m_hasLast = false;
    QColor m_color = QColor(30, 120, 220);
    bool m_hover = false;
    double m_current = 0.0;
};
