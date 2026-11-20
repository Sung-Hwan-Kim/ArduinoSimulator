#include "CircuitView.h"
#include <QGraphicsItem>
#include <QMouseEvent>
#include <QScrollBar>
#include <QWheelEvent>

CircuitView::CircuitView(QWidget* parent)
    : QGraphicsView(parent)
{
    setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform);
    setDragMode(QGraphicsView::RubberBandDrag);
    setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setAcceptDrops(true);
    setMouseTracking(true);
}

void CircuitView::zoomBy(double factor)
{
    double z = zoomLevel() * factor;
    if (z < 0.15 || z > 6.0) return;
    scale(factor, factor);
    emit zoomChanged(zoomLevel());
}

void CircuitView::zoomToFit()
{
    QRectF r;
    for (QGraphicsItem* it : scene()->items())
        if (!it->parentItem()) r = r.united(it->sceneBoundingRect());
    if (r.isEmpty()) r = QRectF(-200, -150, 400, 300);
    fitInView(r.adjusted(-60, -60, 60, 60), Qt::KeepAspectRatio);
    if (zoomLevel() > 2.0) { resetTransform(); scale(2.0, 2.0); centerOn(r.center()); }
    emit zoomChanged(zoomLevel());
}

void CircuitView::wheelEvent(QWheelEvent* e)
{
    if (e->modifiers() & Qt::ControlModifier) {
        zoomBy(e->angleDelta().y() > 0 ? 1.15 : 1.0 / 1.15);
        e->accept();
        return;
    }
    QGraphicsView::wheelEvent(e);
}

void CircuitView::mousePressEvent(QMouseEvent* e)
{
    if (e->button() == Qt::MiddleButton) {
        m_panning = true;
        m_panStart = e->pos();
        setCursor(Qt::ClosedHandCursor);
        e->accept();
        return;
    }
    QGraphicsView::mousePressEvent(e);
}

void CircuitView::mouseMoveEvent(QMouseEvent* e)
{
    if (m_panning) {
        QPoint d = e->pos() - m_panStart;
        m_panStart = e->pos();
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - d.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() - d.y());
        e->accept();
        return;
    }
    QGraphicsView::mouseMoveEvent(e);
}

void CircuitView::mouseReleaseEvent(QMouseEvent* e)
{
    if (m_panning && e->button() == Qt::MiddleButton) {
        m_panning = false;
        unsetCursor();
        e->accept();
        return;
    }
    QGraphicsView::mouseReleaseEvent(e);
}
