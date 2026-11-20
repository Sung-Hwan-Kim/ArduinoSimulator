// InterpParser.cpp - 선언/타입/문장 파싱
#include "InterpParser.h"
#include <algorithm>

namespace ard {
namespace interp {

Parser::Parser(std::vector<Token>& toks, Program& prog, ArduinoHost* h) : T(toks), P(prog), host(h) {}

void Parser::fail(const std::string& msg, int line) { throw CompileError{msg, line}; }

std::string Parser::tokDesc(const Token& t) const {
    switch (t.type) {
    case TT::End: return "파일 끝";
    case TT::Str: return "\"" + t.text + "\"";
    case TT::Char: return "'" + t.text + "'";
    default: return "'" + t.text + "'";
    }
}

void Parser::expect(const char* s) {
    if (accept(s)) return;
    std::string w(s);
    if (w == ";") fail("';' 가 필요합니다", prevLine());
    if (w == ")" || w == "]" || w == "}")
        fail("'" + w + "' 가 필요합니다 (" + tokDesc(cur()) + " 앞)", cur().type == TT::End ? prevLine() : cur().line);
    fail("'" + w + "' 가 필요합니다 (" + tokDesc(cur()) + " 앞)", cur().line);
}

std::string Parser::expectIdent(const char* what) {
    if (cur().type != TT::Ident) failHere(std::string(what) + " 이(가) 필요합니다 (" + tokDesc(cur()) + " 앞)");
    return T[pos++].text;
}

// ---------------- 타입 ----------------
bool Parser::isLibClass(const std::string& n) {
    auto it = libCache.find(n);
    if (it != libCache.end()) return it->second;
    bool r = host && host->isLibraryClass(n);
    libCache[n] = r;
    return r;
}

bool mapNamedType(const std::string& w, BT& b) {
    static const std::unordered_map<std::string, BT> m = {
        {"void", BT::Void}, {"bool", BT::Bool}, {"boolean", BT::Bool},
        {"byte", BT::U8}, {"uint8_t", BT::U8}, {"uint_fast8_t", BT::U8}, {"uint_least8_t", BT::U8},
        {"int8_t", BT::I8}, {"word", BT::U16}, {"uint16_t", BT::U16}, {"size_t", BT::U16},
        {"int16_t", BT::I16}, {"uint32_t", BT::U32}, {"int32_t", BT::I32}, {"uint64_t", BT::U64},
        {"int64_t", BT::I64}, {"float", BT::F32}, {"double", BT::F32}, {"String", BT::Str},
        {"auto", BT::Void}, {"uintptr_t", BT::U16}, {"ptrdiff_t", BT::I16}};
    auto it = m.find(w);
    if (it == m.end()) return false;
    b = it->second;
    return true;
}

bool Parser::isTypeName(size_t at) {
    static const std::unordered_set<std::string> words = {
        "char", "short", "int", "long", "unsigned", "signed", "const", "constexpr", "static", "volatile",
        "struct", "enum", "register", "extern", "inline"};
    const Token& t = T[std::min(at, T.size() - 1)];
    if (t.type != TT::Ident) return false;
    BT b;
    if (words.count(t.text) || mapNamedType(t.text, b)) return true;
    if (typedefs.count(t.text) || structIds.count(t.text) || enumTypes.count(t.text)) return true;
    if (findVar(t.text)) return false;
    return isLibClass(t.text);
}

bool Parser::parseType(TypeInfo& ti, bool& isStatic) {
    size_t start = pos;
    ti = TypeInfo();
    isStatic = false;
    bool haveBase = false, isUns = false, isSig = false, sawChar = false, sawShort = false, sawInt = false;
    int longs = 0;
    BT base = BT::I16;
    while (cur().type == TT::Ident) {
        const std::string w = cur().text;
        if (w == "const" || w == "constexpr") { ti.isConst = true; ++pos; continue; }
        if (w == "static") { isStatic = true; ++pos; continue; }
        if (w == "volatile" || w == "inline" || w == "extern" || w == "register" || w == "PROGMEM") { ++pos; continue; }
        if (w == "unsigned") { isUns = true; ++pos; continue; }
        if (w == "signed") { isSig = true; ++pos; continue; }
        if (w == "long") { ++longs; ++pos; continue; }
        if (w == "short") { sawShort = true; ++pos; continue; }
        if (w == "int") { sawInt = true; ++pos; continue; }
        if (w == "char") { sawChar = true; ++pos; continue; }
        bool anyInt = isUns || isSig || longs || sawShort || sawInt || sawChar;
        if (haveBase || anyInt) {
            if (w == "double" && longs) { base = BT::F32; haveBase = true; longs = 0; ++pos; continue; }
            break;
        }
        if (w == "struct" || w == "union") {
            ++pos;
            int sid = parseStructDef();
            base = BT::Struct;
            ti.structId = sid;
            haveBase = true;
            continue;
        }
        if (w == "enum") {
            ++pos;
            if (cur().type == TT::Ident) ++pos;
            base = BT::I16;
            haveBase = true;
            continue;
        }
        BT b;
        if (mapNamedType(w, b)) {
            base = b;
            if (w == "auto") ti.cls = "auto";
            haveBase = true;
            ++pos;
            continue;
        }
        auto td = typedefs.find(w);
        if (td != typedefs.end()) {
            bool c = ti.isConst;
            ti = td->second;
            ti.isConst = ti.isConst || c;
            base = ti.bt;
            haveBase = true;
            ++pos;
            continue;
        }
        auto si = structIds.find(w);
        if (si != structIds.end()) { base = BT::Struct; ti.structId = si->second; haveBase = true; ++pos; continue; }
        if (enumTypes.count(w)) { base = BT::I16; haveBase = true; ++pos; continue; }
        if (!findVar(w) && isLibClass(w)) { base = BT::Obj; ti.cls = w; haveBase = true; ++pos; continue; }
        break;
    }
    if (!(haveBase || isUns || isSig || longs || sawShort || sawInt || sawChar)) {
        pos = start;
        return false;
    }
    if (!haveBase) {
        if (sawChar) base = isUns ? BT::U8 : BT::I8;
        else if (sawShort) base = isUns ? BT::U16 : BT::I16;
        else if (longs >= 2) base = isUns ? BT::U64 : BT::I64;
        else if (longs == 1) base = isUns ? BT::U32 : BT::I32;
        else base = isUns ? BT::U16 : BT::I16;
    }
    ti.bt = base;
    while (isId("const") || isId("volatile") || isId("PROGMEM")) {
        if (isId("const")) ti.isConst = true;
        ++pos;
    }
    return true;
}

int Parser::parseStructDef() {
    std::string name;
    if (cur().type == TT::Ident) name = T[pos++].text;
    if (!isP("{")) {
        auto it = structIds.find(name);
        if (it == structIds.end()) failHere("알 수 없는 구조체: " + name);
        return it->second;
    }
    int sid = (int)P.structs.size();
    P.structs.emplace_back();
    P.structs[(size_t)sid].name = name.empty() ? std::string("(익명)") : name;
    if (!name.empty()) structIds[name] = sid;
    parseStructBody(sid);
    return sid;
}

void Parser::parseStructBody(int sid) {
    expect("{");
    while (!accept("}")) {
        if (cur().type == TT::End) failHere("구조체 정의에 '}' 가 필요합니다");
        if (isId("public") || isId("private") || isId("protected")) { ++pos; expect(":"); continue; }
        TypeInfo ft;
        bool st;
        if (!parseType(ft, st)) failHere("구조체 멤버의 타입이 필요합니다 (" + tokDesc(cur()) + ")");
        do {
            TypeInfo t2 = ft;
            bool ptr = false;
            while (accept("*")) ptr = true;
            if (ptr) { t2.bt = BT::Ptr; t2.isConst = false; }
            FieldDef fd;
            fd.name = expectIdent("멤버 이름");
            if (isP("(")) failHere("구조체 안의 함수(메서드/생성자)는 지원하지 않습니다");
            while (accept("[")) {
                ExprP de = parseExpr();
                Value v;
                if (!constEval(de.get(), v) || v.k != VK::Int || v.i <= 0 || v.i > 10000)
                    failHere("구조체 멤버 배열의 크기는 양의 상수여야 합니다");
                fd.dims.push_back((int)v.i);
                expect("]");
            }
            if (isP("=") || isP("{")) failHere("구조체 멤버의 기본값 지정은 지원하지 않습니다");
            if (t2.bt == BT::Obj) failHere("구조체 안의 라이브러리 객체는 지원하지 않습니다");
            if (t2.bt == BT::Void) failHere("void 형 멤버는 사용할 수 없습니다");
            if (t2.bt == BT::Struct && t2.structId == sid) failHere("구조체가 자기 자신을 멤버로 가질 수 없습니다");
            fd.type = t2;
            P.structs[(size_t)sid].fields.push_back(std::move(fd));
        } while (accept(","));
        expect(";");
    }
}

void Parser::parseEnumDef() {
    if (isId("class") || isId("struct")) ++pos;
    std::string name;
    if (cur().type == TT::Ident) { name = T[pos++].text; enumTypes.insert(name); }
    if (accept(":")) { TypeInfo dummy; bool st; if (!parseType(dummy, st)) failHere("열거형 기반 타입이 필요합니다"); }
    expect("{");
    int64_t next = 0;
    while (!accept("}")) {
        std::string id = expectIdent("열거 상수 이름");
        int ln = prevLine();
        if (accept("=")) {
            ExprP e = parseTernary();
            Value v;
            if (!constEval(e.get(), v)) fail("열거 상수 값은 상수여야 합니다: " + id, ln);
            next = toI(v, ln);
        }
        enumConsts[id] = next;
        if (!name.empty()) enumConsts[name + "::" + id] = next;
        ++next;
        if (!accept(",")) { expect("}"); break; }
    }
}

void Parser::parseTypedef() {
    ++pos;
    TypeInfo ti;
    bool st;
    if (isId("struct") || isId("union")) {
        ++pos;
        int sid = parseStructDef();
        ti.bt = BT::Struct;
        ti.structId = sid;
    } else if (isId("enum")) {
        ++pos;
        parseEnumDef();
        ti.bt = BT::I16;
    } else if (!parseType(ti, st)) failHere("typedef 뒤에 타입이 필요합니다");
    do {
        TypeInfo t2 = ti;
        bool ptr = false;
        while (accept("*")) ptr = true;
        if (ptr) { t2.bt = BT::Ptr; t2.isConst = false; }
        if (isP("(")) failHere("함수 포인터 typedef 는 지원하지 않습니다");
        std::string name = expectIdent("타입 이름");
        if (isP("[")) failHere("배열 typedef 는 지원하지 않습니다");
        typedefs[name] = t2;
        if (t2.bt == BT::Struct && P.structs[(size_t)t2.structId].name == "(익명)") P.structs[(size_t)t2.structId].name = name;
    } while (accept(","));
    expect(";");
}

// ---------------- 변수 ----------------
const Parser::VarInfo* Parser::findVar(const std::string& name) const {
    for (size_t k = scopes.size(); k-- > 0;) {
        auto it = scopes[k].find(name);
        if (it != scopes[k].end()) return &it->second;
    }
    auto it = globals.find(name);
    return it != globals.end() ? &it->second : nullptr;
}

void Parser::declareVar(const std::string& name, VarInfo& vi, int line) {
    auto& sc = scopes.empty() ? globals : scopes.back();
    if (sc.count(name)) fail("'" + name + "' 이(가) 이미 선언되었습니다", line);
    sc[name] = vi;
}

bool Parser::isDeclStart() {
    const Token& t = cur();
    if (t.type != TT::Ident) return false;
    const std::string& w = t.text;
    if (w == "const" || w == "static" || w == "volatile" || w == "unsigned" || w == "signed" || w == "struct" ||
        w == "enum" || w == "constexpr" || w == "register" || w == "union")
        return true;
    if (!isTypeName(pos)) return false;
    if (isPAt(pos + 1, "(") || isPAt(pos + 1, "::") || isPAt(pos + 1, ".")) return false;
    return true;
}

void Parser::parseInitList(InitNode& n) {
    n.isList = true;
    n.line = cur().line;
    expect("{");
    while (!isP("}")) {
        if (isP(".")) failHere("지정 초기화(.멤버 = 값)는 지원하지 않습니다");
        InitNode kid;
        kid.line = cur().line;
        if (isP("{")) parseInitList(kid);
        else kid.e = parseAssign();
        n.kids.push_back(std::move(kid));
        if (!accept(",")) break;
    }
    expect("}");
}

void Parser::parseDeclarator(const TypeInfo& base, bool isStatic, bool global, DeclItem& it) {
    TypeInfo t = base;
    bool ptr = false;
    while (accept("*")) {
        ptr = true;
        while (isId("const")) ++pos;
    }
    if (ptr) { t.bt = BT::Ptr; t.isConst = false; t.cls.clear(); }
    if (accept("&") || accept("&&")) it.isRef = true;
    if (isP("(")) failHere("함수 포인터는 지원하지 않습니다");
    it.line = cur().line;
    it.name = expectIdent("변수 이름");
    if (curFn && isP("(")) {
        size_t k = pos;
        int depth = 0;
        for (; k < T.size() && T[k].type != TT::End; ++k) {
            if (isPAt(k, "(")) ++depth;
            else if (isPAt(k, ")") && --depth == 0) break;
        }
        if (isPAt(k + 1, "{"))
            fail("'}' 가 필요합니다 (함수 '" + curFn->name + "' 의 끝 '}' 가 빠진 것 같습니다 - 함수 안에서 다른 함수 '" +
                 it.name + "' 를 정의할 수 없습니다)", it.line);
    }
    while (accept("[")) {
        it.isArray = true;
        if (isP("]")) {
            if (!it.dims.empty()) failHere("첫 번째 차원만 크기를 생략할 수 있습니다");
            it.dims.push_back(nullptr);
        } else it.dims.push_back(parseExpr());
        expect("]");
    }
    while (isId("PROGMEM")) ++pos;
    if (t.bt == BT::Void && t.cls != "auto") fail("void 형 변수는 선언할 수 없습니다: " + it.name, it.line);
    if (accept("=")) {
        if (isP("{")) { it.init = DeclItem::List; parseInitList(it.list); }
        else if (t.bt == BT::Obj && !it.isArray && cur().type == TT::Ident && cur().text == t.cls && isPAt(pos + 1, "(")) {
            ++pos;
            it.ctorArgs = parseArgs();
            it.init = DeclItem::Ctor;
        } else {
            it.initExpr = parseAssign();
            it.init = DeclItem::Expr_;
        }
    } else if (isP("(") && !it.isArray) {
        it.ctorArgs = parseArgs();
        it.init = DeclItem::Ctor;
    } else if (isP("{")) {
        if (t.bt == BT::Obj) {
            ++pos;
            while (!isP("}")) { it.ctorArgs.push_back(parseAssign()); if (!accept(",")) break; }
            expect("}");
            it.init = DeclItem::Ctor;
        } else {
            it.init = DeclItem::List;
            parseInitList(it.list);
        }
    }
    if (t.cls == "auto" && it.init != DeclItem::Expr_) fail("auto 변수는 '=' 로 초기화해야 합니다: " + it.name, it.line);
    if (it.isRef && it.init != DeclItem::Expr_) fail("참조 변수는 '=' 로 초기화해야 합니다: " + it.name, it.line);
    it.type = t;
    VarInfo vi;
    vi.type = t;
    vi.isArray = it.isArray;
    vi.isRef = it.isRef;
    if (global || isStatic) { vi.global = true; vi.slot = P.nglobals++; }
    else vi.slot = nextSlot++;
    it.global = vi.global;
    it.slot = vi.slot;
    if (isStatic && !global) it.staticIdx = P.nstatics++;
    if (t.isConst && it.init == DeclItem::Expr_ && !it.isArray && !it.isRef && (isIntBT(t.bt) || isFloatBT(t.bt)) &&
        it.initExpr->isConst()) {
        Value v;
        if (constEval(it.initExpr.get(), v)) {
            Value d = isIntBT(t.bt) ? Value::I(0, t.bt) : Value::F(0, t.bt);
            try { assignTo(d, v, it.line); } catch (RtError& e) { fail(e.msg, it.line); }
            vi.hasConst = true;
            vi.constVal = d;
        }
    }
    declareVar(it.name, vi, it.line);
}

std::unique_ptr<DeclStmt> Parser::parseDeclaratorsAfterType(const TypeInfo& ti, bool isStatic, bool global, int line) {
    auto ds = std::make_unique<DeclStmt>();
    ds->line = line;
    do {
        DeclItem it;
        parseDeclarator(ti, isStatic, global, it);
        ds->items.push_back(std::move(it));
    } while (accept(","));
    return ds;
}

std::unique_ptr<DeclStmt> Parser::parseDeclaration(bool global) {
    int line = cur().line;
    TypeInfo ti;
    bool isStatic;
    if (!parseType(ti, isStatic)) failHere("타입이 필요합니다 (" + tokDesc(cur()) + ")");
    return parseDeclaratorsAfterType(ti, isStatic, global, line);
}

// ---------------- 최상위 ----------------
void Parser::parseProgram() {
    parseTopLevel();
    finish();
}

void Parser::parseTopLevel() {
    while (cur().type != TT::End) {
        if (accept(";")) continue;
        int line = cur().line;
        if (isId("typedef")) { parseTypedef(); continue; }
        if (isId("using")) {
            while (!isP(";") && cur().type != TT::End) ++pos;
            expect(";");
            continue;
        }
        if (isId("template")) failHere("템플릿(template)은 지원하지 않습니다");
        if (isId("class")) failHere("클래스(class) 정의는 지원하지 않습니다 (struct 를 사용하세요)");
        if (isId("namespace")) failHere("namespace 는 지원하지 않습니다");
        if (isId("ISR") && isPAt(pos + 1, "("))
            failHere("ISR(...) 하드웨어 인터럽트 벡터는 지원하지 않습니다 (attachInterrupt 를 사용하세요)");
        if (isId("enum")) {
            size_t save = pos;
            ++pos;
            if (isId("class") || isId("struct")) ++pos;
            if (cur().type == TT::Ident) ++pos;
            if (accept(":")) while (cur().type == TT::Ident) ++pos;
            bool def = isP("{");
            pos = save;
            if (def) {
                ++pos;
                parseEnumDef();
                if (accept(";")) continue;
                TypeInfo ti;
                ti.bt = BT::I16;
                auto ds = parseDeclaratorsAfterType(ti, false, true, line);
                expect(";");
                P.globalInit.push_back(std::move(ds));
                continue;
            }
        }
        if (isId("struct") || isId("union")) {
            size_t save = pos;
            ++pos;
            if (cur().type == TT::Ident) ++pos;
            bool def = isP("{");
            pos = save;
            if (def) {
                ++pos;
                int sid = parseStructDef();
                if (accept(";")) continue;
                TypeInfo ti;
                ti.bt = BT::Struct;
                ti.structId = sid;
                auto ds = parseDeclaratorsAfterType(ti, false, true, line);
                expect(";");
                P.globalInit.push_back(std::move(ds));
                continue;
            }
        }
        TypeInfo ti;
        bool isStatic;
        if (!parseType(ti, isStatic)) {
            const Token& t = cur();
            if (t.type == TT::Ident && peekTok(1).type == TT::Ident)
                failHere("알 수 없는 타입: " + t.text + " (지원하지 않는 라이브러리이거나 오타입니다)");
            if (t.type == TT::Ident && isPAt(pos + 1, "("))
                failHere("함수 밖에서는 함수를 호출할 수 없습니다: " + t.text + "() (setup() 또는 loop() 안에 작성하세요)");
            failHere("함수 밖에는 선언만 올 수 있습니다 (" + tokDesc(t) + " 근처)");
        }
        size_t save = pos;
        TypeInfo rt = ti;
        bool ptr = false;
        while (accept("*")) ptr = true;
        accept("&");
        if (cur().type == TT::Ident && isPAt(pos + 1, "(") && ti.bt != BT::Obj &&
            (isPAt(pos + 2, ")") || isTypeName(pos + 2))) {
            std::string name = cur().text;
            int fl = cur().line;
            ++pos;
            if (ptr) { rt.bt = BT::Ptr; rt.isConst = false; }
            parseFunction(rt, name, fl);
            continue;
        }
        pos = save;
        auto ds = parseDeclaratorsAfterType(ti, isStatic, true, line);
        expect(";");
        P.globalInit.push_back(std::move(ds));
    }
}

void Parser::parseFunction(const TypeInfo& ret, const std::string& name, int line) {
    expect("(");
    std::vector<ParamDef> params;
    if (isId("void") && isPAt(pos + 1, ")")) ++pos;
    while (!isP(")")) {
        ParamDef pd;
        TypeInfo pt;
        bool st;
        if (!parseType(pt, st)) failHere("매개변수의 타입이 필요합니다 (" + tokDesc(cur()) + ")");
        bool ptr = false;
        while (accept("*")) { ptr = true; while (isId("const")) ++pos; }
        if (accept("&") || accept("&&")) pd.isRef = true;
        if (cur().type == TT::Ident) pd.name = T[pos++].text;
        while (accept("[")) {
            pd.isArray = true;
            while (!isP("]") && cur().type != TT::End) ++pos;
            expect("]");
        }
        if (ptr) { pt.bt = BT::Ptr; pt.isConst = false; pd.isArray = true; }
        if (pt.bt == BT::Void && !ptr) failHere("void 형 매개변수는 사용할 수 없습니다");
        if (accept("=")) pd.defVal = parseAssign();
        pd.type = pt;
        params.push_back(std::move(pd));
        if (!accept(",")) break;
    }
    expect(")");
    while (isId("const") || isId("override") || isId("noexcept")) ++pos;
    FuncDef* f = nullptr;
    auto& ov = P.funcByName[name];
    for (FuncDef* g : ov) if (g->params.size() == params.size()) { f = g; break; }
    if (!f) {
        auto nf = std::make_unique<FuncDef>();
        f = nf.get();
        f->name = name;
        P.funcs.push_back(std::move(nf));
        ov.push_back(f);
    }
    if (accept(";")) {
        if (!f->body) { f->ret = ret; f->line = line; f->params = std::move(params); }
        return;
    }
    if (!isP("{")) failHere("함수 본문 '{' 또는 ';' 가 필요합니다");
    if (f->body) fail("함수 '" + name + "' 이(가) 이미 정의되어 있습니다 (매개변수 개수가 같은 오버로딩은 지원하지 않습니다)", line);
    for (size_t k = 0; k < params.size() && k < f->params.size(); ++k)
        if (!params[k].defVal && f->params[k].defVal) params[k].defVal = std::move(f->params[k].defVal);
    f->params = std::move(params);
    f->ret = ret;
    f->line = line;
    curFn = f;
    scopes.clear();
    scopes.emplace_back();
    nextSlot = 0;
    loopDepth = switchDepth = 0;
    for (ParamDef& pd : f->params) {
        VarInfo vi;
        vi.slot = nextSlot++;
        vi.type = pd.type;
        vi.isRef = pd.isRef;
        vi.isArray = pd.isArray;
        vi.isPtrParam = pd.isArray;
        if (!pd.name.empty()) declareVar(pd.name, vi, line);
    }
    f->body = parseBlock();
    f->nslots = nextSlot;
    curFn = nullptr;
    scopes.clear();
}

// ---------------- 문장 ----------------
StmtP Parser::parseBlock() {
    int line = cur().line;
    expect("{");
    auto b = std::make_unique<BlockStmt>();
    b->line = line;
    scopes.emplace_back();
    while (!isP("}")) {
        if (cur().type == TT::End) fail("'}' 가 필요합니다 (" + std::to_string(line) + "번째 줄의 '{' 가 닫히지 않았습니다)", prevLine());
        b->body.push_back(parseStatement());
    }
    ++pos;
    scopes.pop_back();
    return b;
}

StmtP Parser::parseSubStatement() {
    scopes.emplace_back();
    StmtP s = parseStatement();
    scopes.pop_back();
    return s;
}

StmtP Parser::parseStatement() {
    NestGuard guard(*this);
    const Token& t = cur();
    int line = t.line;
    if (t.type == TT::End) fail("'}' 가 필요합니다 (파일이 예상보다 일찍 끝났습니다)", prevLine());
    if (isP("{")) return parseBlock();
    if (isP(";")) { ++pos; auto s = std::make_unique<EmptyStmt>(); s->line = line; return s; }
    if (t.type == TT::Ident) {
        const std::string w = t.text;
        if (w == "if") {
            ++pos;
            expect("(");
            auto s = std::make_unique<IfStmt>();
            s->line = line;
            s->cond = parseExpr();
            expect(")");
            s->a = parseSubStatement();
            if (isId("else")) { ++pos; s->b = parseSubStatement(); }
            return s;
        }
        if (w == "while") {
            ++pos;
            expect("(");
            auto s = std::make_unique<WhileStmt>();
            s->line = line;
            s->cond = parseExpr();
            expect(")");
            ++loopDepth;
            s->body = parseSubStatement();
            --loopDepth;
            return s;
        }
        if (w == "do") {
            ++pos;
            auto s = std::make_unique<DoStmt>();
            s->line = line;
            ++loopDepth;
            s->body = parseSubStatement();
            --loopDepth;
            if (!isId("while")) failHere("do 문 뒤에 'while' 이 필요합니다");
            ++pos;
            expect("(");
            s->cond = parseExpr();
            expect(")");
            expect(";");
            return s;
        }
        if (w == "for") {
            ++pos;
            expect("(");
            auto s = std::make_unique<ForStmt>();
            s->line = line;
            scopes.emplace_back();
            if (!accept(";")) {
                if (isDeclStart()) s->init = parseDeclaration(false);
                else {
                    auto es = std::make_unique<ExprStmt>();
                    es->line = cur().line;
                    es->e = parseExpr();
                    s->init = std::move(es);
                }
                if (isP(":")) failHere("범위 기반 for 문(for (x : 배열))은 지원하지 않습니다");
                expect(";");
            }
            if (!isP(";")) s->cond = parseExpr();
            expect(";");
            if (!isP(")")) s->step = parseExpr();
            expect(")");
            ++loopDepth;
            s->body = parseSubStatement();
            --loopDepth;
            scopes.pop_back();
            return s;
        }
        if (w == "switch") {
            ++pos;
            expect("(");
            auto s = std::make_unique<SwitchStmt>();
            s->line = line;
            s->cond = parseExpr();
            expect(")");
            int bl = cur().line;
            expect("{");
            ++switchDepth;
            scopes.emplace_back();
            while (!isP("}")) {
                if (cur().type == TT::End) fail("switch 문에 '}' 가 필요합니다 (" + std::to_string(bl) + "번째 줄)", prevLine());
                if (isId("case")) {
                    ++pos;
                    ExprP v = parseTernary();
                    if (isP("...")) failHere("case 범위(...)는 지원하지 않습니다");
                    expect(":");
                    s->cases.emplace_back(std::move(v), (int)s->body.size());
                    continue;
                }
                if (isId("default")) {
                    int dl = cur().line;
                    ++pos;
                    expect(":");
                    if (s->defIdx >= 0) fail("default 가 두 번 이상 있습니다", dl);
                    s->defIdx = (int)s->body.size();
                    continue;
                }
                s->body.push_back(parseStatement());
            }
            ++pos;
            scopes.pop_back();
            --switchDepth;
            return s;
        }
        if (w == "case" || w == "default") failHere("'" + w + "' 는 switch 문 안에서만 사용할 수 있습니다");
        if (w == "break") {
            if (loopDepth == 0 && switchDepth == 0) failHere("'break' 는 반복문이나 switch 문 안에서만 사용할 수 있습니다");
            ++pos;
            expect(";");
            auto s = std::make_unique<BreakStmt>();
            s->line = line;
            return s;
        }
        if (w == "continue") {
            if (loopDepth == 0) failHere("'continue' 는 반복문 안에서만 사용할 수 있습니다");
            ++pos;
            expect(";");
            auto s = std::make_unique<ContinueStmt>();
            s->line = line;
            return s;
        }
        if (w == "return") {
            ++pos;
            auto s = std::make_unique<ReturnStmt>();
            s->line = line;
            if (!isP(";")) s->e = parseExpr();
            expect(";");
            return s;
        }
        if (w == "goto") failHere("goto 는 지원하지 않습니다");
        if (w == "else") failHere("'else' 앞에 짝이 되는 'if' 가 없습니다");
        if (w == "enum") {
            size_t save = pos;
            ++pos;
            if (isId("class") || isId("struct")) ++pos;
            if (cur().type == TT::Ident) ++pos;
            bool def = isP("{");
            pos = save;
            if (def) {
                ++pos;
                parseEnumDef();
                if (accept(";")) { auto s = std::make_unique<EmptyStmt>(); s->line = line; return s; }
                TypeInfo ti;
                ti.bt = BT::I16;
                auto ds = parseDeclaratorsAfterType(ti, false, false, line);
                expect(";");
                return ds;
            }
        }
        if (isDeclStart()) {
            auto d = parseDeclaration(false);
            expect(";");
            return d;
        }
    }
    auto es = std::make_unique<ExprStmt>();
    es->line = line;
    es->e = parseExpr();
    expect(";");
    return es;
}

// ---------------- 마무리 (함수 이름 해석) ----------------
void Parser::finish() {
    for (CallExpr* ce : pendingCalls) {
        size_t n = ce->args.size();
        auto it = P.funcByName.find(ce->nm);
        if (it != P.funcByName.end() && !it->second.empty()) {
            FuncDef* chosen = nullptr;
            for (FuncDef* f : it->second)
                if (n >= f->minArgs() && n <= f->params.size()) { chosen = f; break; }
            if (!chosen) {
                size_t want = it->second[0]->params.size();
                fail("함수 '" + ce->nm + "' 의 인자 개수가 맞지 않습니다 (필요 " + std::to_string(want) + "개, 전달 " +
                     std::to_string(n) + "개)", ce->line);
            }
            if (!chosen->body) fail("함수 '" + ce->nm + "' 이(가) 선언만 되어 있고 정의(본문)가 없습니다", ce->line);
            ce->fn = chosen;
            continue;
        }
        int mn = 0, mx = 0;
        int id = findBuiltin(ce->nm, mn, mx);
        if (id >= 0) {
            if ((int)n < mn || (mx >= 0 && (int)n > mx)) {
                std::string want = mn == mx ? std::to_string(mn) : (mx < 0 ? std::to_string(mn) + "개 이상" : std::to_string(mn) + "~" + std::to_string(mx));
                fail("함수 '" + ce->nm + "' 의 인자 개수가 맞지 않습니다 (필요 " + want + "개, 전달 " + std::to_string(n) + "개)", ce->line);
            }
            ce->builtin = id;
            continue;
        }
        fail("정의되지 않은 함수: " + ce->nm, ce->line);
    }
    for (FuncRefExpr* fr : pendingRefs) {
        auto it = P.funcByName.find(fr->nm);
        if (it != P.funcByName.end()) {
            for (FuncDef* f : it->second) if (f->body) { fr->fn = f; break; }
        }
        if (!fr->fn) fail("정의되지 않은 식별자: " + fr->nm, fr->line);
    }
    auto findMain = [&](const char* nm) -> FuncDef* {
        auto it = P.funcByName.find(nm);
        if (it == P.funcByName.end()) return nullptr;
        for (FuncDef* f : it->second) if (f->params.empty() && f->body) return f;
        return nullptr;
    };
    P.setupFn = findMain("setup");
    P.loopFn = findMain("loop");
    if (!P.setupFn) fail("setup() 함수가 없습니다", 0);
    if (!P.loopFn) fail("loop() 함수가 없습니다", 0);
}

} // namespace interp
} // namespace ard
