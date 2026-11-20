// SketchInterpreter.h
// Arduino 스케치(.ino) 코드를 해석/실행하는 트리 워킹 인터프리터 (순수 C++17, Qt 의존성 없음)
#pragma once
#include "ArduinoHost.h"
#include <memory>
#include <string>

namespace ard {

class SketchInterpreter {
public:
    explicit SketchInterpreter(ArduinoHost* host);
    ~SketchInterpreter();
    // Parse + semantic setup. false on error; errorOut = Korean message, errorLine = 1-based line (0 if unknown)
    bool compile(const std::string& sourceUtf8, std::string& errorOut, int& errorLine);
    // Runs global initializers, setup(), then loop() forever until host->stopRequested().
    // Returns true on normal stop, false on runtime error (errorOut/errorLine filled, Korean).
    bool run(std::string& errorOut, int& errorLine);
private:
    struct Impl; std::unique_ptr<Impl> d;
};

} // namespace ard
