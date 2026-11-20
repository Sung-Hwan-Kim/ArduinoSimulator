#include "Netlist.h"
#include "Catalog.h"
#include "PartItem.h"
#include "WireItem.h"
#include <numeric>

Netlist Netlist::build(const QList<PartItem*>& parts, const QList<WireItem*>& wires)
{
    Netlist nl;
    nl.parts = parts;
    QVector<int> base;
    int total = 0;
    for (int i = 0; i < parts.size(); ++i) {
        nl.index[parts[i]] = i;
        base << total;
        total += parts[i]->pinCount();
        if (nl.boardIndex < 0 && parts[i]->def()->isBoard) nl.boardIndex = i;
    }
    QVector<int> parent(total);
    std::iota(parent.begin(), parent.end(), 0);
    auto find = [&](int x) {
        while (parent[x] != x) { parent[x] = parent[parent[x]]; x = parent[x]; }
        return x;
    };
    auto unite = [&](int a, int b) { a = find(a); b = find(b); if (a != b) parent[a] = b; };

    for (int i = 0; i < parts.size(); ++i)
        for (const QVector<int>& grp : parts[i]->def()->internalNets)
            for (int k = 1; k < grp.size(); ++k) unite(base[i] + grp[0], base[i] + grp[k]);
    for (WireItem* w : wires) {
        if (!nl.index.contains(w->partA()) || !nl.index.contains(w->partB())) continue;
        unite(base[nl.index[w->partA()]] + w->pinA(), base[nl.index[w->partB()]] + w->pinB());
    }

    // 넷 번호 매기기: 보드 GND 를 0 으로
    QHash<int, int> rootToNet;
    int gndRoot = -1;
    if (nl.boardIndex >= 0) {
        const PartDef* bd = parts[nl.boardIndex]->def();
        for (int p = 0; p < bd->pins.size(); ++p)
            if (bd->pins[p].bkind == BoardPinKind::Gnd) { gndRoot = find(base[nl.boardIndex] + p); break; }
    }
    int next = 0;
    if (gndRoot >= 0) { rootToNet[gndRoot] = 0; next = 1; nl.hasGround = true; }
    else next = 1;   // 0 번은 비워 둔다 (GND 없음)
    nl.pinNet.resize(parts.size());
    for (int i = 0; i < parts.size(); ++i) {
        nl.pinNet[i].resize(parts[i]->pinCount());
        for (int p = 0; p < parts[i]->pinCount(); ++p) {
            int r = find(base[i] + p);
            auto it = rootToNet.find(r);
            if (it == rootToNet.end()) it = rootToNet.insert(r, next++);
            nl.pinNet[i][p] = it.value();
        }
    }
    nl.netCount = next;
    nl.netPins.resize(next);
    for (int i = 0; i < parts.size(); ++i)
        for (int p = 0; p < parts[i]->pinCount(); ++p) nl.netPins[nl.pinNet[i][p]].push_back({ i, p });
    return nl;
}

int Netlist::net(PartItem* p, int pin) const
{
    auto it = index.find(p);
    if (it == index.end() || pin < 0 || pin >= pinNet[it.value()].size()) return -1;
    return pinNet[it.value()][pin];
}

QVector<int> Netlist::boardPinsOnNet(int n) const
{
    QVector<int> out;
    if (n < 0 || n >= netPins.size()) return out;
    for (const auto& pp : netPins[n]) if (pp.first == boardIndex) out << pp.second;
    return out;
}

int Netlist::nonBoardPinCount(int n) const
{
    if (n < 0 || n >= netPins.size()) return 0;
    int c = 0;
    for (const auto& pp : netPins[n]) if (pp.first != boardIndex) ++c;
    return c;
}

int Netlist::netOfArduinoPin(int ardPin) const
{
    if (boardIndex < 0) return -1;
    const PartDef* bd = parts[boardIndex]->def();
    for (int p = 0; p < bd->pins.size(); ++p)
        if (bd->pins[p].ardPin == ardPin && bd->pins[p].bkind != BoardPinKind::None) return pinNet[boardIndex][p];
    return -1;
}

const PinDef* Netlist::boardPin(int boardPinIdx) const
{
    if (boardIndex < 0) return nullptr;
    const PartDef* bd = parts[boardIndex]->def();
    if (boardPinIdx < 0 || boardPinIdx >= bd->pins.size()) return nullptr;
    return &bd->pins[boardPinIdx];
}
