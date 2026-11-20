// SketchInterpreter.cpp - 공개 API 구현
#include "SketchInterpreter.h"
#include "InterpParser.h"
#include <exception>
#include <new>

namespace ard {

using namespace interp;

namespace {
const size_t kStackSlots = 32768;
const size_t kEepromSize = 1024;
}

struct SketchInterpreter::Impl {
    ArduinoHost* host = nullptr;
    std::unique_ptr<Program> prog;
    std::vector<Value> eeprom;   // 실행 간 유지 (실제 EEPROM 처럼)
};

SketchInterpreter::SketchInterpreter(ArduinoHost* host) : d(new Impl) { d->host = host; }

SketchInterpreter::~SketchInterpreter() = default;

bool SketchInterpreter::compile(const std::string& sourceUtf8, std::string& errorOut, int& errorLine) {
    d->prog.reset();
    errorOut.clear();
    errorLine = 0;
    try {
        std::vector<Token> toks = preprocess(sourceUtf8);
        auto prog = std::make_unique<Program>();
        Parser p(toks, *prog, d->host);
        p.parseProgram();
        d->prog = std::move(prog);
        return true;
    } catch (CompileError& e) {
        errorOut = e.msg;
        errorLine = e.line;
    } catch (RtError& e) {
        errorOut = e.msg;
        errorLine = e.line;
    } catch (std::bad_alloc&) {
        errorOut = "메모리가 부족합니다";
    } catch (std::exception& e) {
        errorOut = std::string("내부 오류: ") + e.what();
    }
    return false;
}

bool SketchInterpreter::run(std::string& errorOut, int& errorLine) {
    errorOut.clear();
    errorLine = 0;
    if (!d->prog) {
        errorOut = "컴파일된 스케치가 없습니다";
        return false;
    }
    Program& P = *d->prog;
    Ctx c;
    c.host = d->host;
    c.prog = &P;
    c.globals.assign((size_t)P.nglobals, Value());
    c.G = c.globals.data();
    c.stack.assign(kStackSlots, Value());
    c.fp = c.stack.data();
    c.sp = 0;
    c.staticDone.assign((size_t)P.nstatics, 0);
    if (d->eeprom.size() != kEepromSize) d->eeprom.assign(kEepromSize, Value::I(255, BT::U8));
    c.eeprom = std::move(d->eeprom);

    struct EepromKeeper {
        Impl* d;
        Ctx& c;
        ~EepromKeeper() { d->eeprom = std::move(c.eeprom); }
    } keeper{d.get(), c};

    try {
        for (auto& s : P.globalInit) {
            c.tick();
            s->exec(c);
        }
        invoke(c, P.setupFn, nullptr, P.setupFn->line);
        while (!d->host->stopRequested()) {
            invoke(c, P.loopFn, nullptr, P.loopFn->line);
            d->host->onLoopIteration();
            if (c.isrCount && c.intEnabled) c.pollIrq();
        }
        return true;
    } catch (StopSignal&) {
        return true;
    } catch (RtError& e) {
        errorOut = e.msg;
        errorLine = e.line;
    } catch (std::bad_alloc&) {
        errorOut = "메모리가 부족합니다 (배열이나 String 이 너무 큽니다)";
    } catch (std::exception& e) {
        errorOut = std::string("내부 오류: ") + e.what();
    }
    return false;
}

} // namespace ard
