#pragma once
// 자동 배치 / 자동 배선 / 코드 자동 생성
#include <QString>
#include <QStringList>

class CircuitScene;
class PartItem;

namespace AutoWire {

struct Result {
    int wires = 0;
    int resistors = 0;
    QStringList log;
};

// 보드가 없으면 기본 보드를 추가하고 반환
PartItem* ensureBoard(CircuitScene* scene);
// 모든 센서/부품을 보드 위쪽에 정렬 배치하고 다시 배선 (자동 삽입된 저항은 재생성)
Result placeAndWireAll(CircuitScene* scene);
// 아직 연결되지 않은 부품만 현재 위치에서 배선
Result wireUnconnected(CircuitScene* scene);

} // namespace AutoWire

namespace CodeGen {
// 현재 회로의 연결을 분석하여 스케치 생성
QString generate(CircuitScene* scene, QStringList* notes = nullptr);
// "{PIN:LED1.A}" 같은 토큰을 실제 연결된 핀으로 치환
QString resolvePinTokens(CircuitScene* scene, const QString& code);
} // namespace CodeGen
