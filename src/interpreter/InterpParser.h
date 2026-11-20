// InterpParser.h - 재귀 하강 파서 (토큰 -> AST, 이름 해석 포함)
#pragma once
#include "InterpLexer.h"
#include "InterpNodes.h"
#include <unordered_set>

namespace ard {
namespace interp {

bool mapNamedType(const std::string& w, BT& b);

class Parser {
public:
    Parser(std::vector<Token>& toks, Program& prog, ArduinoHost* host);
    void parseProgram();   // 실패 시 CompileError

private:
    struct VarInfo {
        bool global = false;
        int slot = 0;
        TypeInfo type;
        bool isArray = false;
        bool isRef = false;
        bool isPtrParam = false;
        bool hasConst = false;
        Value constVal;
    };

    std::vector<Token>& T;
    size_t pos = 0;
    Program& P;
    ArduinoHost* host;

    std::unordered_map<std::string, VarInfo> globals;
    std::vector<std::unordered_map<std::string, VarInfo>> scopes;
    FuncDef* curFn = nullptr;
    int nextSlot = 0;
    int loopDepth = 0, switchDepth = 0;
    int nest = 0;   // 재귀 하강 중첩 깊이 (스택 보호)
    struct NestGuard {
        Parser& p;
        explicit NestGuard(Parser& pp) : p(pp) {
            if (++p.nest > 300) p.failHere("식 또는 문장이 너무 깊게 중첩되었습니다");
        }
        ~NestGuard() { --p.nest; }
    };
    std::unordered_map<std::string, int64_t> enumConsts;
    std::unordered_map<std::string, int> structIds;
    std::unordered_map<std::string, TypeInfo> typedefs;
    std::unordered_set<std::string> enumTypes;
    std::unordered_map<std::string, bool> libCache;
    std::vector<CallExpr*> pendingCalls;
    std::vector<FuncRefExpr*> pendingRefs;

    // ---- 토큰 유틸 ----
    const Token& cur() const { return T[pos]; }
    const Token& peekTok(size_t n) const { return T[std::min(pos + n, T.size() - 1)]; }
    bool isPAt(size_t i, const char* s) const {
        const Token& t = T[std::min(i, T.size() - 1)];
        return t.type == TT::Punct && t.text == s;
    }
    bool isP(const char* s) const { return isPAt(pos, s); }
    bool isId(const char* s) const { return cur().type == TT::Ident && cur().text == s; }
    bool accept(const char* s) { if (isP(s)) { ++pos; return true; } return false; }
    int prevLine() const { return pos > 0 ? T[pos - 1].line : cur().line; }
    void expect(const char* s);
    std::string expectIdent(const char* what);
    [[noreturn]] void fail(const std::string& msg, int line);
    [[noreturn]] void failHere(const std::string& msg) { fail(msg, cur().line); }
    std::string tokDesc(const Token& t) const;

    // ---- 타입 ----
    bool isLibClass(const std::string& n);
    bool isTypeName(size_t at);
    bool parseType(TypeInfo& ti, bool& isStatic);
    void parseStructBody(int sid);
    int parseStructDef();          // 'struct' 다음 위치에서 호출, sid 반환
    void parseEnumDef();           // 'enum' 다음 위치에서 호출
    void parseTypedef();

    // ---- 선언/함수 ----
    void parseTopLevel();
    void parseFunction(const TypeInfo& ret, const std::string& name, int line);
    std::unique_ptr<DeclStmt> parseDeclaration(bool global);
    std::unique_ptr<DeclStmt> parseDeclaratorsAfterType(const TypeInfo& ti, bool isStatic, bool global, int line);
    void parseDeclarator(const TypeInfo& base, bool isStatic, bool global, DeclItem& it);
    void parseInitList(InitNode& n);
    void declareVar(const std::string& name, VarInfo& vi, int line);
    const VarInfo* findVar(const std::string& name) const;
    bool isDeclStart();

    // ---- 문장 ----
    StmtP parseStatement();
    StmtP parseSubStatement();
    StmtP parseBlock();

    // ---- 식 ----
    ExprP parseExpr();
    ExprP parseAssign();
    ExprP parseTernary();
    ExprP parseBinary(int minPrec);
    ExprP parseUnary();
    ExprP parsePostfix(ExprP e);
    ExprP parsePrimary();
    std::vector<ExprP> parseArgs();
    ExprP parseIdentifier();
    ExprP resolveIdent(const std::string& name, int line);
    ExprP makeLit(const Value& v, int line);
    bool constEval(Expr* e, Value& out);
    void checkAssignable(Expr* e, int line);
    ExprP makeBuiltinCall(const std::string& nm, std::vector<ExprP> args, int line);

    void finish();
};

} // namespace interp
} // namespace ard
