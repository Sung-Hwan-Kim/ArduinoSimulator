#pragma once
#include <QString>
#include <QVector>

namespace Units {

// "4.7k", "10K", "2M2", "100n", "4u7", "220", "0.25" → double. 단위 문자(Ω, F, V, W 등)는 무시
double parse(const QString& text, bool* ok = nullptr);

// 4700, "Ω" → "4.7kΩ"
QString format(double value, const QString& unit, int precision = 3);

// E 계열 표준값
const QVector<double>& e6();
const QVector<double>& e12();
const QVector<double>& e24();

// decades 범위의 E 계열 값 목록 (예: 1Ω ~ 10MΩ)
QVector<double> series(const QVector<double>& base, double minVal, double maxVal);

// value 이상인 가장 가까운 표준값 / 가장 가까운 표준값
double nextStandardUp(double value, const QVector<double>& base);
double nearestStandard(double value, const QVector<double>& base);

// 표준 저항 정격 전력 (W) 중 power * margin 이상인 것
double standardPowerRating(double watts, double margin = 2.0);
QString powerLabel(double watts);

} // namespace Units
