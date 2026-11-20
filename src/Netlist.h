#pragma once
// 부품/전선으로부터 전기적 연결(넷) 계산
#include <QHash>
#include <QList>
#include <QPair>
#include <QVector>

class PartItem;
class WireItem;
struct PinDef;

struct Netlist {
    QList<PartItem*> parts;
    QHash<PartItem*, int> index;
    QVector<QVector<int>> pinNet;                    // [부품][핀] → 넷 번호 (보드 GND 넷 = 0)
    QVector<QVector<QPair<int, int>>> netPins;       // 넷 → (부품, 핀) 목록
    int netCount = 0;
    int boardIndex = -1;
    bool hasGround = false;

    static Netlist build(const QList<PartItem*>& parts, const QList<WireItem*>& wires);

    int net(PartItem* p, int pin) const;
    int net(int partIdx, int pin) const { return pinNet[partIdx][pin]; }
    // 넷에 연결된 보드 핀 (없으면 -1 = 보드 핀 인덱스)
    QVector<int> boardPinsOnNet(int net) const;
    // 넷에 연결된 다른 부품 핀 수 (보드 제외)
    int nonBoardPinCount(int net) const;
    // 보드의 특정 아두이노 핀 번호가 속한 넷
    int netOfArduinoPin(int ardPin) const;
    const PinDef* boardPin(int boardPinIdx) const;
};
