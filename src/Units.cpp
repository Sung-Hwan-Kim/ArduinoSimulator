#include "Units.h"
#include <QRegularExpression>
#include <cmath>

namespace Units {

static double prefixMul(QChar c, bool* known)
{
    *known = true;
    switch (c.unicode()) {
    case 'p': return 1e-12;
    case 'n': return 1e-9;
    case 'u': case 0x00B5: case 0x03BC: return 1e-6;   // u, µ, μ
    case 'm': return 1e-3;
    case 'k': case 'K': return 1e3;
    case 'M': return 1e6;
    case 'G': return 1e9;
    case 'R': case 'r': return 1.0;                     // 4R7
    default: *known = false; return 1.0;
    }
}

double parse(const QString& textIn, bool* ok)
{
    QString t = textIn.trimmed();
    // 단위 기호 제거
    static const QStringList units = { QStringLiteral("Ω"), QStringLiteral("ohm"), QStringLiteral("Ohm"),
                                       QStringLiteral("F"), QStringLiteral("V"), QStringLiteral("W"),
                                       QStringLiteral("A"), QStringLiteral("Hz"), QStringLiteral("%"),
                                       QStringLiteral("°C"), QStringLiteral("lx"), QStringLiteral("cm") };
    for (const QString& u : units) {
        if (t.endsWith(u)) { t.chop(u.size()); t = t.trimmed(); break; }
    }
    t.remove(' ');
    if (ok) *ok = false;
    if (t.isEmpty()) return 0.0;

    bool good = false;
    double v = t.toDouble(&good);
    if (good) { if (ok) *ok = true; return v; }

    // 4k7 / 2M2 / 4u7 형식
    static const QRegularExpression mid(QStringLiteral("^([0-9]+)([pnuµμmkKMGRr])([0-9]+)$"));
    auto m = mid.match(t);
    if (m.hasMatch()) {
        bool known = false;
        double mul = prefixMul(m.captured(2).at(0), &known);
        double val = (m.captured(1) + "." + m.captured(3)).toDouble(&good);
        if (good && known) { if (ok) *ok = true; return val * mul; }
    }
    // 4.7k 형식
    QChar last = t.at(t.size() - 1);
    bool known = false;
    double mul = prefixMul(last, &known);
    if (known) {
        double val = t.left(t.size() - 1).toDouble(&good);
        if (good) { if (ok) *ok = true; return val * mul; }
    }
    return 0.0;
}

QString format(double value, const QString& unit, int precision)
{
    if (value == 0.0) return QStringLiteral("0") + unit;
    struct P { double m; const char* s; };
    static const P table[] = { {1e9, "G"}, {1e6, "M"}, {1e3, "k"}, {1.0, ""},
                               {1e-3, "m"}, {1e-6, "µ"}, {1e-9, "n"}, {1e-12, "p"} };
    double a = std::fabs(value);
    for (const P& p : table) {
        if (a >= p.m * 0.9999) {
            double v = value / p.m;
            QString num = QString::number(v, 'g', precision);
            return num + QString::fromUtf8(p.s) + unit;
        }
    }
    return QString::number(value, 'g', precision) + unit;
}

const QVector<double>& e6()
{
    static const QVector<double> v = { 1.0, 1.5, 2.2, 3.3, 4.7, 6.8 };
    return v;
}
const QVector<double>& e12()
{
    static const QVector<double> v = { 1.0, 1.2, 1.5, 1.8, 2.2, 2.7, 3.3, 3.9, 4.7, 5.6, 6.8, 8.2 };
    return v;
}
const QVector<double>& e24()
{
    static const QVector<double> v = { 1.0, 1.1, 1.2, 1.3, 1.5, 1.6, 1.8, 2.0, 2.2, 2.4, 2.7, 3.0,
                                       3.3, 3.6, 3.9, 4.3, 4.7, 5.1, 5.6, 6.2, 6.8, 7.5, 8.2, 9.1 };
    return v;
}

QVector<double> series(const QVector<double>& base, double minVal, double maxVal)
{
    QVector<double> out;
    for (double decade = 1e-12; decade <= maxVal * 10; decade *= 10.0) {
        for (double b : base) {
            double v = b * decade;
            // 부동소수 오차 정리
            double rounded = std::round(v / decade * 100.0) / 100.0 * decade;
            if (rounded >= minVal * 0.999 && rounded <= maxVal * 1.001) out.push_back(rounded);
        }
    }
    return out;
}

double nextStandardUp(double value, const QVector<double>& base)
{
    if (value <= 0) return base.first();
    double decade = std::pow(10.0, std::floor(std::log10(value)));
    for (int k = 0; k < 2; ++k) {
        for (double b : base) {
            double v = b * decade;
            if (v >= value * 0.999) return std::round(v / decade * 100.0) / 100.0 * decade;
        }
        decade *= 10.0;
    }
    return value;
}

double nearestStandard(double value, const QVector<double>& base)
{
    if (value <= 0) return base.first();
    double decade = std::pow(10.0, std::floor(std::log10(value)));
    double best = value, bestErr = 1e300;
    for (int k = -1; k <= 1; ++k) {
        double d = decade * std::pow(10.0, k);
        for (double b : base) {
            double v = b * d;
            double err = std::fabs(std::log(v / value));
            if (err < bestErr) { bestErr = err; best = std::round(v / d * 100.0) / 100.0 * d; }
        }
    }
    return best;
}

double standardPowerRating(double watts, double margin)
{
    static const double ratings[] = { 0.125, 0.25, 0.5, 1.0, 2.0, 3.0, 5.0, 10.0 };
    double need = watts * margin;
    for (double r : ratings) if (r >= need) return r;
    return 10.0;
}

QString powerLabel(double watts)
{
    if (std::fabs(watts - 0.125) < 1e-6) return QStringLiteral("1/8W");
    if (std::fabs(watts - 0.25) < 1e-6) return QStringLiteral("1/4W");
    if (std::fabs(watts - 0.5) < 1e-6) return QStringLiteral("1/2W");
    return QString::number(watts) + "W";
}

} // namespace Units
