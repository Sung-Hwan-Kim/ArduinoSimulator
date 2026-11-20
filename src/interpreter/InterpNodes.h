// InterpNodes.h - AST 노드 (식/문장)
#pragma once
#include "InterpCore.h"

namespace ard {
namespace interp {

// ===================== 식 =====================
struct LitExpr : Expr {
    Value v;
    std::string constName;   // const 변수에서 접힌 경우 변수 이름
    Value eval(Ctx&) override { return v; }
    bool isConst() const override { return true; }
};

struct LocalVar : Expr {
    int slot = 0;
    std::string nm;
    bool isConstVar = false;
    LocalVar() { lvalue = true; }
    Value* ref(Ctx& c) override { Value* p = c.fp + slot; return p->k == VK::Ref ? p->p : p; }
    Value eval(Ctx& c) override { return *ref(c); }
};

struct GlobalVar : Expr {
    int slot = 0;
    std::string nm;
    bool isConstVar = false;
    GlobalVar() { lvalue = true; }
    Value* ref(Ctx& c) override { Value* p = c.G + slot; return p->k == VK::Ref ? p->p : p; }
    Value eval(Ctx& c) override { return *ref(c); }
};

struct AssignExpr : Expr {
    ExprP target, value;
    Value eval(Ctx& c) override;
};

struct CompoundExpr : Expr {
    int op = 0;
    ExprP target, value;
    Value eval(Ctx& c) override;
};

struct IncDecExpr : Expr {
    ExprP target;
    bool pre = true;
    int delta = 1;
    Value eval(Ctx& c) override;
};

struct BinExpr : Expr {
    int op = 0;
    ExprP a, b;
    Value eval(Ctx& c) override { Value x = a->eval(c); return binop(op, x, b->eval(c), line); }
    bool isConst() const override { return a->isConst() && b->isConst(); }
};

struct LogicExpr : Expr {
    bool isAnd = true;
    ExprP a, b;
    Value eval(Ctx& c) override {
        bool x = truthy(a->eval(c));
        if (isAnd) return Value::B(x && truthy(b->eval(c)));
        return Value::B(x || truthy(b->eval(c)));
    }
    bool isConst() const override { return a->isConst() && b->isConst(); }
};

struct UnExpr : Expr {
    int op = 0;
    ExprP a;
    Value eval(Ctx& c) override { return unop(op, a->eval(c), line); }
    bool isConst() const override { return a->isConst(); }
};

struct TernExpr : Expr {
    ExprP cond, a, b;
    Value eval(Ctx& c) override;
    bool isConst() const override { return cond->isConst() && a->isConst() && b->isConst(); }
};

struct CastExpr : Expr {
    TypeInfo type;
    ExprP a;
    Value eval(Ctx& c) override;
    bool isConst() const override { return a->isConst(); }
};

struct CommaExpr : Expr {
    ExprP a, b;
    Value eval(Ctx& c) override { a->eval(c); return b->eval(c); }
};

struct IndexExpr : Expr {
    ExprP base, idx;
    std::string nm;
    bool baseByValue = false;
    IndexExpr() { lvalue = true; }
    Value eval(Ctx& c) override;
    Value* ref(Ctx& c) override;
    // 결과: 요소 포인터 (sub 가 채워지면 하위 배열)
    Value* locate(Ctx& c, Value& tmp, Value& sub, bool& isSub, bool& isChar, int64_t& ch);
};

struct FieldExpr : Expr {
    ExprP base;
    std::string field;
    int csid = -1, cidx = -1;
    FieldExpr() { lvalue = true; }
    Value eval(Ctx& c) override;
    Value* ref(Ctx& c) override;
    Value* fieldOf(Ctx& c, Value* s);
};

struct AddrExpr : Expr {
    ExprP a;
    Value eval(Ctx& c) override;
};

struct DerefExpr : Expr {
    ExprP a;
    DerefExpr() { lvalue = true; }
    Value eval(Ctx& c) override;
    Value* ref(Ctx& c) override;
};

struct CallExpr : Expr {
    std::string nm;
    std::vector<ExprP> args;
    FuncDef* fn = nullptr;
    int builtin = -1;
    Value eval(Ctx& c) override {
        if (fn) return invoke(c, fn, &args, line);
        return callBuiltin(c, builtin, args, line);
    }
};

struct FuncRefExpr : Expr {
    std::string nm;
    FuncDef* fn = nullptr;
    Value eval(Ctx&) override { Value v; v.k = VK::Func; v.t = BT::Void; v.fn = fn; return v; }
};

// recv.method(args): 런타임에 String / 라이브러리 객체 / 구조체 판별
struct MethodExpr : Expr {
    ExprP recv;
    std::string method;
    int strMethod = -1;
    std::vector<ExprP> args;
    Value eval(Ctx& c) override;
};

struct SerialExpr : Expr {
    int method = 0;
    std::vector<ExprP> args;
    Value eval(Ctx& c) override { return callSerial(c, method, args, line); }
};

struct EepromExpr : Expr {
    int method = 0;
    std::vector<ExprP> args;
    Value eval(Ctx& c) override { return callEeprom(c, method, args, line); }
};

struct EepromIndexExpr : Expr {
    ExprP idx;
    EepromIndexExpr() { lvalue = true; }
    Value eval(Ctx& c) override { return *ref(c); }
    Value* ref(Ctx& c) override;
};

// Wire.begin() 처럼 선언 없이 쓰는 전역 라이브러리 객체
struct GlobalObjExpr : Expr {
    std::string obj, method;
    std::vector<ExprP> args;
    Value eval(Ctx& c) override { return callLibrary(c, obj, -1, method, args, line); }
};

// 전역 라이브러리 객체를 값으로 사용 (&Wire 등)
struct ObjValueExpr : Expr {
    Value v;
    Value eval(Ctx&) override { return v; }
};

struct SizeofExpr : Expr {
    ExprP a;
    Value eval(Ctx& c) override;
};

// ===================== 문장 =====================
struct ExprStmt : Stmt {
    ExprP e;
    Flow exec(Ctx& c) override { e->eval(c); return F_NORMAL; }
};

struct EmptyStmt : Stmt {
    Flow exec(Ctx&) override { return F_NORMAL; }
};

struct BlockStmt : Stmt {
    std::vector<StmtP> body;
    Flow exec(Ctx& c) override {
        for (auto& s : body) {
            c.tick();
            Flow f = s->exec(c);
            if (f != F_NORMAL) return f;
        }
        return F_NORMAL;
    }
};

struct IfStmt : Stmt {
    ExprP cond;
    StmtP a, b;
    Flow exec(Ctx& c) override {
        if (truthy(cond->eval(c))) return a->exec(c);
        if (b) return b->exec(c);
        return F_NORMAL;
    }
};

struct WhileStmt : Stmt {
    ExprP cond;
    StmtP body;
    Flow exec(Ctx& c) override;
};

struct DoStmt : Stmt {
    ExprP cond;
    StmtP body;
    Flow exec(Ctx& c) override;
};

struct ForStmt : Stmt {
    StmtP init;
    ExprP cond, step;
    StmtP body;
    Flow exec(Ctx& c) override;
};

struct SwitchStmt : Stmt {
    ExprP cond;
    std::vector<StmtP> body;
    std::vector<std::pair<ExprP, int>> cases;   // (값 식, body 인덱스)
    int defIdx = -1;
    bool cached = false;
    std::vector<int64_t> cvals;
    Flow exec(Ctx& c) override;
};

struct BreakStmt : Stmt { Flow exec(Ctx&) override { return F_BREAK; } };
struct ContinueStmt : Stmt { Flow exec(Ctx&) override { return F_CONTINUE; } };

struct ReturnStmt : Stmt {
    ExprP e;
    Flow exec(Ctx& c) override {
        if (e) c.ret = e->eval(c); else c.ret = Value();
        return F_RETURN;
    }
};

// 초기화 목록 { ... }
struct InitNode {
    ExprP e;
    std::vector<InitNode> kids;
    bool isList = false;
    int line = 0;
};

struct DeclItem {
    std::string name;
    int line = 0;
    bool global = false;
    int slot = 0;
    int staticIdx = -1;     // static 지역변수: 1회만 초기화
    TypeInfo type;
    std::vector<ExprP> dims;   // 배열 크기 식 (첫 차원이 비어 있으면 nullptr)
    bool isArray = false;
    bool isRef = false;
    enum InitKind { None, Expr_, List, Ctor } init = None;
    ExprP initExpr;
    InitNode list;
    std::vector<ExprP> ctorArgs;
};

struct DeclStmt : Stmt {
    std::vector<DeclItem> items;
    Flow exec(Ctx& c) override;
};

} // namespace interp
} // namespace ard
