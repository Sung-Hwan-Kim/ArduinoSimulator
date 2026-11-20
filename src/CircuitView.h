#pragma once
#include <QGraphicsView>

class CircuitView : public QGraphicsView
{
    Q_OBJECT
public:
    explicit CircuitView(QWidget* parent = nullptr);
    void zoomBy(double factor);
    void zoomToFit();
    double zoomLevel() const { return transform().m11(); }

signals:
    void zoomChanged(double level);

protected:
    void wheelEvent(QWheelEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;

private:
    bool m_panning = false;
    QPoint m_panStart;
};
