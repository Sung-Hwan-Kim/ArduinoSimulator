// InterpCore.h - 인터프리터 내부 공통 정의 (값, 타입, 노드 기반 클래스, 실행 컨텍스트)
#pragma once
#include "ArduinoHost.h"
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace ard {
namespace interp {

// 기본 타입 (Uno 기준: int=16bit, long=32bit, float=double=32bit)
enum class BT : uint8_t { Void, Bool, I8, U8, I16, U16, I32, U32, I64, U64, F32, F64, Str, Ptr, Struct, Obj };
// 런타임 값 종류
enum class VK : uint8_t { Void, Int, Float, Str, Arr, Ptr, Ref, Struct, Obj, Func };

inline bool isIntBT(BT t) { return t >= BT::Bool && t <= BT::U64; }
inline bool isFloatBT(BT t) { return t == BT::F32 || t == BT::F64; }
inline bool isUnsignedBT(BT t) {
    return t == BT::Bool || t == BT::U8 || t == BT::U16 || t == BT::U32 || t == BT::U64;
}

struct FuncDef;
struct Program;
struct Ctx;

struct HeapObj { virtual ~HeapObj() = default; };

struct Value {
    VK k = VK::Void;
    BT t = BT::Void;     // 선언 타입 (대입 시 변환 기준)
    uint8_t lvl = 0;     // Arr: 현재 차원 레벨
    union { int64_t i; double f; Value* p; FuncDef* fn; };   // Arr: i = 요소 오프셋
    std::shared_ptr<HeapObj> h;
    Value() : i(0) {}
    static Value I(int64_t v, BT t) { Value r; r.k = VK::Int; r.t = t; r.i = v; return r; }
    static Value F(double v, BT t = BT::F32) { Value r; r.k = VK::Float; r.t = t; r.f = v; return r; }
    static Value B(bool b) { return I(b ? 1 : 0, BT::Bool); }
    static Value S(std::string s, BT t = BT::Str);   // t=Str: String 객체, t=Ptr: char* 문자열
};

struct StrObj : HeapObj { std::string s; explicit StrObj(std::string v) : s(std::move(v)) {} };
struct ArrObj : HeapObj {
    BT elem = BT::I16;
    int structId = -1;
    std::vector<int> dims;
    std::vector<size_t> strides;   // strides[l] = l 레벨 인덱스 1 증가 시 요소 수
    std::vector<Value> data;
};
struct StructObj : HeapObj { int sid = -1; std::vector<Value> f; };
struct LibObj : HeapObj { std::string cls; int id = -1; };

inline Value Value::S(std::string s, BT t) {
    Value r; r.k = VK::Str; r.t = t; r.h = std::make_shared<StrObj>(std::move(s)); return r;
}
inline const std::string& strOf(const Value& v) { return static_cast<StrObj*>(v.h.get())->s; }
inline ArrObj* arrOf(const Value& v) { return static_cast<ArrObj*>(v.h.get()); }
inline StructObj* structOf(const Value& v) { return static_cast<StructObj*>(v.h.get()); }
inline LibObj* libOf(const Value& v) { return static_cast<LibObj*>(v.h.get()); }

// 예외
struct RtError { std::string msg; int line; };
struct StopSignal {};
struct CompileError { std::string msg; int line; };
[[noreturn]] void rtFail(const std::string& msg, int line);

// 타입 정보
struct TypeInfo {
    BT bt = BT::I16;
    int structId = -1;
    std::string cls;       // 라이브러리 클래스명 (bt == Obj)
    bool isConst = false;
};

struct FieldDef { std::string name; TypeInfo type; std::vector<int> dims; };
struct StructDef { std::string name; std::vector<FieldDef> fields; };

// AST 기반
struct Expr {
    int line = 0;
    bool lvalue = false;
    virtual ~Expr() = default;
    virtual Value eval(Ctx& c) = 0;
    virtual Value* ref(Ctx& c);
    virtual bool isConst() const { return false; }
};
using ExprP = std::unique_ptr<Expr>;

enum Flow { F_NORMAL = 0, F_BREAK, F_CONTINUE, F_RETURN };
struct Stmt {
    int line = 0;
    virtual ~Stmt() = default;
    virtual Flow exec(Ctx& c) = 0;
};
using StmtP = std::unique_ptr<Stmt>;

struct ParamDef {
    std::string name;
    TypeInfo type;
    bool isRef = false;
    bool isArray = false;
    ExprP defVal;
};

struct FuncDef {
    std::string name;
    TypeInfo ret;
    std::vector<ParamDef> params;
    StmtP body;
    int nslots = 0;
    int line = 0;
    size_t minArgs() const {
        size_t n = params.size();
        while (n > 0 && params[n - 1].defVal) --n;
        return n;
    }
};

struct Program {
    std::vector<std::unique_ptr<FuncDef>> funcs;
    std::unordered_map<std::string, std::vector<FuncDef*>> funcByName;
    std::vector<StmtP> globalInit;
    int nglobals = 0;
    int nstatics = 0;
    std::vector<StructDef> structs;
    FuncDef* setupFn = nullptr;
    FuncDef* loopFn = nullptr;
};

// 실행 컨텍스트 (run() 마다 새로 생성)
struct Ctx {
    ArduinoHost* host = nullptr;
    Program* prog = nullptr;
    std::vector<Value> globals;
    Value* G = nullptr;
    std::vector<Value> stack;
    Value* fp = nullptr;
    size_t sp = 0;
    int depth = 0;
    Value ret;
    uint32_t ticks = 0;
    bool intEnabled = true;
    bool inIsr = false;
    int isrCount = 0;
    std::vector<FuncDef*> isr;
    std::vector<uint8_t> staticDone;
    uint32_t randState = 1;
    long serialTimeout = 1000;
    int nextInstance = 0;
    std::vector<Value> eeprom;

    void pollIrq();
    inline void tick() {
        if (host->stopRequested()) throw StopSignal();
        if (isrCount && intEnabled && !inIsr && ((++ticks & 15) == 0)) pollIrq();
    }
    void checkStop() { if (host->stopRequested()) throw StopSignal(); }
    void delayUs(uint64_t us);
};

// ---- InterpValue.cpp ----
enum Op { OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_MOD, OP_AND, OP_OR, OP_XOR, OP_SHL, OP_SHR,
          OP_EQ, OP_NE, OP_LT, OP_GT, OP_LE, OP_GE };
enum UOp { U_NEG, U_PLUS, U_NOT, U_BITNOT };

int btSize(BT t);
BT promoteBT(BT t);
int64_t wrapInt(int64_t v, BT t);
int64_t dblToInt(double d);
int64_t toI(const Value& v, int line);
double toD(const Value& v, int line);
bool truthy(const Value& v);
bool isNum(const Value& v);
bool isStrLike(const Value& v);         // Str 또는 char 배열
std::string cstrOf(const Value& v);      // char 배열/문자열 -> std::string
std::string toStr(const Value& v);       // String 변환 규칙 (연결 연산)
std::string& mutStr(Value& v);           // String 변경용 (공유 시 복제)
void assignTo(Value& d, const Value& s, int line);
Value deepCopy(const Value& v);
Value binop(int op, const Value& a, const Value& b, int line);
Value unop(int op, const Value& a, int line);
std::string fmtInteger(int64_t v, BT t, int base);
std::string fmtFloat(double v, int digits);
std::string printText(const Value& v, int line);
std::string printText2(const Value& v, int arg, int line);
std::string formatPrintf(const std::string& fmt, const std::vector<Value>& args, size_t first, int line);
void writeCStr(Value& arr, const std::string& s, int line, const char* fn);
int sizeOfValue(const Value& v, const Program& p);
int sizeOfType(const TypeInfo& t, const Program& p);
Value makeDefault(Ctx& c, const TypeInfo& ti);
Value makeArray(Ctx& c, const TypeInfo& elem, const std::vector<int>& dims);
LibValue toLib(const Value& v);
Value fromLib(const LibValue& v);

// ---- InterpNodes.cpp ----
Value invoke(Ctx& c, FuncDef* f, std::vector<ExprP>* args, int line);

// ---- InterpBuiltins.cpp ----
int findBuiltin(const std::string& name, int& minArgs, int& maxArgs);
Value callBuiltin(Ctx& c, int id, std::vector<ExprP>& args, int line);

// ---- InterpObjects.cpp ----
int findSerialMethod(const std::string& name, int& minArgs, int& maxArgs);
Value callSerial(Ctx& c, int id, std::vector<ExprP>& args, int line);
int findStringMethod(const std::string& name);
Value callStringMethod(Ctx& c, Value* recv, int id, const std::string& name, std::vector<ExprP>& args, int line);
int findEepromMethod(const std::string& name);
Value callEeprom(Ctx& c, int id, std::vector<ExprP>& args, int line);
Value callLibrary(Ctx& c, const std::string& cls, int id, const std::string& method,
                  std::vector<ExprP>& args, int line);
Value constructString(Ctx& c, std::vector<ExprP>& args, int line);

} // namespace interp
} // namespace ard
