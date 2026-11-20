// InterpLexer.cpp - 전처리기 + 토크나이저
#include "InterpLexer.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace ard {
namespace interp {

namespace {

struct Macro {
    bool fn = false;
    bool variadic = false;
    std::vector<std::string> params;
    std::vector<Token> body;
};
using MacroP = std::shared_ptr<Macro>;
using MacroTable = std::unordered_map<std::string, MacroP>;
struct MacroEvent { size_t pos; std::string name; MacroP def; };

inline bool isIdStart(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
inline bool isDigitC(char c) { return c >= '0' && c <= '9'; }
inline bool isIdChar(char c) { return isIdStart(c) || isDigitC(c); }
inline bool isHexC(char c) { return isDigitC(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }

std::string stripComments(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    size_t n = s.size(), k = 0;
    while (k < n) {
        char c = s[k];
        if (c == '/' && k + 1 < n && s[k + 1] == '/') {
            while (k < n && s[k] != '\n') ++k;
            continue;
        }
        if (c == '/' && k + 1 < n && s[k + 1] == '*') {
            k += 2;
            out += ' ';
            while (k < n && !(s[k] == '*' && k + 1 < n && s[k + 1] == '/')) {
                if (s[k] == '\n') out += '\n';
                ++k;
            }
            k += 2;
            continue;
        }
        if (c == '"' || c == '\'') {
            char q = c;
            out += c;
            ++k;
            while (k < n && s[k] != q && s[k] != '\n') {
                if (s[k] == '\\' && k + 1 < n) { out += s[k]; ++k; }
                if (s[k] != '\r') out += s[k];
                ++k;
            }
            if (k < n && s[k] == q) { out += q; ++k; }
            continue;
        }
        if (c == '\r') { ++k; continue; }
        out += c;
        ++k;
    }
    return out;
}

size_t readEscape(const std::string& s, size_t k, std::string& out) {
    if (k >= s.size()) return k;
    char c = s[k];
    switch (c) {
    case 'n': out += '\n'; return k + 1;
    case 't': out += '\t'; return k + 1;
    case 'r': out += '\r'; return k + 1;
    case 'a': out += '\a'; return k + 1;
    case 'b': out += '\b'; return k + 1;
    case 'f': out += '\f'; return k + 1;
    case 'v': out += '\v'; return k + 1;
    case 'e': out += '\x1b'; return k + 1;
    case 'x': {
        int v = 0, cnt = 0;
        ++k;
        while (k < s.size() && isHexC(s[k]) && cnt < 2) {
            char h = s[k];
            v = v * 16 + (isDigitC(h) ? h - '0' : (h | 0x20) - 'a' + 10);
            ++k; ++cnt;
        }
        out += (char)v;
        return k;
    }
    default:
        if (c >= '0' && c <= '7') {
            int v = 0, cnt = 0;
            while (k < s.size() && s[k] >= '0' && s[k] <= '7' && cnt < 3) { v = v * 8 + (s[k] - '0'); ++k; ++cnt; }
            out += (char)v;
            return k;
        }
        out += c;
        return k + 1;
    }
}

size_t lexNumber(const std::string& s, size_t k, int line, Token& t) {
    size_t n = s.size(), st = k;
    bool isFloat = false;
    int base = 10;
    if (s[k] == '0' && k + 1 < n && (s[k + 1] == 'x' || s[k + 1] == 'X')) {
        base = 16; k += 2;
        while (k < n && isHexC(s[k])) ++k;
    } else if (s[k] == '0' && k + 2 < n && (s[k + 1] == 'b' || s[k + 1] == 'B') && (s[k + 2] == '0' || s[k + 2] == '1')) {
        base = 2; k += 2;
        while (k < n && (s[k] == '0' || s[k] == '1')) ++k;
    } else {
        while (k < n && isDigitC(s[k])) ++k;
        if (k < n && s[k] == '.') { isFloat = true; ++k; while (k < n && isDigitC(s[k])) ++k; }
        if (k < n && (s[k] == 'e' || s[k] == 'E')) {
            size_t m = k + 1;
            if (m < n && (s[m] == '+' || s[m] == '-')) ++m;
            if (m < n && isDigitC(s[m])) { isFloat = true; k = m; while (k < n && isDigitC(s[k])) ++k; }
        }
        if (!isFloat && s[st] == '0' && k - st > 1) base = 8;
    }
    std::string digits = s.substr(st, k - st);
    bool u = false, fsuf = false;
    int l = 0;
    while (k < n) {
        char c = s[k];
        if (c == 'u' || c == 'U') u = true;
        else if (c == 'l' || c == 'L') ++l;
        else if ((c == 'f' || c == 'F') && base == 10) fsuf = true;
        else break;
        ++k;
    }
    if (k < n && (isIdChar(s[k]) || s[k] == '.'))
        throw CompileError{"잘못된 숫자 형식: " + s.substr(st, k - st + 1), line};
    t.line = line;
    t.text = s.substr(st, k - st);
    if (isFloat || fsuf) {
        t.type = TT::Float;
        t.fval = std::strtod(digits.c_str(), nullptr);
        t.nt = BT::F32;
        return k;
    }
    const char* dp = digits.c_str() + ((base == 16 || base == 2) ? 2 : 0);
    if (base == 8) for (char c : digits) if (c > '7') throw CompileError{"잘못된 8진수: " + digits, line};
    uint64_t v = std::strtoull(dp, nullptr, base);
    t.type = TT::Int;
    t.ival = (int64_t)v;
    bool dec = base == 10;
    BT nt;
    if (!u && l == 0)
        nt = v <= 32767 ? BT::I16 : (!dec && v <= 65535) ? BT::U16 : v <= 2147483647ull ? BT::I32
           : (!dec && v <= 4294967295ull) ? BT::U32 : v <= (uint64_t)INT64_MAX ? BT::I64 : BT::U64;
    else if (u && l == 0)
        nt = v <= 65535 ? BT::U16 : v <= 4294967295ull ? BT::U32 : BT::U64;
    else if (!u && l == 1)
        nt = v <= 2147483647ull ? BT::I32 : (!dec && v <= 4294967295ull) ? BT::U32 : BT::I64;
    else if (u && l == 1)
        nt = v <= 4294967295ull ? BT::U32 : BT::U64;
    else
        nt = u ? BT::U64 : BT::I64;
    t.nt = nt;
    return k;
}

const char* const kPuncts[] = {
    ">>=", "<<=", "...", "->", "++", "--", "<<", ">>", "<=", ">=", "==", "!=", "&&", "||",
    "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "::", "##",
    "+", "-", "*", "/", "%", "&", "|", "^", "!", "~", "<", ">", "=", "?", ":", ";", ",", ".",
    "(", ")", "[", "]", "{", "}", "#", nullptr };

void lexText(const std::string& s, int line, std::vector<Token>& out) {
    size_t n = s.size(), k = 0;
    while (k < n) {
        char c = s[k];
        if (c == ' ' || c == '\t' || c == '\f' || c == '\v' || c == '\r' || c == '\n') { ++k; continue; }
        Token t;
        t.line = line;
        if (isIdStart(c)) {
            size_t st = k;
            while (k < n && isIdChar(s[k])) ++k;
            t.type = TT::Ident;
            t.text = s.substr(st, k - st);
            // L"..", u8"..": 접두사 무시
            if (k < n && s[k] == '"' && (t.text == "L" || t.text == "u8" || t.text == "u" || t.text == "U")) continue;
            out.push_back(std::move(t));
            continue;
        }
        if (isDigitC(c) || (c == '.' && k + 1 < n && isDigitC(s[k + 1]))) {
            k = lexNumber(s, k, line, t);
            out.push_back(std::move(t));
            continue;
        }
        if (c == '"') {
            ++k;
            std::string v;
            while (k < n && s[k] != '"') {
                if (s[k] == '\\') k = readEscape(s, k + 1, v);
                else v += s[k++];
            }
            if (k >= n) throw CompileError{"문자열이 닫히지 않았습니다 ('\"' 가 필요합니다)", line};
            ++k;
            t.type = TT::Str;
            t.text = v;
            out.push_back(std::move(t));
            continue;
        }
        if (c == '\'') {
            ++k;
            std::string v;
            while (k < n && s[k] != '\'') {
                if (s[k] == '\\') k = readEscape(s, k + 1, v);
                else v += s[k++];
            }
            if (k >= n) throw CompileError{"문자 상수가 닫히지 않았습니다 (\"'\" 가 필요합니다)", line};
            ++k;
            if (v.empty()) throw CompileError{"빈 문자 상수 '' 는 사용할 수 없습니다", line};
            t.type = TT::Char;
            int64_t val = 0;
            if (v.size() == 1) val = (int8_t)v[0];
            else for (char ch : v) val = (val << 8) | (uint8_t)ch;   // 다중 문자 상수
            t.ival = val;
            t.nt = v.size() == 1 ? BT::I8 : BT::I16;
            t.text = v;
            out.push_back(std::move(t));
            continue;
        }
        bool found = false;
        for (int p = 0; kPuncts[p]; ++p) {
            size_t len = std::strlen(kPuncts[p]);
            if (s.compare(k, len, kPuncts[p]) == 0) {
                t.type = TT::Punct;
                t.text = kPuncts[p];
                k += len;
                found = true;
                break;
            }
        }
        if (!found) {
            std::string bad;
            if ((unsigned char)c >= 0x80) {
                size_t st = k;
                ++k;
                while (k < n && ((unsigned char)s[k] & 0xC0) == 0x80) ++k;
                bad = s.substr(st, k - st);
            } else bad = std::string(1, c);
            throw CompileError{"알 수 없는 문자: '" + bad + "' (따옴표 밖의 한글/특수문자는 사용할 수 없습니다)", line};
        }
        out.push_back(std::move(t));
    }
}

std::string joinTokens(const std::vector<Token>& v, bool spaces) {
    std::string r;
    for (size_t k = 0; k < v.size(); ++k) {
        if (spaces && k) r += ' ';
        if (v[k].type == TT::Str) r += "\"" + v[k].text + "\"";
        else r += v[k].text;
    }
    return r;
}

class Expander {
public:
    explicit Expander(MacroTable& tab) : tab_(tab) {}
    void expandList(const std::vector<Token>& src, std::vector<Token>& out) {
        for (size_t i = 0; i < src.size();) i = expandAt(src, i, out);
    }
    size_t expandAt(const std::vector<Token>& src, size_t i, std::vector<Token>& out) {
        const Token& t = src[i];
        if (t.type != TT::Ident) { out.push_back(t); return i + 1; }
        auto it = tab_.find(t.text);
        if (it == tab_.end() || !it->second ||
            std::find(disabled_.begin(), disabled_.end(), t.text) != disabled_.end()) {
            out.push_back(t);
            return i + 1;
        }
        MacroP m = it->second;
        if (disabled_.size() > 100) throw CompileError{"매크로 확장이 너무 깊습니다: " + t.text, t.line};
        std::vector<Token> repl;
        size_t next = i + 1;
        if (!m->fn) {
            repl = m->body;
        } else {
            if (next >= src.size() || !(src[next].type == TT::Punct && src[next].text == "(")) {
                out.push_back(t);
                return i + 1;
            }
            std::vector<std::vector<Token>> args(1);
            int depth = 0;
            size_t j = next + 1;
            for (; j < src.size(); ++j) {
                const Token& a = src[j];
                if (a.type == TT::Punct) {
                    if (a.text == "(") ++depth;
                    else if (a.text == ")") { if (depth == 0) break; --depth; }
                    else if (a.text == "," && depth == 0 &&
                             !(m->variadic && args.size() >= m->params.size())) {
                        args.emplace_back();
                        continue;
                    }
                }
                args.back().push_back(a);
            }
            if (j >= src.size()) throw CompileError{"매크로 '" + t.text + "' 호출에 ')' 가 필요합니다", t.line};
            next = j + 1;
            if (m->params.empty() && args.size() == 1 && args[0].empty()) args.clear();
            if (m->variadic && args.size() + 1 == m->params.size()) args.emplace_back();
            if (args.size() != m->params.size())
                throw CompileError{"매크로 '" + t.text + "' 의 인자 개수가 맞지 않습니다 (필요 " +
                                   std::to_string(m->params.size()) + "개, 전달 " + std::to_string(args.size()) + "개)", t.line};
            auto paramIndex = [&](const Token& tk) -> int {
                if (tk.type != TT::Ident) return -1;
                for (size_t q = 0; q < m->params.size(); ++q) if (m->params[q] == tk.text) return (int)q;
                return -1;
            };
            const std::vector<Token>& body = m->body;
            for (size_t b = 0; b < body.size(); ++b) {
                const Token& bt = body[b];
                if (bt.type == TT::Punct && bt.text == "#" && b + 1 < body.size()) {
                    int pi = paramIndex(body[b + 1]);
                    if (pi >= 0) {
                        Token s; s.type = TT::Str; s.line = t.line; s.text = joinTokens(args[pi], true);
                        repl.push_back(s);
                        ++b;
                        continue;
                    }
                }
                if (bt.type == TT::Punct && bt.text == "##" && !repl.empty() && b + 1 < body.size()) {
                    const Token& rhs = body[++b];
                    int pi = paramIndex(rhs);
                    std::string rtext = pi >= 0 ? joinTokens(args[pi], false) : rhs.text;
                    std::string merged = repl.back().text + rtext;
                    repl.pop_back();
                    lexText(merged, t.line, repl);
                    continue;
                }
                int pi = paramIndex(bt);
                if (pi >= 0) {
                    std::vector<Token> ea;
                    expandList(args[pi], ea);
                    repl.insert(repl.end(), ea.begin(), ea.end());
                } else repl.push_back(bt);
            }
        }
        for (Token& r : repl) r.line = t.line;
        disabled_.push_back(t.text);
        expandList(repl, out);
        disabled_.pop_back();
        return next;
    }
private:
    MacroTable& tab_;
    std::vector<std::string> disabled_;
};

// #if 식 계산기
class IfEval {
public:
    IfEval(const std::vector<Token>& t, int line) : t_(t), line_(line) {}
    int64_t run() {
        int64_t v = cond();
        if (p_ < t_.size()) fail();
        return v;
    }
private:
    const std::vector<Token>& t_;
    size_t p_ = 0;
    int line_;
    [[noreturn]] void fail() { throw CompileError{"#if 조건식을 해석할 수 없습니다", line_}; }
    bool isP(const char* s) const { return p_ < t_.size() && t_[p_].type == TT::Punct && t_[p_].text == s; }
    static int prec(const std::string& op) {
        static const char* const ops[] = { "||", "&&", "|", "^", "&", "==", "!=", "<", ">", "<=", ">=",
                                           "<<", ">>", "+", "-", "*", "/", "%" };
        static const int pr[] = { 1, 2, 3, 4, 5, 6, 6, 7, 7, 7, 7, 8, 8, 9, 9, 10, 10, 10 };
        for (int k = 0; k < 18; ++k) if (op == ops[k]) return pr[k];
        return 0;
    }
    int64_t cond() {
        int64_t v = expr(1);
        if (isP("?")) {
            ++p_;
            int64_t a = cond();
            if (!isP(":")) fail();
            ++p_;
            int64_t b = cond();
            return v ? a : b;
        }
        return v;
    }
    int64_t expr(int minPrec) {
        int64_t lhs = unary();
        while (p_ < t_.size() && t_[p_].type == TT::Punct) {
            const std::string op = t_[p_].text;
            int pr = prec(op);
            if (pr == 0 || pr < minPrec) break;
            ++p_;
            int64_t r = expr(pr + 1);
            if (op == "||") lhs = (lhs || r); else if (op == "&&") lhs = (lhs && r);
            else if (op == "|") lhs |= r; else if (op == "^") lhs ^= r; else if (op == "&") lhs &= r;
            else if (op == "==") lhs = lhs == r; else if (op == "!=") lhs = lhs != r;
            else if (op == "<") lhs = lhs < r; else if (op == ">") lhs = lhs > r;
            else if (op == "<=") lhs = lhs <= r; else if (op == ">=") lhs = lhs >= r;
            else if (op == "<<") lhs = (r >= 0 && r < 63) ? lhs << r : 0;
            else if (op == ">>") lhs = (r >= 0 && r < 63) ? lhs >> r : 0;
            else if (op == "+") lhs += r; else if (op == "-") lhs -= r; else if (op == "*") lhs *= r;
            else if (op == "/") lhs = r ? lhs / r : 0; else if (op == "%") lhs = r ? lhs % r : 0;
        }
        return lhs;
    }
    int64_t unary() {
        if (p_ >= t_.size()) fail();
        const Token& tk = t_[p_];
        if (tk.type == TT::Punct) {
            ++p_;
            if (tk.text == "!") return !unary();
            if (tk.text == "-") return -unary();
            if (tk.text == "+") return unary();
            if (tk.text == "~") return ~unary();
            if (tk.text == "(") {
                int64_t v = cond();
                if (!isP(")")) fail();
                ++p_;
                return v;
            }
            fail();
        }
        ++p_;
        if (tk.type == TT::Int || tk.type == TT::Char) return tk.ival;
        if (tk.type == TT::Float) return (int64_t)tk.fval;
        if (tk.type == TT::Ident) return tk.text == "true" ? 1 : 0;
        fail();
    }
};

bool evalIf(const std::string& text, MacroTable& defs, int line) {
    std::vector<Token> toks, t2, t3;
    lexText(text, line, toks);
    for (size_t k = 0; k < toks.size(); ++k) {
        if (toks[k].type == TT::Ident && toks[k].text == "defined") {
            size_t q = k + 1;
            bool paren = q < toks.size() && toks[q].type == TT::Punct && toks[q].text == "(";
            if (paren) ++q;
            if (q >= toks.size() || toks[q].type != TT::Ident) throw CompileError{"defined 뒤에 이름이 필요합니다", line};
            Token r; r.type = TT::Int; r.line = line;
            auto it = defs.find(toks[q].text);
            r.ival = (it != defs.end() && it->second) ? 1 : 0;
            t2.push_back(r);
            k = q + (paren ? 1 : 0);
            continue;
        }
        t2.push_back(toks[k]);
    }
    Expander ex(defs);
    ex.expandList(t2, t3);
    if (t3.empty()) throw CompileError{"#if 뒤에 조건식이 필요합니다", line};
    return IfEval(t3, line).run() != 0;
}

void parseDefine(const std::string& rest, int line, std::string& name, Macro& m) {
    size_t k = 0, n = rest.size();
    while (k < n && (rest[k] == ' ' || rest[k] == '\t')) ++k;
    if (k >= n || !isIdStart(rest[k])) throw CompileError{"#define 뒤에 매크로 이름이 필요합니다", line};
    size_t st = k;
    while (k < n && isIdChar(rest[k])) ++k;
    name = rest.substr(st, k - st);
    if (k < n && rest[k] == '(') {
        m.fn = true;
        ++k;
        while (true) {
            while (k < n && (rest[k] == ' ' || rest[k] == '\t')) ++k;
            if (k < n && rest[k] == ')') { ++k; break; }
            if (rest.compare(k, 3, "...") == 0) {
                m.variadic = true;
                m.params.push_back("__VA_ARGS__");
                k += 3;
            } else if (k < n && isIdStart(rest[k])) {
                size_t ps = k;
                while (k < n && isIdChar(rest[k])) ++k;
                m.params.push_back(rest.substr(ps, k - ps));
            } else throw CompileError{"#define 매개변수 목록이 잘못되었습니다", line};
            while (k < n && (rest[k] == ' ' || rest[k] == '\t')) ++k;
            if (k < n && rest[k] == ',') { ++k; continue; }
            if (k < n && rest[k] == ')') { ++k; break; }
            throw CompileError{"#define 매개변수 목록에 ')' 가 필요합니다", line};
        }
    }
    lexText(rest.substr(k), line, m.body);
}

void addPredef(MacroTable& t, const char* name, const char* body) {
    auto m = std::make_shared<Macro>();
    if (*body) lexText(body, 0, m->body);
    t[name] = m;
}

} // namespace

std::vector<Token> preprocess(const std::string& srcIn) {
    std::string src = srcIn;
    if (src.size() >= 3 && (unsigned char)src[0] == 0xEF && (unsigned char)src[1] == 0xBB && (unsigned char)src[2] == 0xBF)
        src.erase(0, 3);
    src = stripComments(src);
    std::vector<std::string> lines;
    {
        size_t st = 0;
        while (true) {
            size_t e = src.find('\n', st);
            if (e == std::string::npos) { lines.push_back(src.substr(st)); break; }
            lines.push_back(src.substr(st, e - st));
            st = e + 1;
        }
    }
    MacroTable defs;
    addPredef(defs, "ARDUINO", "10819");
    addPredef(defs, "ARDUINO_AVR_UNO", "");
    addPredef(defs, "ARDUINO_ARCH_AVR", "");
    addPredef(defs, "__AVR__", "");
    addPredef(defs, "__AVR_ATmega328P__", "");
    addPredef(defs, "F_CPU", "16000000UL");
    {
        const char* fnMacros[] = {"_BV(bit) (1 << (bit))", "bitToggle(value, bit) ((value) ^= (1UL << (bit)))",
                                  "clockCyclesPerMicrosecond() 16"};
        for (const char* fm : fnMacros) {
            auto m = std::make_shared<Macro>();
            std::string name;
            parseDefine(fm, 0, name, *m);
            defs[name] = m;
        }
    }
    MacroTable initial = defs;

    std::vector<Token> raw;
    std::vector<MacroEvent> events;
    struct Cond { bool parentActive, active, taken; int line; };
    std::vector<Cond> conds;
    bool active = true;
    for (size_t li = 0; li < lines.size(); ++li) {
        int lineNo = (int)li + 1;
        std::string text = lines[li];
        while (!text.empty() && text.back() == '\\' && li + 1 < lines.size()) {
            text.pop_back();
            text += ' ';
            text += lines[++li];
        }
        size_t p = text.find_first_not_of(" \t\f\v");
        if (p != std::string::npos && text[p] == '#') {
            size_t q = p + 1;
            while (q < text.size() && (text[q] == ' ' || text[q] == '\t')) ++q;
            size_t ws = q;
            while (q < text.size() && isIdChar(text[q])) ++q;
            std::string dir = text.substr(ws, q - ws);
            std::string rest = text.substr(q);
            auto firstIdent = [&]() {
                std::vector<Token> tk;
                lexText(rest, lineNo, tk);
                if (tk.empty() || tk[0].type != TT::Ident) throw CompileError{"#" + dir + " 뒤에 이름이 필요합니다", lineNo};
                return tk[0].text;
            };
            if (dir == "ifdef" || dir == "ifndef") {
                bool cond = false;
                if (active) {
                    auto it = defs.find(firstIdent());
                    bool def = it != defs.end() && it->second;
                    cond = dir == "ifdef" ? def : !def;
                }
                conds.push_back({active, active && cond, cond || !active, lineNo});
                active = active && cond;
            } else if (dir == "if") {
                bool cond = active ? evalIf(rest, defs, lineNo) : false;
                conds.push_back({active, active && cond, cond || !active, lineNo});
                active = active && cond;
            } else if (dir == "elif") {
                if (conds.empty()) throw CompileError{"#elif 에 대응하는 #if 가 없습니다", lineNo};
                Cond& c = conds.back();
                if (c.taken) c.active = false;
                else {
                    bool cond = c.parentActive && evalIf(rest, defs, lineNo);
                    c.active = cond;
                    c.taken = cond;
                }
                active = c.active;
            } else if (dir == "else") {
                if (conds.empty()) throw CompileError{"#else 에 대응하는 #if 가 없습니다", lineNo};
                Cond& c = conds.back();
                c.active = c.parentActive && !c.taken;
                c.taken = true;
                active = c.active;
            } else if (dir == "endif") {
                if (conds.empty()) throw CompileError{"#endif 에 대응하는 #if 가 없습니다", lineNo};
                conds.pop_back();
                active = conds.empty() ? true : conds.back().active;
            } else if (!active) {
                // 비활성 구간
            } else if (dir == "define") {
                auto m = std::make_shared<Macro>();
                std::string name;
                parseDefine(rest, lineNo, name, *m);
                defs[name] = m;
                events.push_back({raw.size(), name, m});
            } else if (dir == "undef") {
                std::string name = firstIdent();
                defs.erase(name);
                events.push_back({raw.size(), name, nullptr});
            } else if (dir == "error") {
                throw CompileError{"#error" + rest, lineNo};
            }
            // include, pragma, warning, line 등은 무시
            continue;
        }
        if (active) lexText(text, lineNo, raw);
    }
    if (!conds.empty()) throw CompileError{"#if/#ifdef 에 대응하는 #endif 가 없습니다", conds.back().line};

    std::vector<Token> out;
    out.reserve(raw.size() + 16);
    MacroTable table = initial;
    Expander ex(table);
    size_t ei = 0;
    for (size_t i = 0; i < raw.size();) {
        while (ei < events.size() && events[ei].pos <= i) {
            if (events[ei].def) table[events[ei].name] = events[ei].def;
            else table.erase(events[ei].name);
            ++ei;
        }
        i = ex.expandAt(raw, i, out);
    }
    Token end;
    end.type = TT::End;
    end.line = (int)lines.size();
    out.push_back(end);
    return out;
}

} // namespace interp
} // namespace ard
