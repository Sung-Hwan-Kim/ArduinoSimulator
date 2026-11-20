#pragma once
#include <QGraphicsScene>
#include <QJsonObject>
#include <QPointer>
#include <functional>

class PartItem;
class WireItem;
struct PartDef;

class CircuitScene : public QGraphicsScene
{
    Q_OBJECT
public:
    static constexpr double kGrid = 10.0;

    explicit CircuitScene(QObject* parent = nullptr);

    // ---- 부품 ----
    PartItem* addPart(const QString& defId, const QPointF& pos, const QString& ref = QString());
    void removePart(PartItem* part);
    QList<PartItem*> parts() const;
    PartItem* board() const;                         // 첫 번째 아두이노 보드
    PartItem* findPart(const QString& uid) const;
    QString nextRef(const QString& prefix) const;

    // ---- 배선 ----
    WireItem* addWire(PartItem* a, int pinA, PartItem* b, int pinB, const QVector<QPointF>& bends = {}, const QColor& color = QColor());
    void removeWire(WireItem* w);
    QList<WireItem*> wires() const;
    bool hasWire(PartItem* a, int pinA, PartItem* b, int pinB) const;
    void cancelWiring();
    bool isWiring() const { return m_wiring; }

    void deleteSelection();
    void rotateSelection();
    void duplicateSelection();
    void clearAll();

    // ---- 상태 ----
    bool snapToGrid() const { return m_snap; }
    void setSnapToGrid(bool on) { m_snap = on; }
    void setShowGrid(bool on) { m_showGrid = on; update(); }
    bool showGrid() const { return m_showGrid; }
    void setSimulating(bool on);
    bool simulating() const { return m_simulating; }
    void setDefaultWireColor(const QColor& c) { m_wireColor = c; }

    void notifyModified() { if (!m_loading) emit modified(); }
    void notifyParamChanged(PartItem* part, const QString& key) { emit partParamChanged(part, key); }

    // 핀 툴팁 (시뮬레이션 중이면 전압 포함)
    QString pinToolTip(PartItem* part, int pin) const;
    void setPinInfoProvider(std::function<QString(PartItem*, int)> f) { m_pinInfo = std::move(f); }

    // ---- 저장 / 불러오기 ----
    QJsonObject toJson() const;
    bool fromJson(const QJsonObject& obj, QString* error = nullptr);

    // 배선 색상 (기본 팔레트)
    static QList<QPair<QString, QColor>> wireColors();

signals:
    void modified();
    void partParamChanged(PartItem* part, const QString& key);
    void partAdded(PartItem* part);
    void statusMessage(const QString& msg);

protected:
    void drawBackground(QPainter* p, const QRectF& rect) override;
    void drawForeground(QPainter* p, const QRectF& rect) override;
    void mousePressEvent(QGraphicsSceneMouseEvent* e) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent* e) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;
    void contextMenuEvent(QGraphicsSceneContextMenuEvent* e) override;
    void dragEnterEvent(QGraphicsSceneDragDropEvent* e) override;
    void dragMoveEvent(QGraphicsSceneDragDropEvent* e) override;
    void dropEvent(QGraphicsSceneDragDropEvent* e) override;

private:
    QPair<PartItem*, int> pinUnder(const QPointF& scenePos) const;
    QPointF snap(const QPointF& p) const;
    QColor autoWireColor(PartItem* a, int pinA, PartItem* b, int pinB) const;

    bool m_snap = true;
    bool m_showGrid = true;
    bool m_simulating = false;
    bool m_loading = false;
    QColor m_wireColor;
    // 배선 중 상태
    bool m_wiring = false;
    PartItem* m_wireFrom = nullptr;
    int m_wireFromPin = -1;
    QVector<QPointF> m_wireBends;
    QPointF m_mousePos;
    std::function<QString(PartItem*, int)> m_pinInfo;
};
