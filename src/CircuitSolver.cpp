#include "CircuitSolver.h"
#include <cmath>
#include <numeric>

namespace {
const double kOffG = 1e-9;     // 차단 상태 컨덕턴스
const double kLeakG = 1e-10;   // 모든 노드 → GND 누설 (특이행렬 방지)

bool gaussSolve(std::vector<double>& A, std::vector<double>& x, int n)
{
    // A: n x (n+1) 확장 행렬 (행 우선)
    const int w = n + 1;
    for (int col = 0; col < n; ++col) {
        int piv = col;
        double best = std::fabs(A[col * w + col]);
        for (int r = col + 1; r < n; ++r) {
            double v = std::fabs(A[r * w + col]);
            if (v > best) { best = v; piv = r; }
        }
        if (best < 1e-30) return false;
        if (piv != col)
            for (int c = col; c < w; ++c) std::swap(A[col * w + c], A[piv * w + c]);
        double d = A[col * w + col];
        for (int r = col + 1; r < n; ++r) {
            double f = A[r * w + col] / d;
            if (f == 0.0) continue;
            for (int c = col; c < w; ++c) A[r * w + c] -= f * A[col * w + c];
        }
    }
    x.assign(n, 0.0);
    for (int r = n - 1; r >= 0; --r) {
        double s = A[r * w + n];
        for (int c = r + 1; c < n; ++c) s -= A[r * w + c] * x[c];
        x[r] = s / A[r * w + r];
    }
    return true;
}
} // namespace

bool CircuitSolver::solve(int netCount, std::vector<SElem>& elems, std::vector<double>& volts, int maxIter)
{
    volts.assign(netCount > 0 ? netCount : 1, 0.0);
    const int n = netCount - 1;
    if (n <= 0) {
        for (SElem& e : elems) e.current = 0.0;
        return true;
    }
    const int w = n + 1;
    std::vector<double> A, x;

    auto stampG = [&](int a, int b, double g) {
        if (a > 0) A[(a - 1) * w + (a - 1)] += g;
        if (b > 0) A[(b - 1) * w + (b - 1)] += g;
        if (a > 0 && b > 0) { A[(a - 1) * w + (b - 1)] -= g; A[(b - 1) * w + (a - 1)] -= g; }
    };
    auto stampI = [&](int a, int b, double i) {   // a 로 전류 i 주입, b 에서 빼냄
        if (a > 0) A[(a - 1) * w + n] += i;
        if (b > 0) A[(b - 1) * w + n] -= i;
    };
    auto V = [&](int node) { return node > 0 ? volts[node] : 0.0; };

    bool converged = false;
    for (int iter = 0; iter < maxIter; ++iter) {
        A.assign((size_t)n * w, 0.0);
        for (int i = 0; i < n; ++i) A[i * w + i] += kLeakG;

        for (const SElem& e : elems) {
            switch (e.kind) {
            case SElem::Resistor:
                stampG(e.a, e.b, 1.0 / std::max(e.r, 1e-6));
                break;
            case SElem::Source: {
                double g = 1.0 / std::max(e.r, 1e-6);
                stampG(e.a, e.b, g);
                stampI(e.a, e.b, e.v * g);
                break;
            }
            case SElem::Diode: {
                if (e.state == 1) {
                    double g = 1.0 / std::max(e.r, 1e-6);
                    stampG(e.a, e.b, g);
                    stampI(e.a, e.b, g * e.v);
                } else if (e.state == 2) {
                    double g = 1.0 / std::max(e.r, 1e-6);
                    stampG(e.a, e.b, g);
                    stampI(e.a, e.b, -g * e.vz);
                } else {
                    stampG(e.a, e.b, kOffG);
                }
                break;
            }
            case SElem::VSwitch:
            case SElem::ISwitch:
                stampG(e.a, e.b, e.state ? 1.0 / std::max(e.r, 1e-6) : kOffG);
                break;
            }
        }
        if (!gaussSolve(A, x, n)) return false;
        for (int i = 0; i < n; ++i) volts[i + 1] = x[i];

        // 전류 계산
        for (SElem& e : elems) {
            double vab = V(e.a) - V(e.b);
            switch (e.kind) {
            case SElem::Resistor: e.current = vab / std::max(e.r, 1e-6); break;
            case SElem::Source: e.current = (e.v - vab) / std::max(e.r, 1e-6); break;
            case SElem::Diode:
                if (e.state == 1) e.current = (vab - e.v) / std::max(e.r, 1e-6);
                else if (e.state == 2) e.current = (vab + e.vz) / std::max(e.r, 1e-6);
                else e.current = vab * kOffG;
                break;
            case SElem::VSwitch:
            case SElem::ISwitch:
                e.current = vab * (e.state ? 1.0 / std::max(e.r, 1e-6) : kOffG);
                break;
            }
        }

        // 상태 갱신
        bool changed = false;
        for (SElem& e : elems) {
            int ns = e.state;
            if (e.kind == SElem::Diode) {
                double vab = V(e.a) - V(e.b);
                if (e.state == 1) ns = (e.current < -1e-9) ? 0 : 1;
                else if (e.state == 2) ns = (vab > -e.vz + 1e-6) ? 0 : 2;
                else {
                    if (vab > e.v + 1e-6) ns = 1;
                    else if (e.vz > 0 && vab < -e.vz - 1e-6) ns = 2;
                    else ns = 0;
                }
            } else if (e.kind == SElem::VSwitch) {
                double vc = (e.cp >= 0 ? V(e.cp) : 0.0) - (e.cn >= 0 ? V(e.cn) : 0.0);
                bool on = vc > e.v;
                if (e.invert) on = !on;
                ns = on ? 1 : 0;
            } else if (e.kind == SElem::ISwitch) {
                bool on = e.ctrlElem >= 0 && e.ctrlElem < (int)elems.size() && elems[e.ctrlElem].current > e.ith;
                ns = on ? 1 : 0;
            }
            if (ns != e.state) { e.state = ns; changed = true; }
        }
        if (!changed) { converged = true; break; }
    }
    return converged || true;
}

std::vector<bool> CircuitSolver::floatingNodes(int netCount, const std::vector<SElem>& elems)
{
    std::vector<int> parent(std::max(netCount, 1));
    std::iota(parent.begin(), parent.end(), 0);
    auto find = [&](int x) {
        while (parent[x] != x) { parent[x] = parent[parent[x]]; x = parent[x]; }
        return x;
    };
    auto unite = [&](int a, int b) {
        if (a < 0 || b < 0 || a >= netCount || b >= netCount) return;
        a = find(a); b = find(b);
        if (a != b) parent[a] = b;
    };
    for (const SElem& e : elems) {
        bool conducts = false;
        switch (e.kind) {
        case SElem::Resistor: conducts = e.r < 1e8; break;
        case SElem::Source: conducts = e.r < 1e8; break;
        case SElem::Diode: conducts = e.state != 0; break;
        case SElem::VSwitch:
        case SElem::ISwitch: conducts = e.state != 0; break;
        }
        if (conducts) unite(e.a, e.b);
    }
    std::vector<bool> out(std::max(netCount, 1), false);
    int g = find(0);
    for (int i = 1; i < netCount; ++i) out[i] = find(i) != g;
    return out;
}
