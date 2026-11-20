// InterpObjects.cpp - Serial, String 메서드, EEPROM, 라이브러리 객체 호출
#include "InterpCore.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace ard {
namespace interp {

namespace {

enum SerialM {
    SM_BEGIN, SM_END, SM_PRINT, SM_PRINTLN, SM_WRITE, SM_AVAILABLE, SM_AVAILFW, SM_READ, SM_PEEK, SM_FLUSH,
    SM_PARSEINT, SM_PARSEFLOAT, SM_READSTRING, SM_READSTRINGUNTIL, SM_READBYTES, SM_READBYTESUNTIL,
    SM_SETTIMEOUT, SM_FIND, SM_PRINTF
};
struct MDef { const char* name; int id; int mn; int mx; };
const MDef kSerial[] = {
    {"begin", SM_BEGIN, 1, 2}, {"end", SM_END, 0, 0}, {"print", SM_PRINT, 1, 2}, {"println", SM_PRINTLN, 0, 2},
    {"write", SM_WRITE, 1, 2}, {"available", SM_AVAILABLE, 0, 0}, {"availableForWrite", SM_AVAILFW, 0, 0},
    {"read", SM_READ, 0, 0}, {"peek", SM_PEEK, 0, 0}, {"flush", SM_FLUSH, 0, 0},
    {"parseInt", SM_PARSEINT, 0, 2}, {"parseFloat", SM_PARSEFLOAT, 0, 2}, {"readString", SM_READSTRING, 0, 0},
    {"readStringUntil", SM_READSTRINGUNTIL, 1, 1}, {"readBytes", SM_READBYTES, 2, 2},
    {"readBytesUntil", SM_READBYTESUNTIL, 3, 3}, {"setTimeout", SM_SETTIMEOUT, 1, 1}, {"find", SM_FIND, 1, 1},
    {"printf", SM_PRINTF, 1, -1}, {nullptr, 0, 0, 0}};

enum StrM {
    S_LENGTH, S_CHARAT, S_SETCHARAT, S_INDEXOF, S_LASTINDEXOF, S_SUBSTRING, S_TOINT, S_TOFLOAT, S_TOUPPER,
    S_TOLOWER, S_TRIM, S_REPLACE, S_STARTSWITH, S_ENDSWITH, S_EQUALS, S_EQUALSIC, S_CONCAT, S_CSTR, S_REMOVE,
    S_ISEMPTY, S_COMPARETO, S_RESERVE, S_GETBYTES, S_TOCHARARRAY
};
const MDef kStr[] = {
    {"length", S_LENGTH, 0, 0}, {"charAt", S_CHARAT, 1, 1}, {"setCharAt", S_SETCHARAT, 2, 2},
    {"indexOf", S_INDEXOF, 1, 2}, {"lastIndexOf", S_LASTINDEXOF, 1, 2}, {"substring", S_SUBSTRING, 1, 2},
    {"toInt", S_TOINT, 0, 0}, {"toFloat", S_TOFLOAT, 0, 0}, {"toDouble", S_TOFLOAT, 0, 0},
    {"toUpperCase", S_TOUPPER, 0, 0}, {"toLowerCase", S_TOLOWER, 0, 0}, {"trim", S_TRIM, 0, 0},
    {"replace", S_REPLACE, 2, 2}, {"startsWith", S_STARTSWITH, 1, 2}, {"endsWith", S_ENDSWITH, 1, 1},
    {"equals", S_EQUALS, 1, 1}, {"equalsIgnoreCase", S_EQUALSIC, 1, 1}, {"concat", S_CONCAT, 1, 1},
    {"c_str", S_CSTR, 0, 0}, {"remove", S_REMOVE, 1, 2}, {"isEmpty", S_ISEMPTY, 0, 0},
    {"compareTo", S_COMPARETO, 1, 1}, {"reserve", S_RESERVE, 1, 1}, {"getBytes", S_GETBYTES, 2, 3},
    {"toCharArray", S_TOCHARARRAY, 2, 3}, {nullptr, 0, 0, 0}};

enum EepM { E_READ, E_WRITE, E_UPDATE, E_LENGTH, E_GET, E_PUT, E_NOP };
const MDef kEep[] = {
    {"read", E_READ, 1, 1}, {"write", E_WRITE, 2, 2}, {"update", E_UPDATE, 2, 2}, {"length", E_LENGTH, 0, 0},
    {"get", E_GET, 2, 2}, {"put", E_PUT, 2, 2}, {"begin", E_NOP, 0, 1}, {"commit", E_NOP, 0, 0},
    {"end", E_NOP, 0, 0}, {nullptr, 0, 0, 0}};

const MDef* findDef(const MDef* tab, const std::string& n) {
    for (const MDef* d = tab; d->name; ++d) if (n == d->name) return d;
    return nullptr;
}

void checkArgs(const MDef* tab, int id, size_t n, const char* owner, int line) {
    for (const MDef* d = tab; d->name; ++d) {
        if (d->id != id) continue;
        if ((int)n >= d->mn && (d->mx < 0 || (int)n <= d->mx)) return;
    }
    for (const MDef* d = tab; d->name; ++d)
        if (d->id == id) rtFail(std::string(owner) + "." + d->name + "() 의 인자 개수가 맞지 않습니다", line);
}

int timedRead(Ctx& c) {
    uint64_t start = c.host->micros();
    while (true) {
        int ch = c.host->serialRead();
        if (ch >= 0) return ch;
        c.checkStop();
        if (c.host->micros() - start >= (uint64_t)c.serialTimeout * 1000) return -1;
        c.delayUs(1000);
    }
}

int timedPeek(Ctx& c) {
    uint64_t start = c.host->micros();
    while (true) {
        int ch = c.host->serialPeek();
        if (ch >= 0) return ch;
        c.checkStop();
        if (c.host->micros() - start >= (uint64_t)c.serialTimeout * 1000) return -1;
        c.delayUs(1000);
    }
}

int peekNextDigit(Ctx& c, bool detectDecimal) {
    while (true) {
        int ch = timedPeek(c);
        if (ch < 0 || ch == '-' || (ch >= '0' && ch <= '9') || (detectDecimal && ch == '.')) return ch;
        c.host->serialRead();
    }
}

size_t arrEnd(const Value& v) {
    ArrObj* a = arrOf(v);
    if (v.lvl > 0) return std::min(a->data.size(), (size_t)v.i + a->strides[v.lvl - 1]);
    return a->data.size();
}

std::string rawBytes(const Value& v, int64_t len, int line) {
    if (len < 0) len = 0;
    if (v.k == VK::Str) {
        const std::string& s = strOf(v);
        if ((size_t)len > s.size()) rtFail("write(): 길이가 문자열보다 깁니다", line);
        return s.substr(0, (size_t)len);
    }
    if (v.k != VK::Arr) rtFail("write(버퍼, 길이): 배열이 필요합니다", line);
    ArrObj* a = arrOf(v);
    size_t st = (size_t)v.i;
    if (st + (size_t)len > arrEnd(v)) rtFail("write(): 길이가 배열 크기보다 깁니다", line);
    std::string r;
    for (size_t k = 0; k < (size_t)len; ++k) {
        const Value& e = a->data[st + k];
        r.push_back((char)(uint8_t)(e.k == VK::Float ? dblToInt(e.f) : e.i));
    }
    return r;
}

void storeBytes(Value& buf, const std::string& s, bool nulTerm, int line, const char* fn) {
    if (buf.k != VK::Arr) rtFail(std::string(fn) + ": 배열 버퍼가 필요합니다", line);
    ArrObj* a = arrOf(buf);
    size_t st = (size_t)buf.i;
    if (st + s.size() + (nulTerm ? 1 : 0) > arrEnd(buf)) rtFail(std::string(fn) + ": 버퍼 크기 초과", line);
    for (size_t k = 0; k <= s.size(); ++k) {
        if (k == s.size() && !nulTerm) break;
        Value& e = a->data[st + k];
        e.k = VK::Int;
        e.i = wrapInt(k < s.size() ? (unsigned char)s[k] : 0, isIntBT(e.t) ? e.t : BT::I8);
    }
}

std::string charOrStr(const Value& v) {
    if (v.k == VK::Int) return std::string(1, (char)v.i);
    return toStr(v);
}

void serialize(const Value& v, std::vector<uint8_t>& out, int line) {
    switch (v.k) {
    case VK::Int: {
        int n = btSize(v.t);
        uint64_t u = (uint64_t)v.i;
        for (int k = 0; k < n; ++k) out.push_back((uint8_t)(u >> (8 * k)));
        return;
    }
    case VK::Float: {
        float f = (float)v.f;
        uint32_t u;
        std::memcpy(&u, &f, 4);
        for (int k = 0; k < 4; ++k) out.push_back((uint8_t)(u >> (8 * k)));
        return;
    }
    case VK::Arr: {
        ArrObj* a = arrOf(v);
        for (size_t k = (size_t)v.i; k < arrEnd(v); ++k) serialize(a->data[k], out, line);
        return;
    }
    case VK::Struct:
        for (const Value& f : structOf(v)->f) serialize(f, out, line);
        return;
    case VK::Str: rtFail("EEPROM.get/put 에는 String 을 사용할 수 없습니다 (char 배열을 사용하세요)", line);
    default: rtFail("EEPROM.get/put 에 사용할 수 없는 종류의 값입니다", line);
    }
}

void deserialize(Value& v, const std::vector<uint8_t>& in, size_t& p, int line) {
    switch (v.k) {
    case VK::Int: {
        int n = btSize(v.t);
        uint64_t u = 0;
        for (int k = 0; k < n; ++k) u |= (uint64_t)in[p++] << (8 * k);
        v.i = wrapInt((int64_t)u, v.t);
        return;
    }
    case VK::Float: {
        uint32_t u = 0;
        for (int k = 0; k < 4; ++k) u |= (uint32_t)in[p++] << (8 * k);
        float f;
        std::memcpy(&f, &u, 4);
        v.f = f;
        return;
    }
    case VK::Arr: {
        ArrObj* a = arrOf(v);
        for (size_t k = (size_t)v.i; k < arrEnd(v); ++k) deserialize(a->data[k], in, p, line);
        return;
    }
    case VK::Struct:
        for (Value& f : structOf(v)->f) deserialize(f, in, p, line);
        return;
    default: rtFail("EEPROM.get 에 사용할 수 없는 종류의 변수입니다", line);
    }
}

} // namespace

// ---------------- Serial ----------------
int findSerialMethod(const std::string& name, int& mn, int& mx) {
    const MDef* d = findDef(kSerial, name);
    if (!d) return -1;
    mn = d->mn;
    mx = d->mx;
    return d->id;
}

Value callSerial(Ctx& c, int id, std::vector<ExprP>& args, int line) {
    ArduinoHost* h = c.host;
    auto V = [&](size_t k) { return args[k]->eval(c); };
    auto I = [&](size_t k) { return toI(args[k]->eval(c), line); };
    switch (id) {
    case SM_BEGIN: h->serialBegin((long)I(0)); return Value();
    case SM_END: case SM_FLUSH: return Value();
    case SM_PRINT: case SM_PRINTLN: {
        std::string s;
        if (!args.empty()) {
            Value v = V(0);
            s = args.size() > 1 ? printText2(v, (int)I(1), line) : printText(v, line);
        }
        if (id == SM_PRINTLN) s += "\r\n";
        if (!s.empty()) h->serialWrite(s);
        return Value::I((int64_t)s.size(), BT::U16);
    }
    case SM_WRITE: {
        Value v = V(0);
        std::string s;
        if (args.size() == 2) s = rawBytes(v, I(1), line);
        else if (isStrLike(v)) s = cstrOf(v);
        else s = std::string(1, (char)(uint8_t)toI(v, line));
        h->serialWrite(s);
        return Value::I((int64_t)s.size(), BT::U16);
    }
    case SM_AVAILABLE: return Value::I(h->serialAvailable(), BT::I16);
    case SM_AVAILFW: return Value::I(63, BT::I16);
    case SM_READ: return Value::I(h->serialRead(), BT::I16);
    case SM_PEEK: return Value::I(h->serialPeek(), BT::I16);
    case SM_SETTIMEOUT: c.serialTimeout = (long)std::max<int64_t>(0, I(0)); return Value();
    case SM_PARSEINT: {
        for (auto& a : args) a->eval(c);
        int ch = peekNextDigit(c, false);
        if (ch < 0) return Value::I(0, BT::I32);
        bool neg = false;
        int64_t v = 0;
        do {
            if (ch == '-') neg = true;
            else if (ch >= '0' && ch <= '9') v = v * 10 + (ch - '0');
            h->serialRead();
            ch = timedPeek(c);
        } while (ch >= '0' && ch <= '9');
        return Value::I(wrapInt(neg ? -v : v, BT::I32), BT::I32);
    }
    case SM_PARSEFLOAT: {
        for (auto& a : args) a->eval(c);
        int ch = peekNextDigit(c, true);
        if (ch < 0) return Value::F(0);
        bool neg = false, frac = false;
        double v = 0, fr = 1.0;
        do {
            if (ch == '-') neg = true;
            else if (ch == '.') frac = true;
            else if (ch >= '0' && ch <= '9') {
                if (frac) { fr *= 0.1; v += fr * (ch - '0'); }
                else v = v * 10 + (ch - '0');
            }
            h->serialRead();
            ch = timedPeek(c);
        } while ((ch >= '0' && ch <= '9') || (ch == '.' && !frac));
        return Value::F((double)(float)(neg ? -v : v));
    }
    case SM_READSTRING: case SM_READSTRINGUNTIL: {
        int term = id == SM_READSTRINGUNTIL ? (int)(uint8_t)I(0) : -2;
        std::string s;
        int ch = timedRead(c);
        while (ch >= 0 && ch != term) { s.push_back((char)ch); ch = timedRead(c); }
        return Value::S(s);
    }
    case SM_READBYTES: case SM_READBYTESUNTIL: {
        int term = -2;
        size_t bi = 0;
        if (id == SM_READBYTESUNTIL) { term = (int)(uint8_t)I(0); bi = 1; }
        Value buf = V(bi);
        int64_t len = I(bi + 1);
        std::string s;
        while ((int64_t)s.size() < len) {
            int ch = timedRead(c);
            if (ch < 0 || ch == term) break;
            s.push_back((char)ch);
        }
        storeBytes(buf, s, false, line, "readBytes");
        return Value::I((int64_t)s.size(), BT::U16);
    }
    case SM_FIND: {
        std::string target = cstrOf(V(0));
        if (target.empty()) return Value::B(true);
        size_t matched = 0;
        while (true) {
            int ch = timedRead(c);
            if (ch < 0) return Value::B(false);
            if ((char)ch == target[matched]) { if (++matched == target.size()) return Value::B(true); }
            else matched = ((char)ch == target[0]) ? 1 : 0;
        }
    }
    case SM_PRINTF: {
        std::string fmt = toStr(V(0));
        std::vector<Value> vals;
        for (size_t k = 1; k < args.size(); ++k) vals.push_back(V(k));
        std::string s = formatPrintf(fmt, vals, 0, line);
        h->serialWrite(s);
        return Value::I((int64_t)s.size(), BT::U16);
    }
    default: rtFail("알 수 없는 Serial 함수", line);
    }
}

// ---------------- String ----------------
int findStringMethod(const std::string& name) {
    const MDef* d = findDef(kStr, name);
    return d ? d->id : -1;
}

Value constructString(Ctx& c, std::vector<ExprP>& args, int line) {
    if (args.empty()) return Value::S(std::string());
    Value v = args[0]->eval(c);
    if (args.size() == 1) {
        if (v.k == VK::Void) rtFail("void 값으로 String 을 만들 수 없습니다", line);
        if (v.k == VK::Obj || v.k == VK::Ptr || v.k == VK::Struct) rtFail("이 값으로는 String 을 만들 수 없습니다", line);
        return Value::S(toStr(v));
    }
    int64_t arg = toI(args[1]->eval(c), line);
    if (v.k == VK::Float) {
        int dec = (int)std::max<int64_t>(0, std::min<int64_t>(20, arg));
        char buf[400];
        std::snprintf(buf, sizeof buf, "%.*f", dec, v.f);
        return Value::S(buf);
    }
    if (v.k == VK::Int) {
        int base = (int)arg;
        if (base < 2 || base > 36) base = 10;
        BT t = promoteBT(v.t);
        int64_t x = wrapInt(v.i, t);
        if (base != 10 && x < 0) x = wrapInt(x, t == BT::I16 ? BT::U16 : BT::U32);
        std::string s = fmtInteger(x, t, base);
        for (char& ch : s) ch = (char)std::tolower((unsigned char)ch);
        return Value::S(s);
    }
    return Value::S(toStr(v));
}

Value callStringMethod(Ctx& c, Value* r, int id, const std::string& name, std::vector<ExprP>& args, int line) {
    const MDef* d = nullptr;
    for (const MDef* q = kStr; q->name; ++q) if (q->id == id && name == q->name) d = q;
    if (d && ((int)args.size() < d->mn || (d->mx >= 0 && (int)args.size() > d->mx)))
        rtFail("String." + name + "() 의 인자 개수가 맞지 않습니다", line);
    auto V = [&](size_t k) { return args[k]->eval(c); };
    auto I = [&](size_t k) { return toI(args[k]->eval(c), line); };
    const std::string s = strOf(*r);
    int64_t len = (int64_t)s.size();
    switch (id) {
    case S_LENGTH: return Value::I(len, BT::U16);
    case S_CHARAT: {
        int64_t i = I(0);
        return Value::I(i >= 0 && i < len ? (int8_t)s[(size_t)i] : 0, BT::I8);
    }
    case S_SETCHARAT: {
        int64_t i = I(0);
        int64_t ch = I(1);
        if (i >= 0 && i < len) mutStr(*r)[(size_t)i] = (char)ch;
        return Value();
    }
    case S_INDEXOF: case S_LASTINDEXOF: {
        std::string needle = charOrStr(V(0));
        size_t res;
        if (id == S_INDEXOF) {
            int64_t from = args.size() > 1 ? I(1) : 0;
            if (from < 0) from = 0;
            if (from >= len && !(needle.empty() && from == len)) return Value::I(-1, BT::I16);
            res = s.find(needle, (size_t)from);
        } else {
            int64_t from = args.size() > 1 ? I(1) : len;
            if (from < 0) return Value::I(-1, BT::I16);
            res = s.rfind(needle, (size_t)from);
        }
        return Value::I(res == std::string::npos ? -1 : (int64_t)res, BT::I16);
    }
    case S_SUBSTRING: {
        int64_t a = I(0);
        int64_t b = args.size() > 1 ? I(1) : len;
        if (a < 0) a = 0;
        if (b < 0) b = 0;
        if (a > b) std::swap(a, b);
        if (a >= len) return Value::S(std::string());
        if (b > len) b = len;
        return Value::S(s.substr((size_t)a, (size_t)(b - a)));
    }
    case S_TOINT: return Value::I(wrapInt(std::strtoll(s.c_str(), nullptr, 10), BT::I32), BT::I32);
    case S_TOFLOAT: return Value::F((double)(float)std::atof(s.c_str()));
    case S_TOUPPER: case S_TOLOWER: {
        std::string& m = mutStr(*r);
        for (char& ch : m) {
            unsigned char u = (unsigned char)ch;
            if (u < 128) ch = (char)(id == S_TOUPPER ? std::toupper(u) : std::tolower(u));
        }
        return Value();
    }
    case S_TRIM: {
        size_t b = 0, e = s.size();
        while (b < e && std::isspace((unsigned char)s[b])) ++b;
        while (e > b && std::isspace((unsigned char)s[e - 1])) --e;
        if (b != 0 || e != s.size()) mutStr(*r) = s.substr(b, e - b);
        return Value();
    }
    case S_REPLACE: {
        std::string f = charOrStr(V(0)), t = charOrStr(V(1));
        if (f.empty()) return Value();
        std::string out;
        size_t p = 0;
        while (true) {
            size_t q = s.find(f, p);
            if (q == std::string::npos) { out += s.substr(p); break; }
            out += s.substr(p, q - p);
            out += t;
            p = q + f.size();
        }
        mutStr(*r) = out;
        return Value();
    }
    case S_STARTSWITH: {
        std::string p = toStr(V(0));
        int64_t off = args.size() > 1 ? I(1) : 0;
        if (off < 0 || off > len) return Value::B(false);
        return Value::B(s.compare((size_t)off, p.size(), p) == 0 && (size_t)off + p.size() <= s.size());
    }
    case S_ENDSWITH: {
        std::string p = toStr(V(0));
        return Value::B(p.size() <= s.size() && s.compare(s.size() - p.size(), p.size(), p) == 0);
    }
    case S_EQUALS: return Value::B(s == toStr(V(0)));
    case S_EQUALSIC: {
        std::string p = toStr(V(0));
        if (p.size() != s.size()) return Value::B(false);
        for (size_t k = 0; k < p.size(); ++k)
            if (std::tolower((unsigned char)p[k]) != std::tolower((unsigned char)s[k])) return Value::B(false);
        return Value::B(true);
    }
    case S_CONCAT: {
        std::string add = toStr(V(0));
        mutStr(*r) += add;
        return Value::B(true);
    }
    case S_CSTR: { Value v = *r; v.t = BT::Ptr; return v; }
    case S_REMOVE: {
        int64_t i = I(0);
        int64_t n = args.size() > 1 ? I(1) : len;
        if (i < 0 || i >= len || n <= 0) return Value();
        if (n > len - i) n = len - i;
        mutStr(*r).erase((size_t)i, (size_t)n);
        return Value();
    }
    case S_ISEMPTY: return Value::B(s.empty());
    case S_COMPARETO: {
        std::string p = toStr(V(0));
        int cmp = 0;
        for (size_t k = 0;; ++k) {
            int a = k < s.size() ? (unsigned char)s[k] : 0, b = k < p.size() ? (unsigned char)p[k] : 0;
            if (a != b || a == 0) { cmp = a - b; break; }
        }
        return Value::I(cmp, BT::I16);
    }
    case S_RESERVE: for (auto& a : args) a->eval(c); return Value::B(true);
    case S_GETBYTES: case S_TOCHARARRAY: {
        Value buf = V(0);
        int64_t n = I(1);
        int64_t from = args.size() > 2 ? I(2) : 0;
        if (n <= 0) return Value();
        std::string part = from < len && from >= 0 ? s.substr((size_t)from) : std::string();
        if ((int64_t)part.size() > n - 1) part.resize((size_t)(n - 1));
        storeBytes(buf, part, true, line, id == S_GETBYTES ? "getBytes" : "toCharArray");
        return Value();
    }
    default: rtFail("String 에는 '" + name + "' 메서드가 없습니다", line);
    }
}

// ---------------- EEPROM ----------------
int findEepromMethod(const std::string& name) {
    const MDef* d = findDef(kEep, name);
    return d ? d->id : -1;
}

Value callEeprom(Ctx& c, int id, std::vector<ExprP>& args, int line) {
    checkArgs(kEep, id, args.size(), "EEPROM", line);
    auto addrOf = [&](size_t k) {
        int64_t a = toI(args[k]->eval(c), line);
        if (a < 0 || a >= (int64_t)c.eeprom.size())
            rtFail("EEPROM 주소 범위 초과: " + std::to_string(a) + " (크기 " + std::to_string(c.eeprom.size()) + ")", line);
        return (size_t)a;
    };
    switch (id) {
    case E_READ: return Value::I(c.eeprom[addrOf(0)].i, BT::U8);
    case E_WRITE: case E_UPDATE: {
        size_t a = addrOf(0);
        int64_t v = toI(args[1]->eval(c), line);
        c.eeprom[a].i = (uint8_t)v;
        return Value();
    }
    case E_LENGTH: return Value::I((int64_t)c.eeprom.size(), BT::U16);
    case E_PUT: {
        size_t a = addrOf(0);
        Value v = args[1]->eval(c);
        std::vector<uint8_t> bytes;
        serialize(v, bytes, line);
        if (a + bytes.size() > c.eeprom.size()) rtFail("EEPROM.put: 데이터가 EEPROM 범위를 벗어납니다", line);
        for (size_t k = 0; k < bytes.size(); ++k) c.eeprom[a + k].i = bytes[k];
        return v;
    }
    case E_GET: {
        size_t a = addrOf(0);
        Value* p = args[1]->ref(c);
        std::vector<uint8_t> sz;
        serialize(*p, sz, line);
        if (a + sz.size() > c.eeprom.size()) rtFail("EEPROM.get: 데이터가 EEPROM 범위를 벗어납니다", line);
        std::vector<uint8_t> in(sz.size());
        for (size_t k = 0; k < sz.size(); ++k) in[k] = (uint8_t)c.eeprom[a + k].i;
        size_t pp = 0;
        deserialize(*p, in, pp, line);
        return *p;
    }
    default:
        for (auto& x : args) x->eval(c);
        return Value::B(true);
    }
}

// ---------------- 라이브러리 객체 ----------------
Value callLibrary(Ctx& c, const std::string& cls, int id, const std::string& method,
                  std::vector<ExprP>& args, int line) {
    std::vector<LibValue> la;
    la.reserve(args.size());
    for (auto& a : args) la.push_back(toLib(a->eval(c)));
    LibValue res;
    std::string err;
    if (!c.host->libraryCall(cls, id, method, la, res, err))
        rtFail(err.empty() ? cls + "." + method + "() 호출에 실패했습니다" : err, line);
    c.checkStop();
    return fromLib(res);
}

} // namespace interp
} // namespace ard
