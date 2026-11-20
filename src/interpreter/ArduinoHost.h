// ArduinoHost.h
// 스케치 인터프리터(SketchInterpreter)와 시뮬레이터(호스트) 사이의 인터페이스.
// Qt 의존성 없음 (순수 C++17). 모든 메서드는 인터프리터 스레드에서 호출된다.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ard {

// Arduino AVR core 와 동일한 값
enum PinModeValue { MODE_INPUT = 0, MODE_OUTPUT = 1, MODE_INPUT_PULLUP = 2 };
enum InterruptMode { INT_CHANGE = 1, INT_FALLING = 2, INT_RISING = 3, INT_LOW = 0 };

// 라이브러리 객체(Servo, DHT, LiquidCrystal_I2C 등) 호출 시 전달되는 값
struct LibValue {
    enum Kind { Int, Float, String, Void };
    Kind kind = Void;
    double num = 0.0;       // Int/Float 일 때 값
    std::string str;        // String 일 때 값 (UTF-8)

    static LibValue makeInt(long long v)            { LibValue r; r.kind = Int; r.num = (double)v; return r; }
    static LibValue makeFloat(double v)             { LibValue r; r.kind = Float; r.num = v; return r; }
    static LibValue makeString(const std::string& s){ LibValue r; r.kind = String; r.str = s; return r; }
    static LibValue makeVoid()                      { return LibValue(); }
};

class ArduinoHost {
public:
    virtual ~ArduinoHost() = default;

    // ---- GPIO ----
    virtual void pinMode(int pin, int mode) = 0;
    virtual void digitalWrite(int pin, int value) = 0;
    virtual int  digitalRead(int pin) = 0;               // 0 or 1
    virtual int  analogRead(int pin) = 0;                // pin: A0 같은 핀 번호(예: Uno 14) 또는 채널번호(0..7) 둘 다 허용 → 호스트가 해석. 0..1023
    virtual void analogWrite(int pin, int value) = 0;    // 0..255 (PWM)

    // ---- 시간 (마이크로초, 시뮬레이션 시작 기준) ----
    virtual uint64_t micros() = 0;
    virtual void delayMicros(uint64_t us) = 0;           // 블로킹. 정지 요청 시 즉시 반환해도 됨
    virtual bool stopRequested() = 0;                    // true 이면 인터프리터는 즉시 실행을 끝내야 함
    virtual void onLoopIteration() {}                    // loop() 1회 호출 직후 호출됨 (호스트가 양보/스로틀링 가능)

    // ---- Serial (Serial 객체는 인터프리터 내장, 실제 입출력은 호스트로) ----
    virtual void serialBegin(long baud) = 0;
    virtual void serialWrite(const std::string& bytes) = 0;  // print/println/write 결과 문자열
    virtual int  serialAvailable() = 0;
    virtual int  serialRead() = 0;                       // 없으면 -1
    virtual int  serialPeek() = 0;                       // 없으면 -1

    // ---- 기타 내장 함수 ----
    virtual void tone(int pin, unsigned int freq, unsigned long durationMs) = 0;  // durationMs==0 → 무한
    virtual void noTone(int pin) = 0;
    virtual unsigned long pulseIn(int pin, int state, unsigned long timeoutUs) = 0;

    // ---- 인터럽트 ----
    virtual int  digitalPinToInterrupt(int pin) = 0;     // 지원 안되면 -1
    virtual void attachInterrupt(int irq, int mode) = 0;
    virtual void detachInterrupt(int irq) = 0;
    virtual int  pollInterrupt() = 0;                    // 대기중인 인터럽트 번호, 없으면 -1 (가져가면 소거됨)

    // ---- 보드/라이브러리 상수 (A0, LED_BUILTIN, DHT22, SDA ...) ----
    virtual bool lookupConstant(const std::string& name, double& value) = 0;

    // ---- 라이브러리 객체 ----
    // className 이 라이브러리 클래스(Servo, DHT, LiquidCrystal_I2C ...)이면 true
    virtual bool isLibraryClass(const std::string& className) = 0;
    // 선언 없이 쓰는 전역 라이브러리 객체 (Wire, EEPROM 제외 등). true 면 libraryCall(className=name, instanceId=-1)
    virtual bool isGlobalObject(const std::string& name) = 0;
    // 객체 생성: "Servo s;" → args 비어있음, "DHT dht(2, DHT22);" → args = {2, 22}
    virtual void libraryConstruct(const std::string& className, int instanceId, const std::vector<LibValue>& args) = 0;
    // 메서드 호출: "dht.readTemperature()" → className="DHT", method="readTemperature"
    // 알 수 없는 메서드는 호스트가 Void 를 반환하거나 errorOut 을 채워 false 반환
    virtual bool libraryCall(const std::string& className, int instanceId, const std::string& method,
                             const std::vector<LibValue>& args, LibValue& result, std::string& errorOut) = 0;
};

} // namespace ard
