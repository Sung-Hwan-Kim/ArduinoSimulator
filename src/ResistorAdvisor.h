#pragma once
// 회로 연결 상태를 분석하여 저항값을 추천/자동 설정
#include <QString>
#include <QStringList>

class CircuitScene;
class PartItem;

struct ResistorAdvice {
    bool ok = false;
    double ohms = 0.0;
    double watts = 0.25;       // 추천 정격 전력
    QString purpose;           // 용도 (LED 전류 제한, 분압, 풀업 ...)
    QString reason;            // 계산 근거
};

namespace ResistorAdvisor {

// LED 직렬 저항 계산: (Vs - Vf) / I
ResistorAdvice ledSeries(double supplyV, double vf, double ratedmA, const QString& what = QString());
// LDR 분압 저항: √(R@10lux · R@1000lux)
ResistorAdvice ldrDivider(double r10, double gamma, double rdark);
// NTC 분압 저항: R25
ResistorAdvice ntcDivider(double r25);
// 배치된 저항 하나에 대해, 연결된 부품을 보고 추천
ResistorAdvice adviseFor(CircuitScene* scene, PartItem* resistor);
// LED/RGB/7세그 부품에 필요한 직렬 저항 안내 (속성 패널 표시용)
QString ledHint(CircuitScene* scene, PartItem* part);
// 부품 종류별 일반 안내 (센서 등)
QString partHint(PartItem* part);
// 직렬 저항이 없는 LED 핀에 저항을 끼워 넣음. 넣은 저항 수 반환
int insertMissingLedResistors(CircuitScene* scene, PartItem* part, QStringList* report);
// 회로 전체: 기존 저항값 추천/적용 + 빠진 LED 저항 삽입
int applyAll(CircuitScene* scene, QStringList* report);

double roundToSeries(double ohms, bool up);

} // namespace ResistorAdvisor
