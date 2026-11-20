// InterpParseExpr.cpp - 식 파싱 및 이름 해석
#include "InterpParser.h"
#include <cmath>

namespace ard {
namespace interp {

ExprP Parser::makeLit(const Value& v, int line) {
    auto e = std::make_unique<LitExpr>();
    e->v = v;
    e->line = line;
    return e;
}

bool Parser::constEval(Expr* e, Value& out) {
    if (!e || !e->isConst()) return false;
    Ctx dummy;
    dummy.host = host;
    dummy.prog = &P;
    try {
        out = e->eval(dummy);
    } catch (RtError& er) {
        fail(er.msg, er.line ? er.line : e->line);
    }
    return true;
}

void Parser::checkAssignable(Expr* e, int line) {
    if (auto l = dynamic_cast<LitExpr*>(e))
        if (!l->constName.empty()) fail("const 변수 '" + l->constName + "' 에는 값을 대입할 수 없습니다", line);
    if (auto v = dynamic_cast<LocalVar*>(e))
        if (v->isConstVar) fail("const 변수 '" + v->nm + "' 에는 값을 대입할 수 없습니다", line);
    if (auto v = dynamic_cast<GlobalVar*>(e))
        if (v->isConstVar) fail("const 변수 '" + v->nm + "' 에는 값을 대입할 수 없습니다", line);
    if (!e->lvalue) fail("대입/증감 연산자의 대상은 변수여야 합니다", line);
}

ExprP Parser::makeBuiltinCall(const std::string& nm, std::vector<ExprP> args, int line) {
    int mn = 0, mx = 0;
    int id = findBuiltin(nm, mn, mx);
    if (id < 0) fail("정의되지 않은 함수: " + nm, line);
    int n = (int)args.size();
    if (n < mn || (mx >= 0 && n > mx)) fail("함수 '" + nm + "' 의 인자 개수가 맞지 않습니다", line);
    auto ce = std::make_unique<CallExpr>();
    ce->line = line;
    ce->nm = nm;
    ce->builtin = id;
    ce->args = std::move(args);
    return ce;
}

static std::string nameOf(Expr* e) {
    if (auto v = dynamic_cast<LocalVar*>(e)) return v->nm;
    if (auto v = dynamic_cast<GlobalVar*>(e)) return v->nm;
    if (auto v = dynamic_cast<IndexExpr*>(e)) return v->nm;
    if (auto v = dynamic_cast<FieldExpr*>(e)) return v->field;
    return std::string();
}

std::vector<ExprP> Parser::parseArgs() {
    expect("(");
    std::vector<ExprP> a;
    if (accept(")")) return a;
    while (true) {
        a.push_back(parseAssign());
        if (accept(",")) continue;
        expect(")");
        break;
    }
    return a;
}

ExprP Parser::parseExpr() {
    ExprP e = parseAssign();
    while (isP(",")) {
        int line = cur().line;
        ++pos;
        auto c = std::make_unique<CommaExpr>();
        c->line = line;
        c->a = std::move(e);
        c->b = parseAssign();
        e = std::move(c);
    }
    return e;
}

ExprP Parser::parseAssign() {
    ExprP lhs = parseTernary();
    static const char* const ops[] = {"=", "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "<<=", ">>="};
    static const int opc[] = {-1, OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_MOD, OP_AND, OP_OR, OP_XOR, OP_SHL, OP_SHR};
    for (int k = 0; k < 11; ++k) {
        if (!isP(ops[k])) continue;
        int line = cur().line;
        ++pos;
        checkAssignable(lhs.get(), line);
        ExprP rhs = parseAssign();
        if (k == 0) {
            auto a = std::make_unique<AssignExpr>();
            a->line = line;
            a->target = std::move(lhs);
            a->value = std::move(rhs);
            return a;
        }
        auto a = std::make_unique<CompoundExpr>();
        a->line = line;
        a->op = opc[k];
        a->target = std::move(lhs);
        a->value = std::move(rhs);
        return a;
    }
    return lhs;
}

ExprP Parser::parseTernary() {
    ExprP c = parseBinary(1);
    if (!isP("?")) return c;
    int line = cur().line;
    ++pos;
    auto t = std::make_unique<TernExpr>();
    t->line = line;
    t->cond = std::move(c);
    t->a = parseAssign();
    expect(":");
    t->b = parseAssign();
    return t;
}

static int binPrec(const Token& t, int& op) {
    if (t.type != TT::Punct) return 0;
    const std::string& s = t.text;
    if (s.size() > 2) return 0;
    switch (s[0]) {
    case '|': if (s == "||") { op = -1; return 1; } if (s == "|") { op = OP_OR; return 3; } return 0;
    case '&': if (s == "&&") { op = -2; return 2; } if (s == "&") { op = OP_AND; return 5; } return 0;
    case '^': if (s == "^") { op = OP_XOR; return 4; } return 0;
    case '=': if (s == "==") { op = OP_EQ; return 6; } return 0;
    case '!': if (s == "!=") { op = OP_NE; return 6; } return 0;
    case '<':
        if (s == "<") { op = OP_LT; return 7; }
        if (s == "<=") { op = OP_LE; return 7; }
        if (s == "<<") { op = OP_SHL; return 8; }
        return 0;
    case '>':
        if (s == ">") { op = OP_GT; return 7; }
        if (s == ">=") { op = OP_GE; return 7; }
        if (s == ">>") { op = OP_SHR; return 8; }
        return 0;
    case '+': if (s == "+") { op = OP_ADD; return 9; } return 0;
    case '-': if (s == "-") { op = OP_SUB; return 9; } return 0;
    case '*': if (s == "*") { op = OP_MUL; return 10; } return 0;
    case '/': if (s == "/") { op = OP_DIV; return 10; } return 0;
    case '%': if (s == "%") { op = OP_MOD; return 10; } return 0;
    default: return 0;
    }
}

ExprP Parser::parseBinary(int minPrec) {
    ExprP lhs = parseUnary();
    while (true) {
        int op = 0;
        int pr = binPrec(cur(), op);
        if (pr == 0 || pr < minPrec) break;
        int line = cur().line;
        ++pos;
        ExprP rhs = parseBinary(pr + 1);
        ExprP node;
        if (pr <= 2) {
            auto l = std::make_unique<LogicExpr>();
            l->isAnd = (pr == 2);
            l->a = std::move(lhs);
            l->b = std::move(rhs);
            node = std::move(l);
        } else {
            auto b = std::make_unique<BinExpr>();
            b->op = op;
            b->a = std::move(lhs);
            b->b = std::move(rhs);
            node = std::move(b);
        }
        node->line = line;
        Value v;
        if (node->isConst() && constEval(node.get(), v)) node = makeLit(v, line);
        lhs = std::move(node);
    }
    return lhs;
}

ExprP Parser::parseUnary() {
    NestGuard guard(*this);
    const Token& t = cur();
    int line = t.line;
    if (t.type == TT::Punct) {
        const std::string p = t.text;
        if (p == "!" || p == "~" || p == "-" || p == "+") {
            ++pos;
            auto u = std::make_unique<UnExpr>();
            u->line = line;
            u->op = p == "!" ? U_NOT : p == "~" ? U_BITNOT : p == "-" ? U_NEG : U_PLUS;
            u->a = parseUnary();
            Value v;
            if (u->isConst() && constEval(u.get(), v)) return makeLit(v, line);
            return u;
        }
        if (p == "++" || p == "--") {
            ++pos;
            auto e = std::make_unique<IncDecExpr>();
            e->line = line;
            e->pre = true;
            e->delta = p == "++" ? 1 : -1;
            e->target = parseUnary();
            checkAssignable(e->target.get(), line);
            return e;
        }
        if (p == "&") {
            ++pos;
            auto e = std::make_unique<AddrExpr>();
            e->line = line;
            e->a = parseUnary();
            return e;
        }
        if (p == "*") {
            ++pos;
            auto e = std::make_unique<DerefExpr>();
            e->line = line;
            e->a = parseUnary();
            return e;
        }
        if (p == "(" && isTypeName(pos + 1)) {
            size_t save = pos;
            ++pos;
            TypeInfo ti;
            bool st;
            if (parseType(ti, st)) {
                bool ptr = false;
                while (accept("*")) { ptr = true; while (isId("const")) ++pos; }
                if (accept(")")) {
                    if (ptr) { ti.bt = BT::Ptr; ti.isConst = false; }
                    auto c = std::make_unique<CastExpr>();
                    c->line = line;
                    c->type = ti;
                    c->a = parseUnary();
                    Value v;
                    if (isIntBT(ti.bt) || isFloatBT(ti.bt))
                        if (c->isConst() && constEval(c.get(), v)) return makeLit(v, line);
                    return c;
                }
            }
            pos = save;
        }
    }
    if (t.type == TT::Ident && t.text == "sizeof") {
        ++pos;
        if (isP("(")) {
            size_t save = pos;
            ++pos;
            TypeInfo ti;
            bool st;
            if (isTypeName(pos) && parseType(ti, st)) {
                bool ptr = false;
                while (accept("*")) ptr = true;
                if (accept(")")) return makeLit(Value::I(ptr ? 2 : sizeOfType(ti, P), BT::U16), line);
            }
            pos = save;
        }
        {
            size_t save = pos;
            bool paren = accept("(");
            if (cur().type == TT::Ident) {
                const VarInfo* vi = findVar(cur().text);
                bool simple = paren ? isPAt(pos + 1, ")") : !(isPAt(pos + 1, "[") || isPAt(pos + 1, ".") || isPAt(pos + 1, "->"));
                if (vi && simple && (vi->isPtrParam || (vi->type.bt == BT::Ptr && !vi->isArray))) {
                    ++pos;
                    if (paren) expect(")");
                    return makeLit(Value::I(2, BT::U16), line);
                }
            }
            pos = save;
        }
        auto s = std::make_unique<SizeofExpr>();
        s->line = line;
        s->a = parseUnary();
        return s;
    }
    return parsePostfix(parsePrimary());
}

ExprP Parser::parsePostfix(ExprP e) {
    while (true) {
        int line = cur().line;
        if (isP("[")) {
            ++pos;
            auto ie = std::make_unique<IndexExpr>();
            ie->line = line;
            ie->nm = nameOf(e.get());
            ie->baseByValue = !e->lvalue || dynamic_cast<IndexExpr*>(e.get()) != nullptr;
            ie->base = std::move(e);
            ie->idx = parseExpr();
            expect("]");
            e = std::move(ie);
            continue;
        }
        if (isP(".") || isP("->")) {
            bool arrow = isP("->");
            ++pos;
            std::string m = expectIdent("멤버 이름");
            if (arrow) {
                auto d = std::make_unique<DerefExpr>();
                d->line = line;
                d->a = std::move(e);
                e = std::move(d);
            }
            if (isP("(")) {
                auto me = std::make_unique<MethodExpr>();
                me->line = line;
                me->method = m;
                me->strMethod = findStringMethod(m);
                me->recv = std::move(e);
                me->args = parseArgs();
                e = std::move(me);
            } else {
                auto fe = std::make_unique<FieldExpr>();
                fe->line = line;
                fe->field = m;
                fe->base = std::move(e);
                e = std::move(fe);
            }
            continue;
        }
        if (isP("++") || isP("--")) {
            checkAssignable(e.get(), line);
            auto ie = std::make_unique<IncDecExpr>();
            ie->line = line;
            ie->pre = false;
            ie->delta = isP("++") ? 1 : -1;
            ++pos;
            ie->target = std::move(e);
            e = std::move(ie);
            continue;
        }
        if (isP("(")) failHere("함수 포인터 호출 등 이 형태의 호출은 지원하지 않습니다");
        break;
    }
    return e;
}

ExprP Parser::parsePrimary() {
    const Token t = cur();
    int line = t.line;
    switch (t.type) {
    case TT::Int:
        ++pos;
        return makeLit(Value::I(t.ival, t.nt), line);
    case TT::Float:
        ++pos;
        return makeLit(Value::F((double)(float)t.fval, BT::F32), line);
    case TT::Char:
        ++pos;
        return makeLit(Value::I(t.ival, t.nt), line);
    case TT::Str: {
        std::string s;
        while (cur().type == TT::Str) { s += cur().text; ++pos; }
        return makeLit(Value::S(s, BT::Ptr), line);
    }
    case TT::Punct:
        if (t.text == "(") {
            ++pos;
            ExprP e = parseExpr();
            expect(")");
            return e;
        }
        if (t.text == "{") failHere("여기에는 { } 초기값 목록을 사용할 수 없습니다");
        failHere("식이 필요합니다 (" + tokDesc(t) + " 앞)");
    case TT::Ident:
        return parseIdentifier();
    default:
        failHere("식이 필요합니다 (" + tokDesc(t) + " 앞)");
    }
}

ExprP Parser::parseIdentifier() {
    const std::string name = cur().text;
    int line = cur().line;
    bool isVar = findVar(name) != nullptr;
    if (!isVar) {
        if (name == "true" || name == "false") { ++pos; return makeLit(Value::B(name == "true"), line); }
        if (name == "NULL" || name == "nullptr") { ++pos; return makeLit(Value::I(0, BT::I16), line); }
        if ((name == "F" || name == "PSTR" || name == "FPSTR") && isPAt(pos + 1, "(")) {
            ++pos;
            auto args = parseArgs();
            if (args.size() != 1) fail(name + "() 에는 인자가 1개 필요합니다", line);
            return std::move(args[0]);
        }
        if (name == "Serial") {
            ++pos;
            if (!isP(".")) return makeLit(Value::B(true), line);
            ++pos;
            std::string m = expectIdent("Serial 함수 이름");
            int mn = 0, mx = 0;
            int id = findSerialMethod(m, mn, mx);
            if (id < 0) fail("Serial 에는 '" + m + "' 함수가 없습니다", line);
            if (!isP("(")) fail("'(' 가 필요합니다 (Serial." + m + ")", line);
            auto se = std::make_unique<SerialExpr>();
            se->line = line;
            se->method = id;
            se->args = parseArgs();
            int n = (int)se->args.size();
            if (n < mn || (mx >= 0 && n > mx)) fail("Serial." + m + "() 의 인자 개수가 맞지 않습니다", line);
            return se;
        }
        if (name == "EEPROM") {
            ++pos;
            if (accept("[")) {
                auto ei = std::make_unique<EepromIndexExpr>();
                ei->line = line;
                ei->idx = parseExpr();
                expect("]");
                return ei;
            }
            expect(".");
            std::string m = expectIdent("EEPROM 함수 이름");
            int id = findEepromMethod(m);
            if (id < 0) fail("EEPROM 에는 '" + m + "' 함수가 없습니다", line);
            auto ee = std::make_unique<EepromExpr>();
            ee->line = line;
            ee->method = id;
            ee->args = parseArgs();
            return ee;
        }
        if (isPAt(pos + 1, "(")) {
            // String(...) 생성자, 함수형 캐스트 int(x), byte(x) ...
            if (name == "String") { ++pos; return makeBuiltinCall("String", parseArgs(), line); }
            BT b = BT::Void;
            bool cast = false;
            if (name == "int" || name == "short") { b = BT::I16; cast = true; }
            else if (name == "long") { b = BT::I32; cast = true; }
            else if (name == "char") { b = BT::I8; cast = true; }
            else if (name == "unsigned") { b = BT::U16; cast = true; }
            else if (mapNamedType(name, b) && b != BT::Void && b != BT::Str) cast = true;
            else if (typedefs.count(name) && (isIntBT(typedefs[name].bt) || isFloatBT(typedefs[name].bt))) { b = typedefs[name].bt; cast = true; }
            if (cast) {
                ++pos;
                auto args = parseArgs();
                if (name == "word" && args.size() == 2) return makeBuiltinCall("makeWord", std::move(args), line);
                if (args.size() != 1) fail(name + "() 변환에는 인자가 1개 필요합니다", line);
                auto c = std::make_unique<CastExpr>();
                c->line = line;
                c->type.bt = b;
                c->a = std::move(args[0]);
                Value v;
                if (c->isConst() && constEval(c.get(), v)) return makeLit(v, line);
                return c;
            }
        }
        if (host && host->isGlobalObject(name)) {
            ++pos;
            if (accept(".")) {
                std::string m = expectIdent("함수 이름");
                if (!isP("(")) fail("'(' 가 필요합니다 (" + name + "." + m + ")", line);
                auto g = std::make_unique<GlobalObjExpr>();
                g->line = line;
                g->obj = name;
                g->method = m;
                g->args = parseArgs();
                return g;
            }
            auto ov = std::make_unique<ObjValueExpr>();
            ov->line = line;
            ov->v.k = VK::Obj;
            ov->v.t = BT::Obj;
            auto o = std::make_shared<LibObj>();
            o->cls = name;
            o->id = -1;
            ov->v.h = o;
            return ov;
        }
        if (isPAt(pos + 1, "::")) {
            pos += 2;
            std::string m = expectIdent("이름");
            std::string q = name + "::" + m;
            auto it = enumConsts.find(q);
            if (it != enumConsts.end()) return makeLit(Value::I(it->second, BT::I16), line);
            double dv = 0;
            if (host && host->lookupConstant(q, dv)) {
                if (dv == std::floor(dv) && std::fabs(dv) < 2147483648.0)
                    return makeLit(Value::I((int64_t)dv, std::fabs(dv) < 32768 ? BT::I16 : BT::I32), line);
                return makeLit(Value::F(dv), line);
            }
            fail("정의되지 않은 식별자: " + q, line);
        }
    }
    ++pos;
    if (isP("(") && !isVar) {
        if (isLibClass(name)) fail("라이브러리 객체의 임시 생성(" + name + "(...))은 지원하지 않습니다. 변수로 선언하세요", line);
        auto ce = std::make_unique<CallExpr>();
        ce->line = line;
        ce->nm = name;
        ce->args = parseArgs();
        pendingCalls.push_back(ce.get());
        return ce;
    }
    return resolveIdent(name, line);
}

namespace {
struct BConst { const char* name; double v; BT t; };
const BConst kBuiltinConsts[] = {
    {"HIGH", 1, BT::I16}, {"LOW", 0, BT::I16}, {"INPUT", 0, BT::I16}, {"OUTPUT", 1, BT::I16},
    {"INPUT_PULLUP", 2, BT::I16}, {"CHANGE", 1, BT::I16}, {"FALLING", 2, BT::I16}, {"RISING", 3, BT::I16},
    {"LSBFIRST", 0, BT::I16}, {"MSBFIRST", 1, BT::I16}, {"DEC", 10, BT::I16}, {"HEX", 16, BT::I16},
    {"OCT", 8, BT::I16}, {"BIN", 2, BT::I16}, {"INT_MAX", 32767, BT::I16}, {"INT_MIN", -32768, BT::I16},
    {"UINT_MAX", 65535, BT::U16}, {"LONG_MAX", 2147483647.0, BT::I32}, {"LONG_MIN", -2147483648.0, BT::I32},
    {"ULONG_MAX", 4294967295.0, BT::U32}, {"RAND_MAX", 32767, BT::I16}, {"RANDOM_MAX", 2147483647.0, BT::I32},
    {"CHAR_BIT", 8, BT::I16}, {"CHAR_MAX", 127, BT::I16}, {"CHAR_MIN", -128, BT::I16}, {"UCHAR_MAX", 255, BT::I16},
    {"SERIAL_8N1", 6, BT::I16}, {"DEFAULT", 1, BT::I16}, {"INTERNAL", 3, BT::I16}, {"EXTERNAL", 0, BT::I16},
    {"INTERNAL1V1", 2, BT::I16}, {"NOT_AN_INTERRUPT", -1, BT::I16},
    {"PI", 3.141592653589793, BT::F32}, {"HALF_PI", 1.5707963267948966, BT::F32},
    {"TWO_PI", 6.283185307179586, BT::F32}, {"DEG_TO_RAD", 0.017453292519943295, BT::F32},
    {"RAD_TO_DEG", 57.29577951308232, BT::F32}, {"EULER", 2.718281828459045, BT::F32},
    {"M_PI", 3.141592653589793, BT::F32}, {"M_PI_2", 1.5707963267948966, BT::F32}, {"M_E", 2.718281828459045, BT::F32},
    {"FLT_MAX", 3.4028235e38, BT::F32}, {"FLT_MIN", 1.17549435e-38, BT::F32},
    {nullptr, 0, BT::Void}};
}

ExprP Parser::resolveIdent(const std::string& name, int line) {
    if (const VarInfo* vi = findVar(name)) {
        if (vi->hasConst) {
            auto l = std::make_unique<LitExpr>();
            l->v = vi->constVal;
            l->line = line;
            l->constName = name;
            return l;
        }
        bool cst = vi->type.isConst && !vi->isRef && vi->type.bt != BT::Ptr;
        if (vi->global) {
            auto g = std::make_unique<GlobalVar>();
            g->slot = vi->slot;
            g->nm = name;
            g->line = line;
            g->isConstVar = cst;
            return g;
        }
        auto l = std::make_unique<LocalVar>();
        l->slot = vi->slot;
        l->nm = name;
        l->line = line;
        l->isConstVar = cst;
        return l;
    }
    auto ec = enumConsts.find(name);
    if (ec != enumConsts.end()) return makeLit(Value::I(ec->second, BT::I16), line);
    for (const BConst* b = kBuiltinConsts; b->name; ++b) {
        if (name != b->name) continue;
        if (b->t == BT::F32) return makeLit(Value::F(b->v), line);
        return makeLit(Value::I((int64_t)b->v, b->t), line);
    }
    if (name == "NAN") return makeLit(Value::F(std::nan("")), line);
    if (name == "INFINITY") return makeLit(Value::F(HUGE_VAL), line);
    // Arduino binary.h: B0 ~ B11111111
    if (name.size() >= 2 && name.size() <= 9 && name[0] == 'B' &&
        name.find_first_not_of("01", 1) == std::string::npos) {
        int64_t v = 0;
        for (size_t k = 1; k < name.size(); ++k) v = v * 2 + (name[k] - '0');
        return makeLit(Value::I(v, BT::I16), line);
    }
    double dv = 0;
    if (host && host->lookupConstant(name, dv)) {
        if (dv == std::floor(dv) && std::fabs(dv) < 9.0e15) {
            int64_t iv = (int64_t)dv;
            BT t = (iv >= -32768 && iv <= 32767) ? BT::I16 : (iv >= INT32_MIN && iv <= INT32_MAX) ? BT::I32 : BT::I64;
            return makeLit(Value::I(iv, t), line);
        }
        return makeLit(Value::F(dv), line);
    }
    auto fr = std::make_unique<FuncRefExpr>();
    fr->line = line;
    fr->nm = name;
    pendingRefs.push_back(fr.get());
    return fr;
}

} // namespace interp
} // namespace ard
