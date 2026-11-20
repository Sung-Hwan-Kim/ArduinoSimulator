// InterpLexer.h - 전처리기(#define/#if...) + 토크나이저
#pragma once
#include "InterpCore.h"

namespace ard {
namespace interp {

enum class TT : uint8_t { End, Ident, Int, Float, Char, Str, Punct };

struct Token {
    TT type = TT::End;
    std::string text;     // Ident/Punct: 원문, Str: 이스케이프 해석된 내용, 숫자: 원문
    int line = 0;
    int64_t ival = 0;     // Int/Char
    double fval = 0;      // Float
    BT nt = BT::I16;      // 숫자 리터럴 타입
};

// 소스 전체를 전처리하고 토큰열을 만든다. 마지막은 TT::End. 오류 시 CompileError 를 던진다.
std::vector<Token> preprocess(const std::string& src);

} // namespace interp
} // namespace ard
