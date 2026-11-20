// InterpNodes.cpp - AST 노드 실행
#include "InterpNodes.h"
#include <algorithm>

namespace ard {
namespace interp {

static const int kMaxDepth = 400;

// ---------------- Ctx ----------------
void Ctx::pollIrq() {
    int irq = host->pollInterrupt();
    if (irq < 0 || irq >= (int)isr.size() || !isr[irq]) return;
    FuncDef* f = isr[irq];
    inIsr = true;
    Value savedRet = std::move(ret);
    invoke(*this, f, nullptr, f->line);
    ret = std::move(savedRet);
    inIsr = false;
}

void Ctx::delayUs(uint64_t us) {
    if (isrCount && intEnabled && !inIsr) {
        while (us > 0) {
            uint64_t ch = us > 1000 ? 1000 : us;
            host->delayMicros(ch);
            us -= ch;
            checkStop();
            pollIrq();
        }
    } else {
        if (us) host->delayMicros(us);
        checkStop();
    }
}

// ---------------- 함수 호출 ----------------
Value invoke(Ctx& c, FuncDef* f, std::vector<ExprP>* args, int line) {
    if (c.depth >= kMaxDepth)
        rtFail("함수 호출이 너무 깊습니다 (재귀 " + std::to_string(kMaxDepth) + "단계 초과 - 스택 오버플로)", line);
    size_t base = c.sp, need = (size_t)f->nslots;
    if (base + need > c.stack.size()) rtFail("스택 메모리가 부족합니다 (지역 변수/재귀가 너무 많습니다)", line);
    Value* nf = c.stack.data() + base;
    c.sp = base + need;
    size_t np = f->params.size();
    size_t na = args ? args->size() : 0;
    for (size_t k = 0; k < np; ++k) {
        ParamDef& pd = f->params[k];
        Expr* ae = k < na ? (*args)[k].get() : pd.defVal.get();
        if (!ae) rtFail("함수 '" + f->name + "' 에 전달된 인자가 부족합니다", line);
        if (pd.isRef && ae->lvalue) {
            Value r;
            r.k = VK::Ref;
            r.p = ae->ref(c);
            nf[k] = r;
            continue;
        }
        Value v = ae->eval(c);
        if (pd.isArray) {
            nf[k] = std::move(v);
            nf[k].t = BT::Ptr;
            continue;
        }
        Value d = makeDefault(c, pd.type);
        assignTo(d, v, ae->line ? ae->line : line);
        nf[k] = std::move(d);
    }
    for (size_t k = np; k < need; ++k)
        if (nf[k].k != VK::Void || nf[k].h) nf[k] = Value();
    Value* oldfp = c.fp;
    c.fp = nf;
    ++c.depth;
    Flow fl = f->body->exec(c);
    c.fp = oldfp;
    --c.depth;
    c.sp = base;
    BT rt = f->ret.bt;
    if (rt == BT::Void) return Value();
    Value r;
    if (fl == F_RETURN) r = std::move(c.ret);
    if (rt == BT::Struct || rt == BT::Obj) return r;
    Value d = makeDefault(c, f->ret);
    if (r.k != VK::Void) assignTo(d, r, line);
    return d;
}

// ---------------- 식 ----------------
Value AssignExpr::eval(Ctx& c) {
    Value v = value->eval(c);
    Value* p = target->ref(c);
    assignTo(*p, v, line);
    return *p;
}

Value CompoundExpr::eval(Ctx& c) {
    Value v = value->eval(c);
    Value* p = target->ref(c);
    if (op == OP_ADD && p->k == VK::Str && p->t == BT::Str) {
        if (v.k == VK::Void) rtFail("void 값은 String 에 더할 수 없습니다", line);
        std::string add = toStr(v);
        mutStr(*p) += add;
        return *p;
    }
    Value r = binop(op, *p, v, line);
    assignTo(*p, r, line);
    return *p;
}

Value IncDecExpr::eval(Ctx& c) {
    Value* p = target->ref(c);
    if (p->k == VK::Int) {
        int64_t old = p->i;
        p->i = wrapInt(p->i + delta, p->t);
        if (pre) return *p;
        Value r = *p;
        r.i = old;
        return r;
    }
    if (p->k == VK::Float) {
        Value old = *p;
        p->f = (double)(float)(p->f + delta);
        return pre ? *p : old;
    }
    if (p->k == VK::Arr || p->k == VK::Str) {
        Value old = *p;
        Value r = binop(OP_ADD, *p, Value::I(delta, BT::I16), line);
        assignTo(*p, r, line);
        return pre ? *p : old;
    }
    rtFail("++/-- 는 숫자 변수에만 사용할 수 있습니다", line);
}

Value TernExpr::eval(Ctx& c) {
    if (truthy(cond->eval(c))) return a->eval(c);
    return b->eval(c);
}

Value CastExpr::eval(Ctx& c) {
    Value v = a->eval(c);
    BT bt = type.bt;
    if (isIntBT(bt)) {
        Value d = Value::I(0, bt);
        assignTo(d, v, line);
        return d;
    }
    if (isFloatBT(bt)) {
        Value d = Value::F(0, bt);
        assignTo(d, v, line);
        return d;
    }
    switch (bt) {
    case BT::Void: return Value();
    case BT::Str: return Value::S(toStr(v));
    case BT::Ptr: {
        if (v.k == VK::Int && v.i == 0) { Value d; d.k = VK::Ptr; d.t = BT::Ptr; d.p = nullptr; return d; }
        v.t = BT::Ptr;
        return v;
    }
    default: return v;
    }
}

Value* IndexExpr::locate(Ctx& c, Value& tmp, Value& sub, bool& isSub, bool& isChar, int64_t& ch) {
    Value* bp;
    if (!baseByValue) bp = base->ref(c);
    else { tmp = base->eval(c); bp = &tmp; }
    int64_t ix = toI(idx->eval(c), line);
    isSub = false;
    isChar = false;
    switch (bp->k) {
    case VK::Arr: {
        ArrObj* a = arrOf(*bp);
        size_t L = bp->lvl;
        int64_t dim = a->dims[L];
        int64_t st = (int64_t)a->strides[L];
        int64_t eff = (L == 0) ? ix + bp->i / st : ix;
        if (eff < 0 || eff >= dim)
            rtFail("배열 인덱스 범위 초과: " + (nm.empty() ? std::string("배열") : nm) + "[" + std::to_string(eff) +
                   "] (크기 " + std::to_string(dim) + ")", line);
        int64_t pos = bp->i + ix * st;
        if (L + 1 < a->dims.size()) {
            sub = *bp;
            sub.i = pos;
            sub.lvl = (uint8_t)(L + 1);
            sub.t = BT::Void;
            isSub = true;
            return nullptr;
        }
        return &a->data[(size_t)pos];
    }
    case VK::Str: {
        const std::string& s = strOf(*bp);
        isChar = true;
        if (ix < 0 || ix >= (int64_t)s.size()) {
            if (bp->t != BT::Str && ix != (int64_t)s.size())
                rtFail("문자열 인덱스 범위 초과: " + (nm.empty() ? std::string("문자열") : nm) + "[" + std::to_string(ix) +
                       "] (길이 " + std::to_string(s.size()) + ")", line);
            ch = 0;
        } else ch = (int8_t)s[(size_t)ix];
        return nullptr;
    }
    case VK::Ptr:
        if (!bp->p) rtFail("NULL 포인터를 사용했습니다", line);
        if (ix != 0) rtFail("단일 변수 포인터에는 [0] 만 사용할 수 있습니다", line);
        return bp->p;
    default:
        rtFail("배열이 아닌 값에 [] 를 사용할 수 없습니다" + (nm.empty() ? std::string() : ": " + nm), line);
    }
}

Value IndexExpr::eval(Ctx& c) {
    Value tmp, sub;
    bool isSub, isChar;
    int64_t ch = 0;
    Value* p = locate(c, tmp, sub, isSub, isChar, ch);
    if (isSub) return sub;
    if (isChar) return Value::I(ch, BT::I8);
    return *p;
}

Value* IndexExpr::ref(Ctx& c) {
    Value tmp, sub;
    bool isSub, isChar;
    int64_t ch = 0;
    Value* p = locate(c, tmp, sub, isSub, isChar, ch);
    if (isSub) rtFail("배열 전체에는 대입할 수 없습니다", line);
    if (isChar) rtFail("String 의 문자를 바꾸려면 setCharAt() 을 사용하세요", line);
    return p;
}

Value* FieldExpr::fieldOf(Ctx& c, Value* s) {
    if (s->k != VK::Struct) rtFail("구조체가 아닌 값에 '." + field + "' 를 사용할 수 없습니다", line);
    StructObj* so = structOf(*s);
    if (so->sid != csid) {
        const StructDef& sd = c.prog->structs[so->sid];
        int idx = -1;
        for (size_t k = 0; k < sd.fields.size(); ++k) if (sd.fields[k].name == field) { idx = (int)k; break; }
        if (idx < 0) rtFail("구조체 '" + sd.name + "' 에 '" + field + "' 멤버가 없습니다", line);
        csid = so->sid;
        cidx = idx;
    }
    return &so->f[(size_t)cidx];
}

Value* FieldExpr::ref(Ctx& c) {
    if (!base->lvalue) rtFail("임시 구조체 값의 멤버에는 대입할 수 없습니다", line);
    return fieldOf(c, base->ref(c));
}

Value FieldExpr::eval(Ctx& c) {
    if (base->lvalue) return *fieldOf(c, base->ref(c));
    Value tmp = base->eval(c);
    return *fieldOf(c, &tmp);
}

Value AddrExpr::eval(Ctx& c) {
    if (IndexExpr* ie = dynamic_cast<IndexExpr*>(a.get())) {
        Value bv = ie->base->eval(c);
        Value iv = ie->idx->eval(c);
        if (bv.k == VK::Arr) {
            // 범위 검사 (끝 바로 다음은 허용)
            ArrObj* ar = arrOf(bv);
            int64_t ix = toI(iv, line);
            int64_t eff = bv.lvl == 0 ? ix + bv.i / (int64_t)ar->strides[0] : ix;
            if (eff < 0 || eff > ar->dims[bv.lvl])
                rtFail("배열 인덱스 범위 초과: " + ie->nm + "[" + std::to_string(eff) + "] (크기 " +
                       std::to_string(ar->dims[bv.lvl]) + ")", line);
        }
        return binop(OP_ADD, bv, iv, line);
    }
    if (dynamic_cast<FuncRefExpr*>(a.get())) return a->eval(c);
    if (!a->lvalue) rtFail("주소(&)를 구할 수 없는 식입니다", line);
    Value* p = a->ref(c);
    if (p->k == VK::Arr) { Value r = *p; r.t = BT::Ptr; return r; }
    if (p->k == VK::Obj) return *p;
    Value v;
    v.k = VK::Ptr;
    v.t = BT::Ptr;
    v.p = p;
    return v;
}

static Value* derefPtr(const Value& v, int line, Value& sub, bool& isSub) {
    isSub = false;
    if (v.k == VK::Ptr) {
        if (!v.p) rtFail("NULL 포인터를 역참조(*)했습니다", line);
        return v.p;
    }
    if (v.k == VK::Arr) {
        ArrObj* ar = arrOf(v);
        if (v.i < 0 || v.i >= (int64_t)ar->data.size()) rtFail("포인터가 배열 범위를 벗어났습니다", line);
        if ((size_t)v.lvl + 1 < ar->dims.size()) {
            sub = v;
            sub.lvl = (uint8_t)(v.lvl + 1);
            sub.t = BT::Void;
            isSub = true;
            return nullptr;
        }
        return &ar->data[(size_t)v.i];
    }
    rtFail("포인터가 아닌 값은 역참조(*)할 수 없습니다", line);
}

Value* DerefExpr::ref(Ctx& c) {
    Value v = a->eval(c);
    Value sub;
    bool isSub;
    Value* p = derefPtr(v, line, sub, isSub);
    if (isSub) rtFail("배열 전체에는 대입할 수 없습니다", line);
    return p;
}

Value DerefExpr::eval(Ctx& c) {
    Value v = a->eval(c);
    if (v.k == VK::Str) {
        const std::string& s = strOf(v);
        return Value::I(s.empty() ? 0 : (int8_t)s[0], BT::I8);
    }
    Value sub;
    bool isSub;
    Value* p = derefPtr(v, line, sub, isSub);
    if (isSub) return sub;
    return *p;
}

Value MethodExpr::eval(Ctx& c) {
    Value tmp;
    Value* rp;
    if (recv->lvalue) rp = recv->ref(c);
    else { tmp = recv->eval(c); rp = &tmp; }
    switch (rp->k) {
    case VK::Str:
        if (strMethod < 0) rtFail("String 에는 '" + method + "' 메서드가 없습니다", line);
        return callStringMethod(c, rp, strMethod, method, args, line);
    case VK::Obj: {
        if (!rp->h) rtFail("초기화되지 않은 라이브러리 객체입니다", line);
        LibObj* o = libOf(*rp);
        return callLibrary(c, o->cls, o->id, method, args, line);
    }
    case VK::Arr:
        rtFail("char 배열에는 '." + method + "()' 를 사용할 수 없습니다 (String 을 사용하세요)", line);
    default:
        rtFail("'" + method + "' 메서드를 호출할 수 없는 값입니다", line);
    }
}

Value* EepromIndexExpr::ref(Ctx& c) {
    int64_t a = toI(idx->eval(c), line);
    if (a < 0 || a >= (int64_t)c.eeprom.size())
        rtFail("EEPROM 주소 범위 초과: " + std::to_string(a) + " (크기 " + std::to_string(c.eeprom.size()) + ")", line);
    return &c.eeprom[(size_t)a];
}

Value SizeofExpr::eval(Ctx& c) {
    Value v = a->eval(c);
    return Value::I(sizeOfValue(v, *c.prog), BT::U16);
}

// ---------------- 문장 ----------------
Flow WhileStmt::exec(Ctx& c) {
    while (true) {
        c.tick();
        if (!truthy(cond->eval(c))) break;
        Flow f = body->exec(c);
        if (f == F_BREAK) break;
        if (f == F_RETURN) return f;
    }
    return F_NORMAL;
}

Flow DoStmt::exec(Ctx& c) {
    do {
        c.tick();
        Flow f = body->exec(c);
        if (f == F_BREAK) break;
        if (f == F_RETURN) return f;
    } while (truthy(cond->eval(c)));
    return F_NORMAL;
}

Flow ForStmt::exec(Ctx& c) {
    if (init) init->exec(c);
    while (true) {
        c.tick();
        if (cond && !truthy(cond->eval(c))) break;
        Flow f = body->exec(c);
        if (f == F_BREAK) break;
        if (f == F_RETURN) return f;
        if (step) step->eval(c);
    }
    return F_NORMAL;
}

Flow SwitchStmt::exec(Ctx& c) {
    if (!cached) {
        cvals.clear();
        for (auto& cs : cases) cvals.push_back(toI(cs.first->eval(c), cs.first->line));
        cached = true;
    }
    int64_t v = toI(cond->eval(c), line);
    int start = defIdx;
    for (size_t k = 0; k < cvals.size(); ++k)
        if (cvals[k] == v) { start = cases[k].second; break; }
    if (start < 0) return F_NORMAL;
    for (size_t k = (size_t)start; k < body.size(); ++k) {
        c.tick();
        Flow f = body[k]->exec(c);
        if (f == F_BREAK) return F_NORMAL;
        if (f != F_NORMAL) return f;
    }
    return F_NORMAL;
}

// ---------------- 선언 ----------------
static Value constructObj(Ctx& c, const std::string& cls, std::vector<ExprP>* args, int line) {
    std::vector<LibValue> la;
    if (args) for (auto& a : *args) la.push_back(toLib(a->eval(c)));
    (void)line;
    int id = c.nextInstance++;
    c.host->libraryConstruct(cls, id, la);
    auto o = std::make_shared<LibObj>();
    o->cls = cls;
    o->id = id;
    Value v;
    v.k = VK::Obj;
    v.t = BT::Obj;
    v.h = o;
    return v;
}

static void fillArray(Ctx& c, ArrObj* a, size_t L, size_t off, const InitNode& list, int line);

static void writeChars(ArrObj* a, size_t off, size_t cap, const std::string& s, int line) {
    if (s.size() + 1 > cap && s.size() != cap)
        rtFail("문자열이 배열보다 깁니다 (문자열 " + std::to_string(s.size() + 1) + "바이트, 배열 " + std::to_string(cap) + "바이트)", line);
    for (size_t k = 0; k < cap; ++k) {
        Value& e = a->data[off + k];
        e.k = VK::Int;
        e.i = wrapInt(k < s.size() ? (unsigned char)s[k] : 0, isIntBT(e.t) ? e.t : BT::I8);
    }
}

static void initStruct(Ctx& c, Value& sv, const InitNode& list, int line);

static void setElem(Ctx& c, Value& e, const InitNode& n, int line) {
    if (n.isList) {
        if (e.k == VK::Struct) initStruct(c, e, n, line);
        else if (e.k == VK::Arr) fillArray(c, arrOf(e), 0, (size_t)e.i, n, line);
        else if (!n.kids.empty()) setElem(c, e, n.kids[0], line);
        return;
    }
    Value v = n.e->eval(c);
    if (e.k == VK::Arr && e.t != BT::Ptr) {
        ArrObj* a = arrOf(e);
        if (isStrLike(v) && (a->elem == BT::I8 || a->elem == BT::U8) && a->dims.size() == 1) {
            writeChars(a, 0, a->data.size(), cstrOf(v), line);
            return;
        }
        rtFail("배열 멤버는 { } 목록으로 초기화해야 합니다", line);
    }
    assignTo(e, v, line);
}

static void initStruct(Ctx& c, Value& sv, const InitNode& list, int line) {
    StructObj* so = structOf(sv);
    if (list.kids.size() > so->f.size()) rtFail("구조체 초기값이 멤버 수보다 많습니다", line);
    for (size_t k = 0; k < list.kids.size(); ++k) setElem(c, so->f[k], list.kids[k], line);
}

static void fillArray(Ctx& c, ArrObj* a, size_t L, size_t off, const InitNode& list, int line) {
    size_t nd = a->dims.size();
    size_t dim = (size_t)a->dims[L], st = a->strides[L];
    if (L + 1 == nd) {
        if (list.kids.size() > dim) rtFail("초기값 개수(" + std::to_string(list.kids.size()) + ")가 배열 크기(" + std::to_string(dim) + ")보다 많습니다", line);
        for (size_t k = 0; k < list.kids.size(); ++k) setElem(c, a->data[off + k], list.kids[k], line);
        return;
    }
    bool charRows = (a->elem == BT::I8 || a->elem == BT::U8) && L + 2 == nd;
    bool rows = false;
    for (const InitNode& kd : list.kids) if (kd.isList) rows = true;
    if (rows || charRows) {
        if (list.kids.size() > dim) rtFail("초기값 행 개수(" + std::to_string(list.kids.size()) + ")가 배열 크기(" + std::to_string(dim) + ")보다 많습니다", line);
        for (size_t k = 0; k < list.kids.size(); ++k) {
            const InitNode& kd = list.kids[k];
            if (kd.isList) fillArray(c, a, L + 1, off + k * st, kd, line);
            else if (charRows) {
                Value s = kd.e->eval(c);
                if (!isStrLike(s)) rtFail("문자열 초기값이 필요합니다", line);
                writeChars(a, off + k * st, st, cstrOf(s), line);
            } else rtFail("초기값 목록 형식이 잘못되었습니다", line);
        }
        return;
    }
    size_t total = dim * st;
    if (list.kids.size() > total) rtFail("초기값이 배열 크기보다 많습니다", line);
    for (size_t k = 0; k < list.kids.size(); ++k) setElem(c, a->data[off + k], list.kids[k], line);
}

static Value buildArray(Ctx& c, DeclItem& it) {
    std::vector<int> dims(it.dims.size(), 0);
    for (size_t k = 0; k < it.dims.size(); ++k) {
        if (!it.dims[k]) continue;
        int64_t n = toI(it.dims[k]->eval(c), it.line);
        if (n <= 0 || n > 100000)
            rtFail("배열 크기가 잘못되었습니다: " + it.name + "[" + std::to_string(n) + "]", it.line);
        dims[k] = (int)n;
    }
    if (!it.dims.empty() && !it.dims[0]) {
        size_t inner = 1;
        for (size_t k = 1; k < dims.size(); ++k) inner *= (size_t)dims[k];
        int n = 0;
        if (it.init == DeclItem::List) {
            bool rows = false;
            for (const InitNode& kd : it.list.kids) if (kd.isList) rows = true;
            bool charRows = (it.type.bt == BT::I8 || it.type.bt == BT::U8) && dims.size() == 2;
            if (dims.size() == 1 || rows || charRows) n = (int)it.list.kids.size();
            else n = (int)((it.list.kids.size() + inner - 1) / inner);
        } else if (it.init == DeclItem::Expr_) {
            Value s = it.initExpr->eval(c);
            if (!isStrLike(s)) rtFail("배열 크기를 알 수 없습니다: " + it.name, it.line);
            n = (int)cstrOf(s).size() + 1;
        } else rtFail("배열 크기를 알 수 없습니다: " + it.name + "[]", it.line);
        if (n <= 0) rtFail("빈 배열은 만들 수 없습니다: " + it.name, it.line);
        dims[0] = n;
    }
    size_t total = 1;
    for (int d : dims) total *= (size_t)d;
    if (total > 200000) rtFail("배열이 너무 큽니다: " + it.name + " (Uno 의 RAM 은 2KB 입니다)", it.line);
    Value arr = makeArray(c, it.type, dims);
    ArrObj* a = arrOf(arr);
    if (it.type.bt == BT::Obj)
        for (size_t k = 0; k < a->data.size(); ++k) a->data[k] = constructObj(c, it.type.cls, nullptr, it.line);
    if (it.init == DeclItem::List) fillArray(c, a, 0, 0, it.list, it.line);
    else if (it.init == DeclItem::Expr_) {
        Value s = it.initExpr->eval(c);
        if (isStrLike(s) && (a->elem == BT::I8 || a->elem == BT::U8) && dims.size() == 1)
            writeChars(a, 0, a->data.size(), cstrOf(s), it.line);
        else rtFail("배열은 { } 목록이나 문자열로 초기화해야 합니다: " + it.name, it.line);
    }
    return arr;
}

Flow DeclStmt::exec(Ctx& c) {
    for (DeclItem& it : items) {
        if (it.staticIdx >= 0) {
            if (c.staticDone[(size_t)it.staticIdx]) continue;
            c.staticDone[(size_t)it.staticIdx] = 1;
        }
        Value& slot = it.global ? c.G[it.slot] : c.fp[it.slot];
        if (it.isRef) {
            if (!it.initExpr || !it.initExpr->lvalue) rtFail("참조 변수 '" + it.name + "' 는 변수로 초기화해야 합니다", it.line);
            Value r;
            r.k = VK::Ref;
            r.p = it.initExpr->ref(c);
            slot = r;
            continue;
        }
        if (it.isArray) {
            slot = buildArray(c, it);
            continue;
        }
        Value v = makeDefault(c, it.type);
        switch (it.init) {
        case DeclItem::Expr_:
            assignTo(v, it.initExpr->eval(c), it.line);
            break;
        case DeclItem::List:
            if (v.k == VK::Struct) initStruct(c, v, it.list, it.line);
            else if (!it.list.kids.empty()) setElem(c, v, it.list.kids[0], it.line);
            break;
        case DeclItem::Ctor:
            if (it.type.bt == BT::Obj) break;
            if (it.type.bt == BT::Str) assignTo(v, constructString(c, it.ctorArgs, it.line), it.line);
            else if (!it.ctorArgs.empty()) assignTo(v, it.ctorArgs[0]->eval(c), it.line);
            break;
        default:
            break;
        }
        if (it.type.bt == BT::Obj && it.init != DeclItem::Expr_)
            v = constructObj(c, it.type.cls, it.init == DeclItem::Ctor ? &it.ctorArgs : nullptr, it.line);
        slot = std::move(v);
    }
    return F_NORMAL;
}

} // namespace interp
} // namespace ard
