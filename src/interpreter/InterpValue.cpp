// InterpValue.cpp - 값 변환, 산술 연산, 출력 서식
#include "InterpCore.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace ard {
namespace interp {

void rtFail(const std::string& msg, int line) { throw RtError{msg, line}; }

Value* Expr::ref(Ctx&) { rtFail("대입할 수 없는 식입니다 (변수가 필요합니다)", line); }

int btSize(BT t) {
    switch (t) {
    case BT::Bool: case BT::I8: case BT::U8: case BT::Obj: return 1;
    case BT::I16: case BT::U16: case BT::Ptr: return 2;
    case BT::I32: case BT::U32: case BT::F32: case BT::F64: return 4;
    case BT::I64: case BT::U64: return 8;
    case BT::Str: return 6;
    default: return 0;
    }
}

BT promoteBT(BT t) {
    switch (t) {
    case BT::Bool: case BT::I8: case BT::U8: case BT::I16: return BT::I16;
    default: return t;
    }
}

static int rankOf(BT t) {
    switch (t) {
    case BT::I16: case BT::U16: return 1;
    case BT::I32: case BT::U32: return 2;
    default: return 3;
    }
}

static BT commonInt(BT a, BT b) {
    a = promoteBT(a); b = promoteBT(b);
    if (a == b) return a;
    int ra = rankOf(a), rb = rankOf(b);
    if (ra != rb) return ra > rb ? a : b;
    return isUnsignedBT(a) ? a : b;
}

int64_t wrapInt(int64_t v, BT t) {
    switch (t) {
    case BT::Bool: return v != 0 ? 1 : 0;
    case BT::I8: return (int8_t)v;
    case BT::U8: return (uint8_t)v;
    case BT::I16: return (int16_t)v;
    case BT::U16: return (uint16_t)v;
    case BT::I32: return (int32_t)v;
    case BT::U32: return (uint32_t)v;
    default: return v;
    }
}

int64_t dblToInt(double d) {
    if (std::isnan(d)) return 0;
    if (d >= 9.2e18) return INT64_MAX;
    if (d <= -9.2e18) return INT64_MIN;
    return (int64_t)d;
}

static inline double numD(const Value& v) {
    if (v.k == VK::Float) return v.f;
    return v.t == BT::U64 ? (double)(uint64_t)v.i : (double)v.i;
}

bool isNum(const Value& v) { return v.k == VK::Int || v.k == VK::Float; }

int64_t toI(const Value& v, int line) {
    switch (v.k) {
    case VK::Int: return v.i;
    case VK::Float: return dblToInt(v.f);
    case VK::Void: return 0;
    case VK::Str: rtFail("문자열을 숫자로 사용할 수 없습니다 (toInt() 등을 사용하세요)", line);
    default: rtFail("숫자가 필요한 곳에 다른 종류의 값이 사용되었습니다", line);
    }
}

double toD(const Value& v, int line) {
    if (v.k == VK::Int || v.k == VK::Float) return numD(v);
    return (double)toI(v, line);
}

bool truthy(const Value& v) {
    switch (v.k) {
    case VK::Int: return v.i != 0;
    case VK::Float: return v.f != 0.0;
    case VK::Void: return false;
    case VK::Ptr: return v.p != nullptr;
    default: return true;
    }
}

static bool isCharArr(const Value& v) {
    if (v.k != VK::Arr) return false;
    ArrObj* a = arrOf(v);
    return (a->elem == BT::I8 || a->elem == BT::U8) && (size_t)v.lvl + 1 >= a->dims.size();
}

bool isStrLike(const Value& v) { return v.k == VK::Str || isCharArr(v); }

static size_t subEnd(const Value& v) {
    ArrObj* a = arrOf(v);
    if (v.lvl > 0 && v.lvl <= a->strides.size()) return std::min(a->data.size(), (size_t)v.i + a->strides[v.lvl - 1]);
    return a->data.size();
}

std::string cstrOf(const Value& v) {
    if (v.k == VK::Str) return strOf(v);
    if (v.k == VK::Arr) {
        ArrObj* a = arrOf(v);
        std::string s;
        size_t end = subEnd(v);
        for (size_t k = (size_t)v.i; k < end; ++k) {
            int64_t ch = a->data[k].i;
            if (ch == 0) break;
            s.push_back((char)ch);
        }
        return s;
    }
    return std::string();
}

static std::string fmtFixed(double v, int digits) {
    if (std::isnan(v)) return "nan";
    if (std::isinf(v)) return v < 0 ? "-inf" : "inf";
    if (digits < 0) digits = 0;
    if (digits > 20) digits = 20;
    char buf[400];
    std::snprintf(buf, sizeof buf, "%.*f", digits, v);
    return buf;
}

std::string toStr(const Value& v) {
    switch (v.k) {
    case VK::Str: return strOf(v);
    case VK::Arr: return cstrOf(v);
    case VK::Int:
        if (v.t == BT::I8) return std::string(1, (char)v.i);
        return fmtInteger(v.i, v.t, 10);
    case VK::Float: return fmtFixed(v.f, 2);
    default: return std::string();
    }
}

std::string& mutStr(Value& v) {
    if (v.k != VK::Str) {
        std::string s = toStr(v);
        v.k = VK::Str;
        v.h = std::make_shared<StrObj>(std::move(s));
    } else if (v.h.use_count() > 1) {
        v.h = std::make_shared<StrObj>(strOf(v));
    }
    return static_cast<StrObj*>(v.h.get())->s;
}

Value deepCopy(const Value& v) {
    if (v.k == VK::Struct) {
        const StructObj* src = structOf(v);
        auto so = std::make_shared<StructObj>();
        so->sid = src->sid;
        so->f.reserve(src->f.size());
        for (const Value& f : src->f)
            so->f.push_back((f.k == VK::Struct || (f.k == VK::Arr && f.t != BT::Ptr)) ? deepCopy(f) : f);
        Value r = v; r.h = so; return r;
    }
    if (v.k == VK::Arr && v.t != BT::Ptr) {
        auto a = std::make_shared<ArrObj>(*arrOf(v));
        for (Value& e : a->data) if (e.k == VK::Struct) e = deepCopy(e);
        Value r = v; r.h = a; return r;
    }
    return v;
}

void assignTo(Value& d, const Value& s, int line) {
    BT t = d.t;
    if (isIntBT(t)) {
        int64_t v;
        if (s.k == VK::Int) v = s.i;
        else if (s.k == VK::Float) v = dblToInt(s.f);
        else if (t == BT::Bool) v = truthy(s) ? 1 : 0;
        else v = toI(s, line);
        d.k = VK::Int;
        d.i = wrapInt(v, t);
        return;
    }
    if (isFloatBT(t)) {
        double x = (s.k == VK::Int || s.k == VK::Float) ? numD(s) : toD(s, line);
        d.k = VK::Float;
        d.f = (double)(float)x;
        return;
    }
    switch (t) {
    case BT::Str:
        if (s.k == VK::Str) d.h = s.h;
        else if (s.k == VK::Void) rtFail("void 값을 String 에 대입할 수 없습니다", line);
        else d.h = std::make_shared<StrObj>(toStr(s));
        d.k = VK::Str;
        return;
    case BT::Ptr:
        if (s.k == VK::Int || s.k == VK::Void) {
            if (s.k == VK::Int && s.i != 0) rtFail("정수를 포인터에 대입할 수 없습니다", line);
            d.k = VK::Ptr; d.p = nullptr; d.h.reset();
            return;
        }
        if (s.k == VK::Float) rtFail("실수를 포인터에 대입할 수 없습니다", line);
        d.k = s.k; d.lvl = s.lvl; d.i = s.i; d.h = s.h;
        return;
    case BT::Struct:
        if (s.k != VK::Struct) rtFail("구조체에 다른 종류의 값을 대입할 수 없습니다", line);
        d.k = VK::Struct;
        d.h = deepCopy(s).h;
        return;
    case BT::Obj:
        d.k = s.k; d.i = s.i; d.h = s.h;
        return;
    default:
        if (d.k == VK::Arr) rtFail("배열 전체에는 대입할 수 없습니다", line);
        d = (s.k == VK::Struct) ? deepCopy(s) : s;
        return;
    }
}

static Value ptrAdd(const Value& a, int64_t n, int line) {
    if (a.k == VK::Str) {
        const std::string& s = strOf(a);
        if (n < 0 || n > (int64_t)s.size())
            rtFail("문자열 상수 + 숫자는 포인터 연산입니다 (문자열로 붙이려면 String(\"...\") + 값 형태로 쓰세요)", line);
        return Value::S(s.substr((size_t)n), BT::Ptr);
    }
    ArrObj* ar = arrOf(a);
    size_t st = a.lvl < ar->strides.size() ? ar->strides[a.lvl] : 1;
    Value r = a;
    r.i = a.i + n * (int64_t)st;
    r.t = BT::Ptr;
    if (r.i < 0 || r.i > (int64_t)ar->data.size()) rtFail("포인터가 배열 범위를 벗어났습니다", line);
    return r;
}

static bool cmpResult(int op, int c) {
    switch (op) {
    case OP_EQ: return c == 0; case OP_NE: return c != 0;
    case OP_LT: return c < 0;  case OP_GT: return c > 0;
    case OP_LE: return c <= 0; default: return c >= 0;
    }
}

Value binop(int op, const Value& a, const Value& b, int line) {
    if (a.k == VK::Int && b.k == VK::Int) {
        bool shift = (op == OP_SHL || op == OP_SHR);
        BT rt = shift ? promoteBT(a.t) : commonInt(a.t, b.t);
        int64_t x = wrapInt(a.i, rt), y = shift ? b.i : wrapInt(b.i, rt);
        bool u64 = rt == BT::U64;
        int64_t r = 0;
        switch (op) {
        case OP_ADD: r = (int64_t)((uint64_t)x + (uint64_t)y); break;
        case OP_SUB: r = (int64_t)((uint64_t)x - (uint64_t)y); break;
        case OP_MUL: r = (int64_t)((uint64_t)x * (uint64_t)y); break;
        case OP_DIV:
            if (y == 0) rtFail("0으로 나눌 수 없습니다", line);
            if (u64) r = (int64_t)((uint64_t)x / (uint64_t)y);
            else if (y == -1) r = (int64_t)(0 - (uint64_t)x);
            else r = x / y;
            break;
        case OP_MOD:
            if (y == 0) rtFail("0으로 나눌 수 없습니다 (% 연산)", line);
            if (u64) r = (int64_t)((uint64_t)x % (uint64_t)y);
            else if (y == -1) r = 0;
            else r = x % y;
            break;
        case OP_AND: r = x & y; break;
        case OP_OR:  r = x | y; break;
        case OP_XOR: r = x ^ y; break;
        case OP_SHL: r = (y < 0 || y >= 64) ? 0 : (int64_t)((uint64_t)x << y); break;
        case OP_SHR:
            if (y < 0 || y >= 64) r = (x < 0 && !u64) ? -1 : 0;
            else r = u64 ? (int64_t)((uint64_t)x >> y) : (x >> y);
            break;
        case OP_EQ: return Value::B(x == y);
        case OP_NE: return Value::B(x != y);
        case OP_LT: return Value::B(u64 ? (uint64_t)x < (uint64_t)y : x < y);
        case OP_GT: return Value::B(u64 ? (uint64_t)x > (uint64_t)y : x > y);
        case OP_LE: return Value::B(u64 ? (uint64_t)x <= (uint64_t)y : x <= y);
        case OP_GE: return Value::B(u64 ? (uint64_t)x >= (uint64_t)y : x >= y);
        default: rtFail("알 수 없는 연산자", line);
        }
        return Value::I(wrapInt(r, rt), rt);
    }
    if (isNum(a) && isNum(b)) {
        double x = numD(a), y = numD(b), r = 0;
        switch (op) {
        case OP_ADD: r = x + y; break;
        case OP_SUB: r = x - y; break;
        case OP_MUL: r = x * y; break;
        case OP_DIV: r = x / y; break;
        case OP_EQ: return Value::B(x == y);
        case OP_NE: return Value::B(x != y);
        case OP_LT: return Value::B(x < y);
        case OP_GT: return Value::B(x > y);
        case OP_LE: return Value::B(x <= y);
        case OP_GE: return Value::B(x >= y);
        case OP_MOD: rtFail("실수에는 % 연산자를 사용할 수 없습니다 (fmod 를 사용하세요)", line);
        default: rtFail("실수에는 비트 연산자를 사용할 수 없습니다", line);
        }
        return Value::F((double)(float)r, BT::F32);
    }
    bool aStr = a.k == VK::Str && a.t == BT::Str;
    bool bStr = b.k == VK::Str && b.t == BT::Str;
    bool aSL = isStrLike(a), bSL = isStrLike(b);
    if (op == OP_ADD) {
        if (aStr || bStr || (aSL && bSL)) {
            if (a.k == VK::Void || b.k == VK::Void) rtFail("void 값은 연결할 수 없습니다", line);
            return Value::S(toStr(a) + toStr(b));
        }
        if ((a.k == VK::Str || a.k == VK::Arr) && b.k == VK::Int) return ptrAdd(a, b.i, line);
        if ((b.k == VK::Str || b.k == VK::Arr) && a.k == VK::Int) return ptrAdd(b, a.i, line);
        if ((aSL && b.k == VK::Float) || (bSL && a.k == VK::Float))
            rtFail("문자열 상수와 실수는 + 로 연결할 수 없습니다 (String(\"...\") + 값 형태로 쓰세요)", line);
    }
    if (op == OP_SUB) {
        if ((a.k == VK::Str || a.k == VK::Arr) && b.k == VK::Int) return ptrAdd(a, -b.i, line);
        if (a.k == VK::Arr && b.k == VK::Arr && a.h == b.h) return Value::I(a.i - b.i, BT::I16);
    }
    if (op >= OP_EQ) {
        if (aSL && bSL) return Value::B(cmpResult(op, toStr(a).compare(toStr(b))));
        if (op == OP_EQ || op == OP_NE) {
            bool eq;
            if (a.k == VK::Ptr && b.k == VK::Ptr) eq = a.p == b.p;
            else if (a.k == VK::Arr && b.k == VK::Arr) eq = a.h == b.h && a.i == b.i;
            else if (a.k == VK::Obj && b.k == VK::Obj) eq = a.h == b.h;
            else if (a.k == VK::Func && b.k == VK::Func) eq = a.fn == b.fn;
            else if ((a.k == VK::Int && a.i == 0) || (b.k == VK::Int && b.i == 0)) eq = !truthy(a) && !truthy(b);
            else rtFail("서로 비교할 수 없는 값입니다", line);
            return Value::B(op == OP_EQ ? eq : !eq);
        }
    }
    if (a.k == VK::Void || b.k == VK::Void) rtFail("void 값은 연산에 사용할 수 없습니다", line);
    rtFail("이 종류의 값에는 해당 연산을 할 수 없습니다", line);
}

Value unop(int op, const Value& a, int line) {
    if (op == U_NOT) return Value::B(!truthy(a));
    if (a.k == VK::Int) {
        BT rt = promoteBT(a.t);
        switch (op) {
        case U_NEG: return Value::I(wrapInt((int64_t)(0 - (uint64_t)a.i), rt), rt);
        case U_PLUS: return Value::I(wrapInt(a.i, rt), rt);
        default: return Value::I(wrapInt(~a.i, rt), rt);
        }
    }
    if (a.k == VK::Float) {
        if (op == U_NEG) return Value::F(-a.f, a.t);
        if (op == U_PLUS) return a;
        rtFail("실수에는 ~ 연산자를 사용할 수 없습니다", line);
    }
    rtFail("숫자가 아닌 값에 단항 연산자를 사용할 수 없습니다", line);
}

std::string fmtInteger(int64_t v, BT t, int base) {
    if (base < 2 || base > 36) base = 10;
    bool neg = false;
    uint64_t u;
    if (base == 10) {
        if (v < 0 && !isUnsignedBT(t)) { neg = true; u = (uint64_t)(-(v + 1)) + 1; }
        else u = (uint64_t)v;
    } else {
        if (v < 0) u = (t == BT::I64 || t == BT::U64) ? (uint64_t)v : (uint64_t)(uint32_t)v;
        else u = (uint64_t)v;
    }
    char buf[80];
    int pos = 79;
    buf[pos] = 0;
    do {
        int dg = (int)(u % (uint64_t)base);
        buf[--pos] = (char)(dg < 10 ? '0' + dg : 'A' + dg - 10);
        u /= (uint64_t)base;
    } while (u);
    if (neg) buf[--pos] = '-';
    return std::string(buf + pos);
}

// Arduino Print::printFloat 와 동일한 알고리즘 (32bit float 연산)
std::string fmtFloat(double number, int digits) {
    if (std::isnan(number)) return "nan";
    if (std::isinf(number)) return "inf";
    if (number > 4294967040.0 || number < -4294967040.0) return "ovf";
    if (digits < 0) digits = 0;
    if (digits > 30) digits = 30;
    std::string out;
    float n = (float)number;
    if (n < 0.0f) { out += '-'; n = -n; }
    float rounding = 0.5f;
    for (int k = 0; k < digits; ++k) rounding /= 10.0f;
    n += rounding;
    uint32_t ip = (uint32_t)n;
    float rem = n - (float)ip;
    out += std::to_string(ip);
    if (digits > 0) out += '.';
    while (digits-- > 0) {
        rem *= 10.0f;
        unsigned int tp = (unsigned int)rem;
        out += std::to_string(tp);
        rem -= (float)tp;
    }
    return out;
}

std::string printText(const Value& v, int line) {
    switch (v.k) {
    case VK::Int:
        if (v.t == BT::I8) return std::string(1, (char)v.i);
        return fmtInteger(v.i, v.t, 10);
    case VK::Float: return fmtFloat(v.f, 2);
    case VK::Str: return strOf(v);
    case VK::Arr:
        if (isCharArr(v)) return cstrOf(v);
        rtFail("배열은 그대로 출력할 수 없습니다 (요소를 하나씩 출력하세요)", line);
    case VK::Void: rtFail("void 값은 출력할 수 없습니다", line);
    case VK::Ptr: rtFail("포인터는 출력할 수 없습니다", line);
    default: rtFail("출력할 수 없는 종류의 값입니다", line);
    }
}

std::string printText2(const Value& v, int arg, int line) {
    if (v.k == VK::Int) {
        if (arg == 0) return std::string(1, (char)v.i);
        return fmtInteger(v.i, v.t, arg);
    }
    if (v.k == VK::Float) return fmtFloat(v.f, arg);
    return printText(v, line);
}

std::string formatPrintf(const std::string& fmt, const std::vector<Value>& args, size_t ai, int line) {
    std::string out;
    size_t n = fmt.size();
    auto nextArg = [&]() -> const Value& {
        if (ai >= args.size()) rtFail("printf 형식 문자열에 비해 인자가 부족합니다", line);
        return args[ai++];
    };
    auto readNum = [&](size_t& k, std::string& spec) {
        if (k < n && fmt[k] == '*') {
            int64_t w = toI(nextArg(), line);
            w = std::max<int64_t>(-200, std::min<int64_t>(200, w));
            spec += std::to_string(w);
            ++k;
            return;
        }
        int val = 0;
        while (k < n && fmt[k] >= '0' && fmt[k] <= '9') { val = std::min(val * 10 + (fmt[k] - '0'), 200); ++k; }
        spec += std::to_string(val);
    };
    std::vector<char> buf;
    for (size_t k = 0; k < n; ++k) {
        char ch = fmt[k];
        if (ch != '%') { out += ch; continue; }
        if (k + 1 < n && fmt[k + 1] == '%') { out += '%'; ++k; continue; }
        std::string spec = "%";
        ++k;
        while (k < n && std::strchr("-+ #0", fmt[k]) && fmt[k] != 0) spec += fmt[k++];
        if (k < n && (fmt[k] == '*' || (fmt[k] >= '0' && fmt[k] <= '9'))) readNum(k, spec);
        if (k < n && fmt[k] == '.') { spec += '.'; ++k; readNum(k, spec); }
        int lng = 0;
        while (k < n && std::strchr("hlLqjzt", fmt[k]) && fmt[k] != 0) { if (fmt[k] == 'l') ++lng; ++k; }
        if (k >= n) break;
        char conv = fmt[k];
        std::string sarg;
        long long iv = 0;
        unsigned long long uv = 0;
        double dv = 0;
        int kind = 0;   // 1=int 2=uint 3=char 4=str 5=double
        switch (conv) {
        case 'd': case 'i': { const Value& a = nextArg(); iv = toI(a, line); kind = 1; break; }
        case 'u': case 'x': case 'X': case 'o': {
            const Value& a = nextArg();
            int64_t v = toI(a, line);
            if (v < 0) {
                bool is32 = lng == 1 || (a.k == VK::Int && (a.t == BT::I32 || a.t == BT::U32));
                uv = lng >= 2 ? (uint64_t)v : is32 ? (uint64_t)(uint32_t)v : (uint64_t)(uint16_t)v;
            } else uv = (uint64_t)v;
            kind = 2; break;
        }
        case 'c': { iv = (unsigned char)toI(nextArg(), line); kind = 3; break; }
        case 's': { sarg = toStr(nextArg()); kind = 4; break; }
        case 'f': case 'F': case 'e': case 'E': case 'g': case 'G': { dv = toD(nextArg(), line); kind = 5; break; }
        case 'p': { nextArg(); uv = 0; conv = 'X'; kind = 2; break; }
        default: out += spec; out += conv; continue;
        }
        buf.assign(sarg.size() + 512, 0);
        int m = 0;
        switch (kind) {
        case 1: m = std::snprintf(buf.data(), buf.size(), (spec + "lld").c_str(), iv); break;
        case 2: m = std::snprintf(buf.data(), buf.size(), (spec + "ll" + conv).c_str(), uv); break;
        case 3: m = std::snprintf(buf.data(), buf.size(), (spec + "c").c_str(), (int)iv); break;
        case 4: m = std::snprintf(buf.data(), buf.size(), (spec + "s").c_str(), sarg.c_str()); break;
        default: m = std::snprintf(buf.data(), buf.size(), (spec + conv).c_str(), dv); break;
        }
        if (m > 0) out.append(buf.data(), std::min((size_t)m, buf.size() - 1));
    }
    return out;
}

void writeCStr(Value& av, const std::string& s, int line, const char* fn) {
    if (av.k != VK::Arr) rtFail(std::string(fn) + ": char 배열이 필요합니다", line);
    ArrObj* a = arrOf(av);
    size_t start = (size_t)av.i;
    size_t end = subEnd(av);
    if (start + s.size() + 1 > end)
        rtFail(std::string(fn) + ": 버퍼 크기 초과 (필요 " + std::to_string(s.size() + 1) + "바이트, 버퍼 " +
               std::to_string(end > start ? end - start : 0) + "바이트)", line);
    for (size_t k = 0; k <= s.size(); ++k) {
        Value& e = a->data[start + k];
        int64_t ch = k < s.size() ? (unsigned char)s[k] : 0;
        e.k = VK::Int;
        e.i = wrapInt(ch, isIntBT(e.t) ? e.t : BT::I8);
    }
}

int sizeOfType(const TypeInfo& t, const Program& p) {
    if (t.bt == BT::Struct && t.structId >= 0) {
        int total = 0;
        for (const FieldDef& f : p.structs[t.structId].fields) {
            int cnt = 1;
            for (int d : f.dims) cnt *= d;
            total += cnt * sizeOfType(f.type, p);
        }
        return total;
    }
    return btSize(t.bt);
}

int sizeOfValue(const Value& v, const Program& p) {
    switch (v.k) {
    case VK::Int: case VK::Float: return btSize(v.t);
    case VK::Str: return v.t == BT::Str ? 6 : (int)strOf(v).size() + 1;
    case VK::Arr: {
        if (v.t == BT::Ptr) return 2;
        ArrObj* a = arrOf(v);
        size_t cnt = v.lvl == 0 ? a->data.size() : a->strides[v.lvl - 1];
        TypeInfo et; et.bt = a->elem; et.structId = a->structId;
        return (int)cnt * sizeOfType(et, p);
    }
    case VK::Struct: { TypeInfo t; t.bt = BT::Struct; t.structId = structOf(v)->sid; return sizeOfType(t, p); }
    case VK::Ptr: return 2;
    case VK::Obj: return 1;
    default: return 0;
    }
}

Value makeDefault(Ctx& c, const TypeInfo& ti) {
    if (isIntBT(ti.bt)) return Value::I(0, ti.bt);
    if (isFloatBT(ti.bt)) return Value::F(0.0, ti.bt);
    switch (ti.bt) {
    case BT::Str: return Value::S(std::string());
    case BT::Ptr: { Value v; v.k = VK::Ptr; v.t = BT::Ptr; v.p = nullptr; return v; }
    case BT::Struct: {
        const StructDef& sd = c.prog->structs[ti.structId];
        auto so = std::make_shared<StructObj>();
        so->sid = ti.structId;
        for (const FieldDef& fd : sd.fields)
            so->f.push_back(fd.dims.empty() ? makeDefault(c, fd.type) : makeArray(c, fd.type, fd.dims));
        Value v; v.k = VK::Struct; v.t = BT::Struct; v.h = so; return v;
    }
    case BT::Obj: { Value v; v.k = VK::Obj; v.t = BT::Obj; return v; }
    default: return Value();
    }
}

Value makeArray(Ctx& c, const TypeInfo& elem, const std::vector<int>& dims) {
    auto a = std::make_shared<ArrObj>();
    a->elem = elem.bt;
    a->structId = elem.structId;
    a->dims = dims;
    a->strides.resize(dims.size());
    size_t total = 1;
    for (size_t k = dims.size(); k-- > 0;) { a->strides[k] = total; total *= (size_t)dims[k]; }
    if (elem.bt == BT::Struct) {
        a->data.reserve(total);
        for (size_t k = 0; k < total; ++k) a->data.push_back(makeDefault(c, elem));
    } else {
        a->data.assign(total, makeDefault(c, elem));
    }
    Value v; v.k = VK::Arr; v.t = BT::Void; v.h = a; return v;
}

LibValue toLib(const Value& v) {
    switch (v.k) {
    case VK::Int:
        if (v.t == BT::I8) return LibValue::makeString(std::string(1, (char)v.i));
        return LibValue::makeInt(v.i);
    case VK::Float: return LibValue::makeFloat(v.f);
    case VK::Str: return LibValue::makeString(strOf(v));
    case VK::Arr: {
        ArrObj* a = arrOf(v);
        if (a->elem == BT::I8) return LibValue::makeString(cstrOf(v));
        std::string bytes;
        size_t end = subEnd(v);
        for (size_t k = (size_t)v.i; k < end; ++k) {
            const Value& e = a->data[k];
            bytes.push_back((char)(uint8_t)(e.k == VK::Float ? dblToInt(e.f) : e.i));
        }
        return LibValue::makeString(bytes);
    }
    case VK::Obj: return LibValue::makeString(v.h ? libOf(v)->cls : std::string());
    default: return LibValue::makeVoid();
    }
}

Value fromLib(const LibValue& r) {
    switch (r.kind) {
    case LibValue::Int: {
        int64_t v = (int64_t)std::llround(r.num);
        BT t = (v >= INT32_MIN && v <= INT32_MAX) ? BT::I32 : (v >= 0 && v <= (int64_t)UINT32_MAX) ? BT::U32 : BT::I64;
        return Value::I(v, t);
    }
    case LibValue::Float: return Value::F((double)(float)r.num, BT::F32);
    case LibValue::String: return Value::S(r.str);
    default: return Value::I(0, BT::I16);
    }
}

} // namespace interp
} // namespace ard
