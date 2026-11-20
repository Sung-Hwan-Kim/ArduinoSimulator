#pragma once
// 수정 절점 해석(Nodal Analysis) 기반 DC 회로 해석기
// - 전압원은 노턴 등가(내부저항 + 전류원)로 표현 → 행렬 크기 = (노드 수 - 1)
// - 다이오드/LED 는 구간 선형(PWL) 모델, 스위치는 상태 반복으로 수렴
#include <vector>

struct SElem {
    enum Kind { Resistor, Source, Diode, VSwitch, ISwitch };
    Kind kind = Resistor;
    int a = 0, b = 0;          // 노드 (0 = GND)
    double r = 1.0;            // 저항 / 내부저항 / 도통 저항
    double v = 0.0;            // 전원 전압(a 기준 b) / 다이오드 Von / VSwitch 문턱
    double vz = 0.0;           // 제너 항복 전압 (0 = 없음)
    int cp = -1, cn = -1;      // VSwitch 제어 노드: on = (V(cp)-V(cn) > v) ^ invert
    bool invert = false;
    int ctrlElem = -1;         // ISwitch: 제어 소자 인덱스 (그 전류 > ith 이면 on)
    double ith = 0.0;
    int state = 0;             // 다이오드: 0 off, 1 on, 2 항복 / 스위치: 0/1
    double current = 0.0;      // 결과: a→b 전류 (Source 는 a 로 공급하는 전류)
    int owner = -1;            // 소유 부품 인덱스
    int tag = 0;               // 부품 내부 식별자
};

class CircuitSolver
{
public:
    // netCount: 노드 수 (0 포함). 성공 시 volts[0..netCount-1] 채움
    static bool solve(int netCount, std::vector<SElem>& elems, std::vector<double>& volts, int maxIter = 60);
    // 어떤 전원/GND 와도 도통 경로가 없는 노드 (플로팅) 표시
    static std::vector<bool> floatingNodes(int netCount, const std::vector<SElem>& elems);
};
