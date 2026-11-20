#pragma once
// 스크롤되는 목록/패널 안의 콤보박스·스핀박스·슬라이더가
// 마우스 휠을 가로채서 값이 실수로 바뀌는 것을 막는다.
// 포커스가 없을 때의 휠은 스크롤 대상(목록/패널)으로 넘긴다.
#include <QCoreApplication>
#include <QEvent>
#include <QPointer>
#include <QWidget>

class WheelGuard : public QObject
{
public:
    WheelGuard(QWidget* scrollTarget, QObject* parent) : QObject(parent), m_target(scrollTarget) {}

protected:
    bool eventFilter(QObject* o, QEvent* e) override
    {
        if (e->type() == QEvent::Wheel) {
            auto* w = qobject_cast<QWidget*>(o);
            if (w && !w->hasFocus()) {
                if (m_target) QCoreApplication::sendEvent(m_target, e);
                return true;
            }
        }
        return false;
    }

private:
    QPointer<QWidget> m_target;
};

inline void guardWheel(QWidget* w, QWidget* scrollTarget)
{
    w->setFocusPolicy(Qt::StrongFocus);
    w->installEventFilter(new WheelGuard(scrollTarget, w));
}
