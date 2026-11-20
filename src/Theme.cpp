#include "Theme.h"
#include <QApplication>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QProxyStyle>
#include <QStyleOption>
#include <QStyle>
#include <QStyleFactory>

namespace {

// Fusion 을 감싸서 체크박스를 다크/라이트 모두 잘 보이게 직접 그림
// (Fusion 기본 체크박스는 다크 팔레트에서 테두리가 배경에 묻힘)
class AppStyle : public QProxyStyle
{
public:
    AppStyle() : QProxyStyle(QStyleFactory::create(QStringLiteral("Fusion"))) {}

    void drawPrimitive(PrimitiveElement pe, const QStyleOption* opt, QPainter* p, const QWidget* w) const override
    {
        if (pe != PE_IndicatorCheckBox && pe != PE_IndicatorItemViewItemCheck) {
            QProxyStyle::drawPrimitive(pe, opt, p, w);
            return;
        }
        const bool dark = Theme::dark();
        const bool on = opt->state & State_On;
        const bool partial = opt->state & State_NoChange;
        const bool enabled = opt->state & State_Enabled;
        const bool hover = opt->state & State_MouseOver;
        int s = qMin(opt->rect.width(), opt->rect.height()) - 1;
        s = qMin(s, 16);
        QRectF r(opt->rect.x() + (opt->rect.width() - s) / 2.0 + 0.5, opt->rect.y() + (opt->rect.height() - s) / 2.0 + 0.5, s - 1, s - 1);
        const QColor accent(0, 120, 215);
        QColor border = dark ? QColor(150, 155, 165) : QColor(118, 120, 128);
        if (hover && enabled) border = accent.lighter(dark ? 130 : 100);
        QColor fill = opt->palette.base().color();
        if (on || partial) { fill = accent; border = accent; }
        if (!enabled) { border.setAlpha(110); if (on || partial) fill.setAlpha(110); }
        p->save();
        p->setRenderHint(QPainter::Antialiasing, true);
        p->setPen(QPen(border, 1.2));
        p->setBrush(fill);
        p->drawRoundedRect(r, 2.5, 2.5);
        if (on) {
            QPainterPath check;
            check.moveTo(r.left() + r.width() * 0.22, r.top() + r.height() * 0.53);
            check.lineTo(r.left() + r.width() * 0.43, r.top() + r.height() * 0.73);
            check.lineTo(r.left() + r.width() * 0.79, r.top() + r.height() * 0.30);
            p->setPen(QPen(Qt::white, qMax(1.6, s / 7.0), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p->setBrush(Qt::NoBrush);
            p->drawPath(check);
        } else if (partial) {
            p->setPen(QPen(Qt::white, qMax(1.6, s / 7.0), Qt::SolidLine, Qt::RoundCap));
            p->drawLine(QPointF(r.left() + r.width() * 0.25, r.center().y()), QPointF(r.right() - r.width() * 0.25, r.center().y()));
        }
        p->restore();
    }
};

} // namespace

namespace Theme {

static bool g_dark = true;

bool dark() { return g_dark; }

void apply(const QString& mode)
{
    g_dark = mode != "light";
    if (!qobject_cast<QApplication*>(QCoreApplication::instance())) return;
    // 실행 중 전환 시 모든 위젯이 다시 polish 되도록 스타일을 새로 설정한 뒤 팔레트 적용
    QApplication::setStyle(new AppStyle);

    QPalette pal;
    auto set = [&pal](QPalette::ColorRole r, const QColor& c) { pal.setColor(r, c); };
    if (g_dark) {
        const QColor win(45, 46, 50), text(222, 223, 226), dis(115, 116, 122);
        set(QPalette::Window, win);
        set(QPalette::WindowText, text);
        set(QPalette::Base, QColor(30, 31, 34));
        set(QPalette::AlternateBase, QColor(38, 39, 43));
        set(QPalette::ToolTipBase, QColor(60, 62, 68));
        set(QPalette::ToolTipText, text);
        set(QPalette::PlaceholderText, QColor(125, 127, 134));
        set(QPalette::Text, text);
        set(QPalette::Button, QColor(56, 58, 63));
        set(QPalette::ButtonText, text);
        set(QPalette::BrightText, QColor(255, 90, 90));
        set(QPalette::Light, QColor(85, 87, 93));
        set(QPalette::Midlight, QColor(68, 70, 76));
        set(QPalette::Mid, QColor(52, 54, 59));
        set(QPalette::Dark, QColor(28, 29, 32));
        set(QPalette::Shadow, QColor(12, 12, 14));
        set(QPalette::Highlight, QColor(0, 120, 215));
        set(QPalette::HighlightedText, Qt::white);
        set(QPalette::Link, QColor(95, 165, 255));
        set(QPalette::LinkVisited, QColor(180, 140, 255));
        pal.setColor(QPalette::Disabled, QPalette::WindowText, dis);
        pal.setColor(QPalette::Disabled, QPalette::Text, dis);
        pal.setColor(QPalette::Disabled, QPalette::ButtonText, dis);
        pal.setColor(QPalette::Disabled, QPalette::Base, QColor(38, 39, 43));
    } else {
        const QColor win(240, 241, 244), text(25, 25, 30), dis(150, 150, 155);
        set(QPalette::Window, win);
        set(QPalette::WindowText, text);
        set(QPalette::Base, Qt::white);
        set(QPalette::AlternateBase, QColor(246, 247, 250));
        set(QPalette::ToolTipBase, QColor(255, 255, 225));
        set(QPalette::ToolTipText, text);
        set(QPalette::PlaceholderText, QColor(130, 130, 135));
        set(QPalette::Text, text);
        set(QPalette::Button, win);
        set(QPalette::ButtonText, text);
        set(QPalette::BrightText, Qt::red);
        set(QPalette::Light, Qt::white);
        set(QPalette::Midlight, QColor(225, 226, 230));
        set(QPalette::Mid, QColor(190, 191, 196));
        set(QPalette::Dark, QColor(160, 161, 166));
        set(QPalette::Shadow, QColor(100, 100, 105));
        set(QPalette::Highlight, QColor(0, 120, 215));
        set(QPalette::HighlightedText, Qt::white);
        set(QPalette::Link, QColor(0, 102, 204));
        pal.setColor(QPalette::Disabled, QPalette::WindowText, dis);
        pal.setColor(QPalette::Disabled, QPalette::Text, dis);
        pal.setColor(QPalette::Disabled, QPalette::ButtonText, dis);
    }
    QApplication::setPalette(pal);
}

QColor canvasBg() { return g_dark ? QColor(36, 38, 42) : QColor(248, 249, 251); }
QColor gridDot() { return g_dark ? QColor(72, 75, 82) : QColor(200, 204, 212); }
QColor gridMajor() { return g_dark ? QColor(48, 51, 57) : QColor(225, 228, 235); }
QColor partLabel() { return g_dark ? QColor(205, 208, 214) : QColor(60, 60, 60); }
QColor accentText() { return g_dark ? QColor(110, 180, 255) : QColor(0, 80, 190); }
QColor wireHalo() { return g_dark ? QColor(150, 155, 165, 150) : QColor(0, 0, 0, 0); }

QColor editorCurrentLine() { return g_dark ? QColor(42, 44, 50) : QColor(245, 248, 255); }
QColor editorGutterBg() { return g_dark ? QColor(34, 35, 39) : QColor(242, 243, 246); }
QColor editorGutterText() { return g_dark ? QColor(110, 115, 125) : QColor(150, 150, 160); }
QColor editorErrorBg() { return g_dark ? QColor(95, 32, 32) : QColor(255, 215, 215); }
QColor synType() { return g_dark ? QColor(86, 156, 214) : QColor(0, 120, 160); }
QColor synKeyword() { return g_dark ? QColor(197, 134, 192) : QColor(115, 50, 170); }
QColor synConst() { return g_dark ? QColor(78, 201, 176) : QColor(0, 110, 80); }
QColor synFunc() { return g_dark ? QColor(220, 220, 170) : QColor(210, 90, 0); }
QColor synNumber() { return g_dark ? QColor(181, 206, 168) : QColor(0, 100, 200); }
QColor synPreproc() { return g_dark ? QColor(155, 155, 220) : QColor(120, 120, 0); }
QColor synString() { return g_dark ? QColor(206, 145, 120) : QColor(160, 30, 30); }
QColor synComment() { return g_dark ? QColor(106, 153, 85) : QColor(110, 140, 110); }

QColor hintText() { return g_dark ? QColor(160, 163, 170) : QColor(90, 90, 95); }

} // namespace Theme
