#pragma once
#include "Catalog.h"
#include <QGraphicsItem>
#include <QList>
#include <QVariantMap>

class WireItem;

class PartItem : public QGraphicsItem
{
public:
    enum { Type = UserType + 1 };
    PartItem(const PartDef* def, const QString& ref);
    ~PartItem() override;

    int type() const override { return Type; }
    const PartDef* def() const { return m_def; }

    QString ref() const { return m_ref; }
    void setRef(const QString& r) { m_ref = r; update(); }
    QString uid() const { return m_uid; }
    void setUid(const QString& u) { m_uid = u; }
    // 코드 생성용 변수 이름 (예: led1)
    QString varName() const;

    // ---- 파라미터 ----
    const QVariantMap& params() const { return m_params; }
    void setParams(const QVariantMap& p);
    QVariant param(const QString& key) const { return m_params.value(key); }
    double paramD(const QString& key, double def = 0.0) const;
    QString paramS(const QString& key) const { return m_params.value(key).toString(); }
    bool paramB(const QString& key) const { return m_params.value(key).toBool(); }
    // Choice 의 sets 도 함께 적용. 바뀐 키 목록을 반환
    QStringList setParam(const QString& key, const QVariant& v);
    QString specSummary() const;   // 부품 규격 한 줄 요약

    // ---- 핀 ----
    int pinCount() const { return m_def->pins.size(); }
    QPointF pinScenePos(int idx) const;
    int pinAt(const QPointF& scenePos, double radius = 6.0) const;
    void setHoverPin(int idx);
    int hoverPin() const { return m_hoverPin; }

    // ---- 배선 ----
    void addWire(WireItem* w) { if (!m_wires.contains(w)) m_wires.append(w); }
    void removeWire(WireItem* w) { m_wires.removeAll(w); }
    const QList<WireItem*>& wires() const { return m_wires; }
    bool pinHasWire(int pin) const;

    // ---- 시뮬레이션 표시 ----
    void setSimulating(bool on);
    bool simulating() const { return m_simulating; }
    void setSimState(const QVariantMap& s);
    const QVariantMap& simState() const { return m_sim; }

    // ---- 회전 ----
    void rotate90();
    int rotationDeg() const { return m_rot; }
    void setRotationDeg(int deg);

    QRectF boundingRect() const override;
    QPainterPath shape() const override;
    void paint(QPainter* p, const QStyleOptionGraphicsItem* opt, QWidget* w) override;

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant& value) override;
    void hoverMoveEvent(QGraphicsSceneHoverEvent* e) override;
    void hoverLeaveEvent(QGraphicsSceneHoverEvent* e) override;
    void mousePressEvent(QGraphicsSceneMouseEvent* e) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* e) override;
    void wheelEvent(QGraphicsSceneWheelEvent* e) override;

private:
    QRectF bodyRect() const;
    void notifyParam(const QString& key);
    // 모양별 그리기
    void drawLegs(QPainter* p);
    void drawPins(QPainter* p);
    void drawBoard(QPainter* p);
    void drawResistor(QPainter* p);
    void drawLed(QPainter* p);
    void drawRgbLed(QPainter* p);
    void drawSevenSeg(QPainter* p);
    void drawButton(QPainter* p);
    void drawSlide(QPainter* p);
    void drawPot(QPainter* p);
    void drawLdr(QPainter* p);
    void drawNtc(QPainter* p);
    void drawCapCeramic(QPainter* p);
    void drawCapElec(QPainter* p);
    void drawDiode(QPainter* p);
    void drawTo92(QPainter* p);
    void drawTo220(QPainter* p);
    void drawBuzzer(QPainter* p);
    void drawMotor(QPainter* p);
    void drawServo(QPainter* p);
    void drawLcd(QPainter* p);
    void drawNeoRing(QPainter* p);
    void drawNeoStrip(QPainter* p);
    void drawDht(QPainter* p);
    void drawHcsr04(QPainter* p);
    void drawModule(QPainter* p);
    void drawRelay(QPainter* p);
    void drawStepper(QPainter* p);
    void drawJoystick(QPainter* p);
    void drawTilt(QPainter* p);
    void drawReed(QPainter* p);
    void drawWarning(QPainter* p);
    void drawGlow(QPainter* p, const QPointF& c, double radius, const QColor& col, double level);

    const PartDef* m_def;
    QString m_ref;
    QString m_uid;
    QVariantMap m_params;
    QList<WireItem*> m_wires;
    QVariantMap m_sim;
    bool m_simulating = false;
    int m_hoverPin = -1;
    int m_rot = 0;
    double m_anim = 0.0;
    bool m_pressedByMouse = false;
};
