// InterpBuiltins.cpp - Arduino 내장 함수
#include "InterpCore.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace ard {
namespace interp {

namespace {

enum BI {
    BI_PINMODE, BI_DWRITE, BI_DREAD, BI_AREAD, BI_AWRITE, BI_DELAY, BI_DELAYUS, BI_MILLIS, BI_MICROS,
    BI_TONE, BI_NOTONE, BI_PULSEIN, BI_SHIFTOUT, BI_SHIFTIN, BI_ATTACH, BI_DETACH, BI_PIN2INT,
    BI_INTERRUPTS, BI_NOINTERRUPTS,
    BI_MAP, BI_CONSTRAIN, BI_MIN, BI_MAX, BI_ABS, BI_SQ, BI_SQRT, BI_POW, BI_SIN, BI_COS, BI_TAN,
    BI_ASIN, BI_ACOS, BI_ATAN, BI_ATAN2, BI_EXP, BI_LOG, BI_LOG10, BI_FLOOR, BI_CEIL, BI_ROUND,
    BI_FABS, BI_FMOD, BI_ISNAN, BI_ISINF, BI_RADIANS, BI_DEGREES, BI_TRUNC, BI_HYPOT,
    BI_RANDOM, BI_RANDOMSEED, BI_RAND, BI_BITREAD, BI_BITWRITE, BI_BITSET, BI_BITCLEAR, BI_BIT,
    BI_LOWBYTE, BI_HIGHBYTE, BI_MAKEWORD, BI_NOP,
    BI_SPRINTF, BI_SNPRINTF, BI_DTOSTRF, BI_STRLEN, BI_STRCMP, BI_STRNCMP, BI_STRCPY, BI_STRNCPY,
    BI_STRCAT, BI_STRNCAT, BI_ATOI, BI_ATOL, BI_ATOF, BI_ITOA, BI_LTOA, BI_UTOA, BI_ULTOA, BI_MEMSET,
    BI_ISDIGIT, BI_ISALPHA, BI_ISALNUM, BI_ISSPACE, BI_ISUPPER, BI_ISLOWER, BI_ISPUNCT, BI_ISXDIGIT,
    BI_ISPRINT, BI_ISASCII, BI_ISCNTRL, BI_ISGRAPH, BI_TOUPPER, BI_TOLOWER, BI_STRING,
    BI_PGM_BYTE, BI_PGM_WORD, BI_PGM_DWORD, BI_PGM_FLOAT, BI_PGM_PTR, BI_MEMCPY
};

struct BDef { const char* name; int id; int mn; int mx; };
const BDef kDefs[] = {
    {"pinMode", BI_PINMODE, 2, 2}, {"digitalWrite", BI_DWRITE, 2, 2}, {"digitalRead", BI_DREAD, 1, 1},
    {"analogRead", BI_AREAD, 1, 1}, {"analogWrite", BI_AWRITE, 2, 2}, {"delay", BI_DELAY, 1, 1},
    {"delayMicroseconds", BI_DELAYUS, 1, 1}, {"millis", BI_MILLIS, 0, 0}, {"micros", BI_MICROS, 0, 0},
    {"tone", BI_TONE, 2, 3}, {"noTone", BI_NOTONE, 1, 1}, {"pulseIn", BI_PULSEIN, 2, 3},
    {"pulseInLong", BI_PULSEIN, 2, 3}, {"shiftOut", BI_SHIFTOUT, 4, 4}, {"shiftIn", BI_SHIFTIN, 3, 3},
    {"attachInterrupt", BI_ATTACH, 3, 3}, {"detachInterrupt", BI_DETACH, 1, 1},
    {"digitalPinToInterrupt", BI_PIN2INT, 1, 1}, {"interrupts", BI_INTERRUPTS, 0, 0}, {"sei", BI_INTERRUPTS, 0, 0},
    {"noInterrupts", BI_NOINTERRUPTS, 0, 0}, {"cli", BI_NOINTERRUPTS, 0, 0},
    {"map", BI_MAP, 5, 5}, {"constrain", BI_CONSTRAIN, 3, 3}, {"min", BI_MIN, 2, 2}, {"max", BI_MAX, 2, 2},
    {"abs", BI_ABS, 1, 1}, {"labs", BI_ABS, 1, 1}, {"sq", BI_SQ, 1, 1}, {"sqrt", BI_SQRT, 1, 1},
    {"pow", BI_POW, 2, 2}, {"sin", BI_SIN, 1, 1}, {"cos", BI_COS, 1, 1}, {"tan", BI_TAN, 1, 1},
    {"asin", BI_ASIN, 1, 1}, {"acos", BI_ACOS, 1, 1}, {"atan", BI_ATAN, 1, 1}, {"atan2", BI_ATAN2, 2, 2},
    {"exp", BI_EXP, 1, 1}, {"log", BI_LOG, 1, 1}, {"log10", BI_LOG10, 1, 1}, {"floor", BI_FLOOR, 1, 1},
    {"ceil", BI_CEIL, 1, 1}, {"round", BI_ROUND, 1, 1}, {"fabs", BI_FABS, 1, 1}, {"fmod", BI_FMOD, 2, 2},
    {"isnan", BI_ISNAN, 1, 1}, {"isinf", BI_ISINF, 1, 1}, {"radians", BI_RADIANS, 1, 1},
    {"degrees", BI_DEGREES, 1, 1}, {"trunc", BI_TRUNC, 1, 1}, {"hypot", BI_HYPOT, 2, 2},
    {"sqrtf", BI_SQRT, 1, 1}, {"powf", BI_POW, 2, 2}, {"fabsf", BI_FABS, 1, 1}, {"floorf", BI_FLOOR, 1, 1},
    {"ceilf", BI_CEIL, 1, 1}, {"roundf", BI_ROUND, 1, 1},
    {"random", BI_RANDOM, 0, 2}, {"randomSeed", BI_RANDOMSEED, 1, 1}, {"srand", BI_RANDOMSEED, 1, 1},
    {"srandom", BI_RANDOMSEED, 1, 1}, {"rand", BI_RAND, 0, 0},
    {"bitRead", BI_BITREAD, 2, 2}, {"bitWrite", BI_BITWRITE, 3, 3}, {"bitSet", BI_BITSET, 2, 2},
    {"bitClear", BI_BITCLEAR, 2, 2}, {"bit", BI_BIT, 1, 1}, {"lowByte", BI_LOWBYTE, 1, 1},
    {"highByte", BI_HIGHBYTE, 1, 1}, {"makeWord", BI_MAKEWORD, 2, 2},
    {"yield", BI_NOP, 0, 0}, {"analogReference", BI_NOP, 1, 1}, {"analogReadResolution", BI_NOP, 1, 1},
    {"analogWriteResolution", BI_NOP, 1, 1}, {"wdt_reset", BI_NOP, 0, 0}, {"wdt_enable", BI_NOP, 1, 1},
    {"wdt_disable", BI_NOP, 0, 0},
    {"sprintf", BI_SPRINTF, 2, -1}, {"snprintf", BI_SNPRINTF, 3, -1}, {"dtostrf", BI_DTOSTRF, 4, 4},
    {"strlen", BI_STRLEN, 1, 1}, {"strcmp", BI_STRCMP, 2, 2}, {"strncmp", BI_STRNCMP, 3, 3},
    {"strcpy", BI_STRCPY, 2, 2}, {"strncpy", BI_STRNCPY, 3, 3}, {"strcat", BI_STRCAT, 2, 2},
    {"strncat", BI_STRNCAT, 3, 3}, {"atoi", BI_ATOI, 1, 1}, {"atol", BI_ATOL, 1, 1}, {"atof", BI_ATOF, 1, 1},
    {"itoa", BI_ITOA, 3, 3}, {"ltoa", BI_LTOA, 3, 3}, {"utoa", BI_UTOA, 3, 3}, {"ultoa", BI_ULTOA, 3, 3},
    {"memset", BI_MEMSET, 3, 3},
    {"isDigit", BI_ISDIGIT, 1, 1}, {"isdigit", BI_ISDIGIT, 1, 1}, {"isAlpha", BI_ISALPHA, 1, 1},
    {"isalpha", BI_ISALPHA, 1, 1}, {"isAlphaNumeric", BI_ISALNUM, 1, 1}, {"isalnum", BI_ISALNUM, 1, 1},
    {"isSpace", BI_ISSPACE, 1, 1}, {"isWhitespace", BI_ISSPACE, 1, 1}, {"isspace", BI_ISSPACE, 1, 1},
    {"isUpperCase", BI_ISUPPER, 1, 1}, {"isupper", BI_ISUPPER, 1, 1}, {"isLowerCase", BI_ISLOWER, 1, 1},
    {"islower", BI_ISLOWER, 1, 1}, {"isPunct", BI_ISPUNCT, 1, 1}, {"ispunct", BI_ISPUNCT, 1, 1},
    {"isHexadecimalDigit", BI_ISXDIGIT, 1, 1}, {"isxdigit", BI_ISXDIGIT, 1, 1},
    {"isPrintable", BI_ISPRINT, 1, 1}, {"isprint", BI_ISPRINT, 1, 1}, {"isAscii", BI_ISASCII, 1, 1},
    {"isControl", BI_ISCNTRL, 1, 1}, {"iscntrl", BI_ISCNTRL, 1, 1}, {"isGraph", BI_ISGRAPH, 1, 1},
    {"isgraph", BI_ISGRAPH, 1, 1}, {"toupper", BI_TOUPPER, 1, 1}, {"toUpperCase", BI_TOUPPER, 1, 1},
    {"tolower", BI_TOLOWER, 1, 1}, {"toLowerCase", BI_TOLOWER, 1, 1},
    {"String", BI_STRING, 0, 2},
    {"pgm_read_byte", BI_PGM_BYTE, 1, 1}, {"pgm_read_byte_near", BI_PGM_BYTE, 1, 1},
    {"pgm_read_word", BI_PGM_WORD, 1, 1}, {"pgm_read_word_near", BI_PGM_WORD, 1, 1},
    {"pgm_read_dword", BI_PGM_DWORD, 1, 1}, {"pgm_read_dword_near", BI_PGM_DWORD, 1, 1},
    {"pgm_read_float", BI_PGM_FLOAT, 1, 1}, {"pgm_read_float_near", BI_PGM_FLOAT, 1, 1},
    {"pgm_read_ptr", BI_PGM_PTR, 1, 1}, {"pgm_read_ptr_near", BI_PGM_PTR, 1, 1},
    {"strcpy_P", BI_STRCPY, 2, 2}, {"strncpy_P", BI_STRNCPY, 3, 3}, {"strcat_P", BI_STRCAT, 2, 2},
    {"strncat_P", BI_STRNCAT, 3, 3}, {"strcmp_P", BI_STRCMP, 2, 2}, {"strncmp_P", BI_STRNCMP, 3, 3},
    {"strlen_P", BI_STRLEN, 1, 1}, {"sprintf_P", BI_SPRINTF, 2, -1}, {"snprintf_P", BI_SNPRINTF, 3, -1},
    {"memcpy", BI_MEMCPY, 3, 3}, {"memcpy_P", BI_MEMCPY, 3, 3}, {"memmove", BI_MEMCPY, 3, 3},
    {nullptr, 0, 0, 0}};

int32_t avrRandom(uint32_t& st) {
    int32_t x = (int32_t)st;
    if (x == 0) x = 123459876;
    int32_t hi = x / 127773, lo = x % 127773;
    x = 16807 * lo - 2836 * hi;
    if (x < 0) x += 0x7fffffff;
    st = (uint32_t)x;
    return (int32_t)((uint32_t)x % ((uint32_t)0x7fffffff + 1u));
}

std::string lowerDigits(std::string s) {
    for (char& ch : s) ch = (char)std::tolower((unsigned char)ch);
    return s;
}

int charOf(Ctx&, const Value& v, int line) {
    int64_t x = toI(v, line);
    return (int)x;
}

bool ctest(int id, int ch) {
    if (ch < 0 || ch > 255) return false;
    switch (id) {
    case BI_ISDIGIT: return std::isdigit(ch) != 0;
    case BI_ISALPHA: return std::isalpha(ch) != 0;
    case BI_ISALNUM: return std::isalnum(ch) != 0;
    case BI_ISSPACE: return std::isspace(ch) != 0;
    case BI_ISUPPER: return std::isupper(ch) != 0;
    case BI_ISLOWER: return std::islower(ch) != 0;
    case BI_ISPUNCT: return std::ispunct(ch) != 0;
    case BI_ISXDIGIT: return std::isxdigit(ch) != 0;
    case BI_ISPRINT: return std::isprint(ch) != 0;
    case BI_ISASCII: return ch < 128;
    case BI_ISCNTRL: return std::iscntrl(ch) != 0;
    default: return std::isgraph(ch) != 0;
    }
}

Value fval(double d) { return Value::F((double)(float)d, BT::F32); }

} // namespace

int findBuiltin(const std::string& name, int& mn, int& mx) {
    static const std::unordered_map<std::string, const BDef*> idx = [] {
        std::unordered_map<std::string, const BDef*> m;
        for (const BDef* d = kDefs; d->name; ++d) m[d->name] = d;
        return m;
    }();
    auto it = idx.find(name);
    if (it == idx.end()) return -1;
    mn = it->second->mn;
    mx = it->second->mx;
    return it->second->id;
}

Value callBuiltin(Ctx& c, int id, std::vector<ExprP>& args, int line) {
    auto V = [&](size_t k) { return args[k]->eval(c); };
    auto I = [&](size_t k) { return toI(args[k]->eval(c), line); };
    auto D = [&](size_t k) { return toD(args[k]->eval(c), line); };
    ArduinoHost* h = c.host;
    switch (id) {
    case BI_PINMODE: { int p = (int)I(0); int m = (int)I(1); h->pinMode(p, m); return Value(); }
    case BI_DWRITE: { int p = (int)I(0); int v = truthy(V(1)) ? 1 : 0; h->digitalWrite(p, v); return Value(); }
    case BI_DREAD: return Value::I(h->digitalRead((int)I(0)) ? 1 : 0, BT::I16);
    case BI_AREAD: return Value::I(h->analogRead((int)I(0)), BT::I16);
    case BI_AWRITE: {
        int p = (int)I(0);
        int64_t v = I(1);
        h->analogWrite(p, (int)std::max<int64_t>(0, std::min<int64_t>(255, v)));
        return Value();
    }
    case BI_DELAY: { uint64_t ms = (uint32_t)I(0); c.delayUs(ms * 1000); return Value(); }
    case BI_DELAYUS: { uint64_t us = (uint16_t)I(0); c.delayUs(us); return Value(); }
    case BI_MILLIS: return Value::I((uint32_t)(h->micros() / 1000), BT::U32);
    case BI_MICROS: return Value::I((uint32_t)h->micros(), BT::U32);
    case BI_TONE: {
        int p = (int)I(0);
        unsigned int f = (unsigned int)(uint16_t)I(1);
        unsigned long d = args.size() > 2 ? (unsigned long)(uint32_t)I(2) : 0;
        h->tone(p, f, d);
        return Value();
    }
    case BI_NOTONE: h->noTone((int)I(0)); return Value();
    case BI_PULSEIN: {
        int p = (int)I(0);
        int st = (int)I(1);
        unsigned long to = args.size() > 2 ? (unsigned long)(uint32_t)I(2) : 1000000UL;
        unsigned long r = h->pulseIn(p, st, to);
        c.checkStop();
        return Value::I((uint32_t)r, BT::U32);
    }
    case BI_SHIFTOUT: {
        int dp = (int)I(0), cp = (int)I(1), order = (int)I(2);
        int v = (int)(uint8_t)I(3);
        for (int k = 0; k < 8; ++k) {
            int b = order == 0 ? (v >> k) & 1 : (v >> (7 - k)) & 1;
            h->digitalWrite(dp, b);
            h->digitalWrite(cp, 1);
            h->digitalWrite(cp, 0);
        }
        return Value();
    }
    case BI_SHIFTIN: {
        int dp = (int)I(0), cp = (int)I(1), order = (int)I(2);
        int v = 0;
        for (int k = 0; k < 8; ++k) {
            h->digitalWrite(cp, 1);
            int b = h->digitalRead(dp) ? 1 : 0;
            v |= order == 0 ? (b << k) : (b << (7 - k));
            h->digitalWrite(cp, 0);
        }
        return Value::I(v, BT::U8);
    }
    case BI_ATTACH: {
        int irq = (int)I(0);
        Value fv = V(1);
        int mode = (int)I(2);
        if (fv.k != VK::Func || !fv.fn) rtFail("attachInterrupt 의 두 번째 인자는 함수 이름이어야 합니다", line);
        if (irq < 0 || irq > 63) rtFail("attachInterrupt: 잘못된 인터럽트 번호 " + std::to_string(irq) + " (이 핀은 외부 인터럽트를 지원하지 않습니다)", line);
        if (!fv.fn->params.empty()) rtFail("인터럽트 함수(ISR)는 매개변수가 없어야 합니다: " + fv.fn->name, line);
        if ((int)c.isr.size() <= irq) c.isr.resize((size_t)irq + 1, nullptr);
        c.isr[(size_t)irq] = fv.fn;
        c.isrCount = (int)std::count_if(c.isr.begin(), c.isr.end(), [](FuncDef* f) { return f != nullptr; });
        h->attachInterrupt(irq, mode);
        return Value();
    }
    case BI_DETACH: {
        int irq = (int)I(0);
        if (irq >= 0 && irq < (int)c.isr.size()) c.isr[(size_t)irq] = nullptr;
        c.isrCount = (int)std::count_if(c.isr.begin(), c.isr.end(), [](FuncDef* f) { return f != nullptr; });
        h->detachInterrupt(irq);
        return Value();
    }
    case BI_PIN2INT: return Value::I(h->digitalPinToInterrupt((int)I(0)), BT::I16);
    case BI_INTERRUPTS: c.intEnabled = true; return Value();
    case BI_NOINTERRUPTS: c.intEnabled = false; return Value();
    case BI_MAP: {
        int64_t x = wrapInt(I(0), BT::I32), a = wrapInt(I(1), BT::I32), b = wrapInt(I(2), BT::I32);
        int64_t o1 = wrapInt(I(3), BT::I32), o2 = wrapInt(I(4), BT::I32);
        int64_t den = wrapInt(b - a, BT::I32);
        if (den == 0) rtFail("map(): fromLow 와 fromHigh 가 같아서 0으로 나눌 수 없습니다", line);
        int64_t num = wrapInt(wrapInt(x - a, BT::I32) * wrapInt(o2 - o1, BT::I32), BT::I32);
        return Value::I(wrapInt(num / den + o1, BT::I32), BT::I32);
    }
    case BI_CONSTRAIN: {
        Value x = V(0), lo = V(1), hi = V(2);
        if (truthy(binop(OP_LT, x, lo, line))) return lo;
        if (truthy(binop(OP_GT, x, hi, line))) return hi;
        return x;
    }
    case BI_MIN: case BI_MAX: {
        Value a = V(0), b = V(1);
        bool pickA = truthy(binop(id == BI_MIN ? OP_LT : OP_GT, a, b, line));
        Value r = pickA ? a : b;
        if ((a.k == VK::Float || b.k == VK::Float) && r.k == VK::Int) return fval(toD(r, line));
        return r;
    }
    case BI_ABS: {
        Value a = V(0);
        if (a.k == VK::Float) return Value::F(std::fabs(a.f), a.t);
        if (truthy(binop(OP_GT, a, Value::I(0, BT::I16), line))) return unop(U_PLUS, a, line);
        return unop(U_NEG, a, line);
    }
    case BI_SQ: { Value a = V(0); return binop(OP_MUL, a, a, line); }
    case BI_SQRT: return fval(std::sqrt(D(0)));
    case BI_POW: { double b = D(0); return fval(std::pow(b, D(1))); }
    case BI_SIN: return fval(std::sin(D(0)));
    case BI_COS: return fval(std::cos(D(0)));
    case BI_TAN: return fval(std::tan(D(0)));
    case BI_ASIN: return fval(std::asin(D(0)));
    case BI_ACOS: return fval(std::acos(D(0)));
    case BI_ATAN: return fval(std::atan(D(0)));
    case BI_ATAN2: { double y = D(0); return fval(std::atan2(y, D(1))); }
    case BI_EXP: return fval(std::exp(D(0)));
    case BI_LOG: return fval(std::log(D(0)));
    case BI_LOG10: return fval(std::log10(D(0)));
    case BI_FLOOR: return fval(std::floor(D(0)));
    case BI_CEIL: return fval(std::ceil(D(0)));
    case BI_TRUNC: return fval(std::trunc(D(0)));
    case BI_HYPOT: { double x = D(0); return fval(std::hypot(x, D(1))); }
    case BI_ROUND: {
        Value a = V(0);
        if (a.k == VK::Int) return Value::I(wrapInt(a.i, BT::I32), BT::I32);
        double x = toD(a, line);
        int64_t r = x >= 0 ? dblToInt(x + 0.5) : dblToInt(x - 0.5);
        return Value::I(wrapInt(r, BT::I32), BT::I32);
    }
    case BI_FABS: return fval(std::fabs(D(0)));
    case BI_FMOD: { double x = D(0); return fval(std::fmod(x, D(1))); }
    case BI_ISNAN: return Value::B(std::isnan(D(0)));
    case BI_ISINF: return Value::B(std::isinf(D(0)));
    case BI_RADIANS: return fval(D(0) * 0.017453292519943295);
    case BI_DEGREES: return fval(D(0) * 57.29577951308232);
    case BI_RANDOM: {
        if (args.empty()) return Value::I(avrRandom(c.randState), BT::I32);
        int64_t lo = 0, hi;
        if (args.size() == 2) { lo = wrapInt(I(0), BT::I32); hi = wrapInt(I(1), BT::I32); }
        else hi = wrapInt(I(0), BT::I32);
        if (args.size() == 2) {
            if (lo >= hi) return Value::I(lo, BT::I32);
            hi = wrapInt(hi - lo, BT::I32);
        }
        if (hi == 0) return Value::I(lo, BT::I32);
        int64_t r = avrRandom(c.randState) % hi;
        return Value::I(wrapInt(r + lo, BT::I32), BT::I32);
    }
    case BI_RANDOMSEED: { uint32_t s = (uint32_t)I(0); if (s != 0) c.randState = s; return Value(); }
    case BI_RAND: return Value::I(avrRandom(c.randState) % 32768, BT::I16);
    case BI_BITREAD: {
        Value a = V(0);
        Value s = binop(OP_SHR, a, V(1), line);
        return binop(OP_AND, s, Value::I(1, BT::I16), line);
    }
    case BI_BITSET: case BI_BITCLEAR: case BI_BITWRITE: {
        Value* p = args[0]->ref(c);
        int64_t b = I(1);
        bool set = id == BI_BITSET || (id == BI_BITWRITE && truthy(V(2)));
        Value mask = Value::I(b >= 0 && b < 64 ? (int64_t)(1ULL << b) : 0, b < 32 ? BT::U32 : BT::U64);
        Value r = set ? binop(OP_OR, *p, mask, line) : binop(OP_AND, *p, unop(U_BITNOT, mask, line), line);
        assignTo(*p, r, line);
        return *p;
    }
    case BI_BIT: { int64_t b = I(0); return Value::I(b >= 0 && b < 32 ? (int64_t)(1ULL << b) : 0, BT::U32); }
    case BI_LOWBYTE: return Value::I(I(0) & 0xFF, BT::U8);
    case BI_HIGHBYTE: return Value::I((I(0) >> 8) & 0xFF, BT::U8);
    case BI_MAKEWORD: { int64_t hb = I(0); return Value::I(((hb & 0xFF) << 8) | (I(1) & 0xFF), BT::U16); }
    case BI_NOP: for (auto& a : args) a->eval(c); return Value();
    case BI_SPRINTF: case BI_SNPRINTF: {
        Value buf = V(0);
        size_t fi = id == BI_SPRINTF ? 1 : 2;
        int64_t lim = id == BI_SNPRINTF ? I(1) : -1;
        std::string fmt = toStr(V(fi));
        std::vector<Value> vals;
        for (size_t k = fi + 1; k < args.size(); ++k) vals.push_back(V(k));
        std::string s = formatPrintf(fmt, vals, 0, line);
        size_t full = s.size();
        if (lim >= 0) {
            if (lim == 0) return Value::I((int64_t)full, BT::I16);
            if ((int64_t)s.size() > lim - 1) s.resize((size_t)(lim - 1));
        }
        writeCStr(buf, s, line, id == BI_SPRINTF ? "sprintf" : "snprintf");
        return Value::I((int64_t)full, BT::I16);
    }
    case BI_DTOSTRF: {
        double v = D(0);
        int w = (int)I(1), pr = (int)I(2);
        w = std::max(-60, std::min(60, w));
        pr = std::max(0, std::min(20, pr));
        char tmp[200];
        std::snprintf(tmp, sizeof tmp, "%*.*f", w, pr, v);
        Value buf = V(3);
        writeCStr(buf, tmp, line, "dtostrf");
        return buf;
    }
    case BI_STRLEN: return Value::I((int64_t)cstrOf(V(0)).size(), BT::U16);
    case BI_STRCMP: case BI_STRNCMP: {
        std::string a = cstrOf(V(0)), b = cstrOf(V(1));
        size_t n = id == BI_STRNCMP ? (size_t)std::max<int64_t>(0, I(2)) : (size_t)-1;
        int r = 0;
        for (size_t k = 0; k < n; ++k) {
            int ca = k < a.size() ? (unsigned char)a[k] : 0;
            int cb = k < b.size() ? (unsigned char)b[k] : 0;
            if (ca != cb) { r = ca - cb; break; }
            if (ca == 0) break;
        }
        return Value::I(r, BT::I16);
    }
    case BI_STRCPY: { Value d = V(0); writeCStr(d, cstrOf(V(1)), line, "strcpy"); return d; }
    case BI_STRCAT: { Value d = V(0); std::string s = cstrOf(d) + cstrOf(V(1)); writeCStr(d, s, line, "strcat"); return d; }
    case BI_STRNCAT: {
        Value d = V(0);
        std::string src = cstrOf(V(1));
        size_t n = (size_t)std::max<int64_t>(0, I(2));
        writeCStr(d, cstrOf(d) + src.substr(0, std::min(n, src.size())), line, "strncat");
        return d;
    }
    case BI_STRNCPY: {
        Value d = V(0);
        std::string src = cstrOf(V(1));
        int64_t n = I(2);
        if (d.k != VK::Arr) rtFail("strncpy: char 배열이 필요합니다", line);
        ArrObj* a = arrOf(d);
        for (int64_t k = 0; k < n; ++k) {
            size_t idx = (size_t)(d.i + k);
            if (idx >= a->data.size()) rtFail("strncpy: 버퍼 크기 초과", line);
            Value& e = a->data[idx];
            e.k = VK::Int;
            e.i = wrapInt(k < (int64_t)src.size() ? (unsigned char)src[(size_t)k] : 0, isIntBT(e.t) ? e.t : BT::I8);
        }
        return d;
    }
    case BI_ATOI: return Value::I(wrapInt(std::strtoll(cstrOf(V(0)).c_str(), nullptr, 10), BT::I16), BT::I16);
    case BI_ATOL: return Value::I(wrapInt(std::strtoll(cstrOf(V(0)).c_str(), nullptr, 10), BT::I32), BT::I32);
    case BI_ATOF: return fval(std::atof(cstrOf(V(0)).c_str()));
    case BI_ITOA: case BI_LTOA: case BI_UTOA: case BI_ULTOA: {
        int64_t v = I(0);
        Value buf = V(1);
        int base = (int)I(2);
        if (base < 2 || base > 36) base = 10;
        BT t = id == BI_ITOA ? BT::I16 : id == BI_LTOA ? BT::I32 : id == BI_UTOA ? BT::U16 : BT::U32;
        v = wrapInt(v, t);
        if (base != 10 && v < 0) v = wrapInt(v, t == BT::I16 ? BT::U16 : BT::U32);
        writeCStr(buf, lowerDigits(fmtInteger(v, t, base)), line, "itoa");
        return buf;
    }
    case BI_MEMSET: {
        Value d = V(0);
        int64_t v = I(1), n = I(2);
        if (d.k != VK::Arr) rtFail("memset: 배열이 필요합니다", line);
        ArrObj* a = arrOf(d);
        for (int64_t k = 0; k < n; ++k) {
            size_t idx = (size_t)(d.i + k);
            if (idx >= a->data.size()) rtFail("memset: 배열 크기 초과", line);
            Value& e = a->data[idx];
            if (isIntBT(e.t)) { e.k = VK::Int; e.i = wrapInt((uint8_t)v, e.t); }
            else if (isFloatBT(e.t)) { e.k = VK::Float; e.f = 0; }
        }
        return d;
    }
    case BI_ISDIGIT: case BI_ISALPHA: case BI_ISALNUM: case BI_ISSPACE: case BI_ISUPPER: case BI_ISLOWER:
    case BI_ISPUNCT: case BI_ISXDIGIT: case BI_ISPRINT: case BI_ISASCII: case BI_ISCNTRL: case BI_ISGRAPH:
        return Value::B(ctest(id, charOf(c, V(0), line)));
    case BI_TOUPPER: { int ch = charOf(c, V(0), line); return Value::I(ch >= 0 && ch < 256 ? std::toupper(ch) : ch, BT::I16); }
    case BI_TOLOWER: { int ch = charOf(c, V(0), line); return Value::I(ch >= 0 && ch < 256 ? std::tolower(ch) : ch, BT::I16); }
    case BI_STRING: return constructString(c, args, line);
    case BI_PGM_BYTE: case BI_PGM_WORD: case BI_PGM_DWORD: case BI_PGM_FLOAT: case BI_PGM_PTR: {
        Value v = V(0);
        Value e;
        if (v.k == VK::Arr) {
            ArrObj* a = arrOf(v);
            if (v.i < 0 || v.i >= (int64_t)a->data.size()) rtFail("pgm_read: 주소가 배열 범위를 벗어났습니다", line);
            e = a->data[(size_t)v.i];
        } else if (v.k == VK::Ptr) {
            if (!v.p) rtFail("pgm_read: NULL 포인터입니다", line);
            e = *v.p;
        } else if (v.k == VK::Str) {
            const std::string& s = strOf(v);
            e = Value::I(s.empty() ? 0 : (int8_t)s[0], BT::I8);
        } else e = v;
        if (id == BI_PGM_PTR) return e;
        Value d = id == BI_PGM_FLOAT ? Value::F(0) : Value::I(0, id == BI_PGM_BYTE ? BT::U8 : id == BI_PGM_WORD ? BT::U16 : BT::U32);
        assignTo(d, e, line);
        return d;
    }
    case BI_MEMCPY: {
        Value d = V(0), s = V(1);
        int64_t n = I(2);
        if (d.k == VK::Ptr && s.k == VK::Ptr) {
            if (!d.p || !s.p) rtFail("memcpy: NULL 포인터입니다", line);
            assignTo(*d.p, *s.p, line);
            return d;
        }
        if (d.k != VK::Arr || (s.k != VK::Arr && s.k != VK::Str)) rtFail("memcpy: 배열이 필요합니다", line);
        ArrObj* da = arrOf(d);
        int es = std::max(1, btSize(da->elem));
        int64_t cnt = (n + es - 1) / es;
        for (int64_t k = 0; k < cnt; ++k) {
            size_t di = (size_t)(d.i + k);
            if (di >= da->data.size()) rtFail("memcpy: 대상 배열 크기 초과", line);
            if (s.k == VK::Str) {
                const std::string& ss = strOf(s);
                assignTo(da->data[di], Value::I(k < (int64_t)ss.size() ? (int8_t)ss[(size_t)k] : 0, BT::I8), line);
            } else {
                ArrObj* sa = arrOf(s);
                size_t si = (size_t)(s.i + k);
                if (si >= sa->data.size()) rtFail("memcpy: 원본 배열 크기 초과", line);
                Value tmp = sa->data[si];
                assignTo(da->data[di], tmp, line);
            }
        }
        return d;
    }
    default: rtFail("알 수 없는 내장 함수", line);
    }
}

} // namespace interp
} // namespace ard
