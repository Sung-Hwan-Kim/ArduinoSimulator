// interp_test.cpp - SketchInterpreter 콘솔 테스트 (모의 호스트)
#include "SketchInterpreter.h"
#include <chrono>
#include <cstdio>
#include <deque>
#include <functional>
#include <map>
#include <string>
#include <vector>

using namespace ard;

class MockHost : public ArduinoHost {
public:
    uint64_t t = 0;
    uint64_t stopAtUs = UINT64_MAX;
    int stopAfterLoops = -1;
    long stopAfterChecks = -1;
    int loops = 0;
    long checks = 0;
    bool stop = false;
    bool quiet = false;
    std::string serialOut, log;
    std::deque<int> serialIn;
    std::function<int(int)> dread;
    std::function<int(int)> aread;
    std::map<uint64_t, int> irqAt;   // 가상 시간 -> irq
    std::map<int, double> servoPos;

    void L(const std::string& s) { log += s + "\n"; if (!quiet) std::printf("    [%7.1fms] %s\n", t / 1000.0, s.c_str()); }
    static std::string args2s(const std::vector<LibValue>& a) {
        std::string r;
        for (size_t k = 0; k < a.size(); ++k) {
            if (k) r += ",";
            char b[64];
            if (a[k].kind == LibValue::Int) std::snprintf(b, sizeof b, "%lld", (long long)a[k].num);
            else if (a[k].kind == LibValue::Float) std::snprintf(b, sizeof b, "%gf", a[k].num);
            else if (a[k].kind == LibValue::String) { r += "\"" + a[k].str + "\""; continue; }
            else std::snprintf(b, sizeof b, "void");
            r += b;
        }
        return r;
    }
    void pinMode(int pin, int mode) override { L("pinMode(" + std::to_string(pin) + "," + std::to_string(mode) + ")"); }
    void digitalWrite(int pin, int v) override { L("D" + std::to_string(pin) + "=" + std::to_string(v) + "@" + std::to_string(t / 1000)); }
    int digitalRead(int pin) override { return dread ? dread(pin) : 0; }
    int analogRead(int pin) override { return aread ? aread(pin) : 0; }
    void analogWrite(int pin, int v) override { L("PWM" + std::to_string(pin) + "=" + std::to_string(v)); }
    uint64_t micros() override { return t; }
    void delayMicros(uint64_t us) override {
        uint64_t end = t + us;
        for (auto& kv : irqAt) if (kv.first > t && kv.first <= end) pendingIrq = kv.second;
        t = end;
        if (t >= stopAtUs) stop = true;
    }
    bool stopRequested() override {
        if (stopAfterChecks >= 0 && ++checks >= stopAfterChecks) stop = true;
        return stop;
    }
    void onLoopIteration() override { ++loops; if (stopAfterLoops >= 0 && loops >= stopAfterLoops) stop = true; }
    void serialBegin(long baud) override { L("Serial.begin(" + std::to_string(baud) + ")"); }
    void serialWrite(const std::string& s) override { serialOut += s; }
    int serialAvailable() override { return (int)serialIn.size(); }
    int serialRead() override { if (serialIn.empty()) return -1; int c = serialIn.front(); serialIn.pop_front(); return c; }
    int serialPeek() override { return serialIn.empty() ? -1 : serialIn.front(); }
    void tone(int pin, unsigned int f, unsigned long d) override { L("tone(" + std::to_string(pin) + "," + std::to_string(f) + "," + std::to_string(d) + ")"); }
    void noTone(int pin) override { L("noTone(" + std::to_string(pin) + ")"); }
    unsigned long pulseIn(int, int, unsigned long) override { return 1166; }
    int digitalPinToInterrupt(int pin) override { return pin == 2 ? 0 : pin == 3 ? 1 : -1; }
    void attachInterrupt(int irq, int mode) override { L("attachInterrupt(" + std::to_string(irq) + "," + std::to_string(mode) + ")"); }
    void detachInterrupt(int irq) override { L("detachInterrupt(" + std::to_string(irq) + ")"); }
    int pendingIrq = -1;
    int pollInterrupt() override { int r = pendingIrq; pendingIrq = -1; return r; }
    bool lookupConstant(const std::string& n, double& v) override {
        static const std::map<std::string, double> m = {
            {"A0", 14}, {"A1", 15}, {"A2", 16}, {"A3", 17}, {"A4", 18}, {"A5", 19}, {"LED_BUILTIN", 13},
            {"DHT11", 11}, {"DHT22", 22}, {"SDA", 18}, {"SCL", 19}};
        auto it = m.find(n);
        if (it == m.end()) return false;
        v = it->second;
        return true;
    }
    bool isLibraryClass(const std::string& c) override { return c == "Servo" || c == "DHT" || c == "LiquidCrystal_I2C"; }
    bool isGlobalObject(const std::string& n) override { return n == "Wire"; }
    void libraryConstruct(const std::string& cls, int id, const std::vector<LibValue>& a) override {
        L("construct " + cls + "#" + std::to_string(id) + "(" + args2s(a) + ")");
    }
    bool libraryCall(const std::string& cls, int id, const std::string& m, const std::vector<LibValue>& a,
                     LibValue& r, std::string& err) override {
        L(cls + "#" + std::to_string(id) + "." + m + "(" + args2s(a) + ")");
        if (cls == "Servo") {
            if (m == "attach" || m == "detach") { r = LibValue::makeVoid(); return true; }
            if (m == "write") { servoPos[id] = a.at(0).num; r = LibValue::makeVoid(); return true; }
            if (m == "read") { r = LibValue::makeInt((long long)servoPos[id]); return true; }
        } else if (cls == "DHT") {
            if (m == "begin") { r = LibValue::makeVoid(); return true; }
            if (m == "readTemperature") { r = LibValue::makeFloat(23.5); return true; }
            if (m == "readHumidity") { r = LibValue::makeFloat(45.2); return true; }
        } else if (cls == "LiquidCrystal_I2C" || cls == "Wire") {
            r = LibValue::makeVoid();
            return true;
        }
        err = cls + " 에는 '" + m + "' 함수가 없습니다";
        return false;
    }
};

static int g_pass = 0, g_fail = 0;

struct Run {
    bool compiled = false, ok = false;
    std::string err;
    int line = 0;
    double secs = 0;
};

static Run runSketch(const char* name, const std::string& src, MockHost& h) {
    std::printf("=== %s ===\n", name);
    Run r;
    SketchInterpreter in(&h);
    r.compiled = in.compile(src, r.err, r.line);
    if (!r.compiled) { std::printf("  compile error (line %d): %s\n", r.line, r.err.c_str()); return r; }
    auto t0 = std::chrono::steady_clock::now();
    r.ok = in.run(r.err, r.line);
    r.secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    if (!r.ok) std::printf("  runtime error (line %d): %s\n", r.line, r.err.c_str());
    if (!h.serialOut.empty()) {
        std::string s = h.serialOut;
        std::string out;
        for (char c : s) { if (c == '\r') continue; out += c; }
        std::printf("  serial:\n");
        size_t p = 0;
        while (p < out.size()) {
            size_t e = out.find('\n', p);
            if (e == std::string::npos) e = out.size();
            std::printf("    | %s\n", out.substr(p, e - p).c_str());
            p = e + 1;
        }
    }
    return r;
}

static void check(bool cond, const char* what) {
    if (cond) { ++g_pass; std::printf("  PASS: %s\n", what); }
    else { ++g_fail; std::printf("  FAIL: %s\n", what); }
}

static bool has(const std::string& hay, const std::string& needle) { return hay.find(needle) != std::string::npos; }

int main() {
    {
        MockHost h; h.stopAfterLoops = 2;
        Run r = runSketch("Blink", R"(
void setup() { pinMode(LED_BUILTIN, OUTPUT); }
void loop() {
  digitalWrite(LED_BUILTIN, HIGH);   // LED on
  delay(1000);
  digitalWrite(LED_BUILTIN, LOW);
  delay(1000);
})", h);
        check(r.compiled && r.ok, "blink runs");
        check(has(h.log, "D13=1@0") && has(h.log, "D13=0@1000") && has(h.log, "D13=1@2000") && has(h.log, "D13=0@3000"), "blink timing");
    }
    {
        MockHost h; h.stopAfterLoops = 6; h.quiet = true;
        h.dread = [&](int pin) { return (pin == 2 && h.t >= 20000 && h.t < 40000) ? 0 : 1; };
        Run r = runSketch("Button INPUT_PULLUP", R"(
const int buttonPin = 2;
const int ledPin = 13;
int buttonState = 0;
void setup() {
  pinMode(buttonPin, INPUT_PULLUP);
  pinMode(ledPin, OUTPUT);
  Serial.begin(9600);
}
void loop() {
  buttonState = digitalRead(buttonPin);
  if (buttonState == LOW) { digitalWrite(ledPin, HIGH); Serial.println("pressed"); }
  else { digitalWrite(ledPin, LOW); }
  delay(10);
})", h);
        check(r.ok && h.serialOut == "pressed\r\npressed\r\n", "button pressed twice");
        check(has(h.log, "pinMode(2,2)"), "INPUT_PULLUP == 2");
    }
    {
        MockHost h; h.stopAfterLoops = 3; h.quiet = true;
        int n = 0;
        h.aread = [&](int) { int v[] = {0, 512, 1023}; return v[n++ % 3]; };
        Run r = runSketch("analogRead->map->analogWrite", R"(
void setup() { Serial.begin(9600); }
void loop() {
  int v = analogRead(A0);
  int pwm = map(v, 0, 1023, 0, 255);
  analogWrite(9, pwm);
  Serial.print(v); Serial.print(" -> "); Serial.println(pwm);
  delay(100);
})", h);
        check(r.ok && h.serialOut == "0 -> 0\r\n512 -> 127\r\n1023 -> 255\r\n", "map output");
        check(has(h.log, "PWM9=255"), "analogWrite 255");
    }
    {
        MockHost h; h.stopAfterLoops = 1; h.quiet = true;
        Run r = runSketch("Functions/arrays/control/String/print formats", R"(
#define LED 13
#define SQUARE(x) ((x) * (x))
#ifdef ARDUINO
#define PLATFORM "uno"
#else
#define PLATFORM "pc"
#endif
enum Color { RED, GREEN = 5, BLUE };
struct Point { int x; int y; };
int values[] = {5, 3, 8, 1, 9, 2};
const int N = sizeof(values) / sizeof(values[0]);
const char* names[] = {"zero", "one", "two"};
int sumArr(int a[], int n) { int s = 0; for (int i = 0; i < n; i++) s += a[i]; return s; }
void bubble(int a[], int n) {
  for (int i = 0; i < n - 1; i++)
    for (int j = 0; j < n - 1 - i; j++)
      if (a[j] > a[j + 1]) { int t = a[j]; a[j] = a[j + 1]; a[j + 1] = t; }
}
int fact(int n) { return n <= 1 ? 1 : n * fact(n - 1); }
long fib(int n) { if (n < 2) return n; return fib(n - 1) + fib(n - 2); }
String describe(int v) {
  switch (v) {
    case 1: return "one";
    case 2:
    case 3: return "two-three";
    default: break;
  }
  return "many";
}
void swapRef(int &a, int &b) { int t = a; a = b; b = t; }
int addDef(int a, int b = 10) { return a + b; }
Point mid(Point a, Point b) { Point p; p.x = (a.x + b.x) / 2; p.y = (a.y + b.y) / 2; return p; }
void setup() {
  Serial.begin(9600);
  Serial.print("N="); Serial.println(N);
  Serial.println(sumArr(values, N));
  bubble(values, N);
  for (int i = 0; i < N; i++) { Serial.print(values[i]); Serial.print(i < N - 1 ? "," : "\n"); }
  Serial.println(fact(7));
  Serial.println(fib(15));
  int k = 0;
  while (k < 4) { Serial.print(describe(k + 1)); Serial.print(" "); k++; }
  Serial.println();
  String s = "Temp: "; s += 25; s += 'C'; s = s + " / " + 3.14159; Serial.println(s);
  Serial.println(255, HEX); Serial.println(255, BIN); Serial.println(8, OCT);
  Serial.println(3.14159, 4); Serial.println(2.5); Serial.println(-1, HEX);
  int x = 3, y = 4; swapRef(x, y); Serial.print(x); Serial.println(y);
  char c = 'A'; Serial.println(c); Serial.println(c + 1);
  int m[2][3] = {{1, 2, 3}, {4, 5, 6}}; int tot = 0;
  for (int i = 0; i < 2; i++) for (int j = 0; j < 3; j++) tot += m[i][j];
  Serial.println(tot);
  do { k--; } while (k > 0);
  Serial.println(k);
  String t = "Hello World";
  Serial.println(t.length()); Serial.println(t.indexOf('o')); Serial.println(t.substring(6));
  t.toUpperCase(); Serial.println(t); Serial.println(t.startsWith("HELLO"));
  String num = "123"; Serial.println(num.toInt() + 1);
  Serial.println(String(255, HEX)); Serial.println(String(3.14159, 3));
  float f = 10 / 4; Serial.println(f);
  float g = 10 / 4.0; Serial.println(g);
  Serial.println(7 % 3); Serial.println(1 << 4);
  unsigned long big = 4294967295UL; big++; Serial.println(big);
  byte b = 250; b += 10; Serial.println(b);
  Serial.println(constrain(150, 0, 100)); Serial.println(min(3, 7)); Serial.println(max(3.5, 2));
  Serial.println(abs(-5)); Serial.println(sq(4)); Serial.println(sqrt(16)); Serial.println(bitRead(5, 0));
  Serial.println(SQUARE(LED)); Serial.println(PLATFORM);
  Serial.println(BLUE); Serial.println(B1010); Serial.println(0b1010 | 0x10);
  Serial.println(names[2]); Serial.println(addDef(1)); Serial.println(addDef(1, 2));
  Point p1 = {2, 4}; Point p2 = {6, 8}; Point pm = mid(p1, p2);
  Serial.print(pm.x); Serial.print(","); Serial.println(pm.y);
  for (int i = 0; i < 10; i++) { if (i == 2) continue; if (i == 4) break; Serial.print(i); }
  Serial.println();
  char msg[] = "hi"; Serial.println(sizeof(msg)); Serial.println(msg);
  bool flag = true; Serial.println(flag); Serial.println(!flag);
  Serial.println(10 > 3 && 2 > 1 ? "yes" : "no");
  long ms = 60 * 1000L; Serial.println(ms);
  int neg = -7; Serial.println(neg / 2); Serial.println(neg % 3);
  Serial.println((int)3.99); Serial.println((byte)300); Serial.println(int(2.5 * 2));
  String a1 = "abc"; String a2 = "abd"; Serial.println(a1 == "abc"); Serial.println(a1 < a2);
  String r2 = "a,b,c"; r2.replace(",", ";"); Serial.println(r2);
  String tr = "  pad  "; tr.trim(); Serial.print("["); Serial.print(tr); Serial.println("]");
  Serial.println(String("x") + 1 + 2);
  Serial.println(F("flash string"));
}
void loop() {}
)", h);
        const char* expect =
            "N=6\r\n28\r\n1,2,3,5,8,9\n5040\r\n610\r\none two-three two-three many \r\n"
            "Temp: 25C / 3.14\r\nFF\r\n11111111\r\n10\r\n3.1416\r\n2.50\r\nFFFFFFFF\r\n43\r\nA\r\n66\r\n"
            "21\r\n0\r\n11\r\n4\r\nWorld\r\nHELLO WORLD\r\n1\r\n124\r\nff\r\n3.142\r\n2.00\r\n2.50\r\n"
            "1\r\n16\r\n0\r\n4\r\n100\r\n3\r\n3.50\r\n5\r\n16\r\n4.00\r\n1\r\n169\r\nuno\r\n6\r\n10\r\n26\r\n"
            "two\r\n11\r\n3\r\n4,6\r\n013\r\n3\r\nhi\r\n1\r\n0\r\nyes\r\n60000\r\n-3\r\n-1\r\n3\r\n44\r\n5\r\n"
            "1\r\n1\r\na;b;c\r\n[pad]\r\nx12\r\nflash string\r\n";
        check(r.ok, "big sketch runs");
        check(h.serialOut == expect, "big sketch exact serial output");
        if (h.serialOut != expect) {
            std::string a = h.serialOut, b = expect;
            size_t i = 0;
            while (i < a.size() && i < b.size() && a[i] == b[i]) ++i;
            std::printf("  first diff at %zu: got '%s' expected '%s'\n", i, a.substr(i, 30).c_str(), b.substr(i, 30).c_str());
        }
    }
    {
        MockHost h; h.stopAfterLoops = 1;
        Run r = runSketch("Servo sweep", R"(
#include <Servo.h>
Servo myservo;
Servo servos[2];
int pos = 0;
void setup() { myservo.attach(9); servos[1].attach(10); }
void loop() {
  for (pos = 0; pos <= 180; pos += 90) { myservo.write(pos); delay(15); }
  for (pos = 180; pos >= 0; pos -= 90) { myservo.write(pos); delay(15); }
  servos[1].write(myservo.read() + 45);
})", h);
        check(r.ok && has(h.log, "Servo#0.write(90)") && has(h.log, "Servo#0.write(180)") && has(h.log, "Servo#2.write(45)"), "servo calls");
    }
    {
        MockHost h; h.stopAfterLoops = 1;
        Run r = runSketch("DHT", R"(
#include "DHT.h"
#define DHTPIN 2
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);
LiquidCrystal_I2C lcd(0x27, 16, 2);
void setup() { Serial.begin(9600); dht.begin(); lcd.init(); Wire.begin(); }
void loop() {
  delay(2000);
  float h = dht.readHumidity();
  float t = dht.readTemperature();
  if (isnan(h) || isnan(t)) { Serial.println("Failed"); return; }
  Serial.print("Humidity: "); Serial.print(h); Serial.print(" %\t");
  Serial.print("Temperature: "); Serial.print(t); Serial.println(" *C");
  lcd.setCursor(0, 1); lcd.print(t, 1); lcd.print((char)223); lcd.print("C");
})", h);
        check(r.ok && has(h.log, "construct DHT#0(2,22)") && has(h.log, "construct LiquidCrystal_I2C#1(39,16,2)"), "DHT/LCD construct");
        check(h.serialOut == "Humidity: 45.20 %\tTemperature: 23.50 *C\r\n", "DHT print");
        check(has(h.log, "LiquidCrystal_I2C#1.print(23.5f,1)") && has(h.log, "Wire#-1.begin()"), "LCD print float + Wire");
    }
    {
        MockHost h; h.stopAfterLoops = 1; h.quiet = true;
        Run r = runSketch("sprintf/dtostrf", R"(
char buf[40];
void setup() {
  Serial.begin(9600);
  int hh = 5, mm = 7;
  sprintf(buf, "%02d:%02d", hh, mm); Serial.println(buf);
  sprintf(buf, "%5d|%-4d|%x|%X|%c|%s|%u", 42, 7, 255, 255, 'Z', "ok", -1); Serial.println(buf);
  sprintf(buf, "%.2f %ld %lu %%", 3.14159, 100000L, 4000000000UL); Serial.println(buf);
  char t[10]; dtostrf(3.14159, 6, 2, t); Serial.println(t);
  Serial.println(strlen(buf));
  char name[16]; strcpy(name, "Ardu"); strcat(name, "ino"); Serial.println(name);
  Serial.println(strcmp(name, "Arduino") == 0);
  Serial.println(atoi("42") + 1);
  char nb[8]; itoa(255, nb, 16); Serial.println(nb);
}
void loop() {})", h);
        check(r.ok && h.serialOut == "05:07\r\n   42|7   |ff|FF|Z|ok|65535\r\n3.14 100000 4000000000 %\r\n  3.14\r\n24\r\nArduino\r\n1\r\n43\r\nff\r\n", "sprintf output");
    }
    {
        MockHost h; h.stopAfterLoops = 3; h.quiet = true;
        Run r = runSketch("static locals", R"(
int counter() { static int n = 0; n++; return n; }
void setup() { Serial.begin(9600); }
void loop() { Serial.println(counter()); })", h);
        check(r.ok && h.serialOut == "1\r\n2\r\n3\r\n", "static local persists");
    }
    {
        MockHost h; h.stopAfterLoops = 1; h.quiet = true;
        Run r = runSketch("int16 overflow wrap", R"(
void setup() {
  Serial.begin(9600);
  int x = 32767; x++; Serial.println(x);
  int a = 300; int b = a * 200; Serial.println(b);
  long c = a * 200L; Serial.println(c);
  unsigned int u = 0; u--; Serial.println(u);
  unsigned long t1 = 4294967000UL; unsigned long t2 = 200; Serial.println(t2 - t1);
}
void loop() {})", h);
        check(r.ok && h.serialOut == "-32768\r\n-5536\r\n60000\r\n65535\r\n496\r\n", "int16 wrap");
    }
    {
        MockHost h;
        Run r = runSketch("compile error: missing semicolon", "void setup() {\n  int x = 5\n  Serial.begin(9600);\n}\nvoid loop() {}\n", h);
        check(!r.compiled && r.err == "';' 가 필요합니다" && r.line == 2, "missing ';' -> line 2");
    }
    {
        MockHost h;
        Run r = runSketch("compile error: undefined identifier", "void setup() {\n  int x = 5;\n  x = y + 1;\n}\nvoid loop() {}\n", h);
        check(!r.compiled && r.err == "정의되지 않은 식별자: y" && r.line == 3, "undefined identifier line 3");
    }
    {
        MockHost h;
        Run r = runSketch("compile error: no setup", "void loop() {}\n", h);
        check(!r.compiled && r.err == "setup() 함수가 없습니다", "missing setup");
    }
    {
        MockHost h;
        Run r = runSketch("runtime error: array out of bounds", "int arr[5];\nvoid setup() {\n  for (int i = 0; i <= 10; i++) {\n    arr[i] = i;\n  }\n}\nvoid loop() {}\n", h);
        check(r.compiled && !r.ok && r.err == "배열 인덱스 범위 초과: arr[5] (크기 5)" && r.line == 4, "OOB error line 4");
    }
    {
        MockHost h;
        Run r = runSketch("runtime error: division by zero", "void setup() {\n  int z = 0;\n  int q = 10 / z;\n}\nvoid loop() {}\n", h);
        check(r.compiled && !r.ok && r.err == "0으로 나눌 수 없습니다" && r.line == 3, "div by zero line 3");
    }
    {
        MockHost h; h.stopAfterChecks = 100000;
        Run r = runSketch("stop inside while(1);", "void setup() { }\nvoid loop() { while (1); }\n", h);
        check(r.ok, "while(1) stops on request");
    }
    {
        MockHost h; h.quiet = true; h.stopAtUs = 2500000;
        h.irqAt[500000] = 0; h.irqAt[700000] = 0; h.irqAt[1500000] = 0;
        Run r = runSketch("interrupt ISR", R"(
volatile int count = 0;
void onPulse() { count++; }
void setup() { Serial.begin(9600); attachInterrupt(digitalPinToInterrupt(2), onPulse, RISING); }
void loop() { delay(1000); Serial.println(count); })", h);
        check(r.ok && h.serialOut == "2\r\n3\r\n", "ISR counted pulses");
    }
    {
        MockHost h; h.stopAfterLoops = 1; h.quiet = true;
        for (char c : std::string("abc 123\n-4.5x")) h.serialIn.push_back((unsigned char)c);
        Run r = runSketch("Serial input", R"(
void setup() {
  Serial.begin(9600);
  int n = Serial.parseInt(); Serial.println(n * 2);
  float f = Serial.parseFloat(); Serial.println(f, 1);
  Serial.println(Serial.available());
  char ch = Serial.read(); Serial.println(ch);
}
void loop() {})", h);
        check(r.ok && h.serialOut == "246\r\n-4.5\r\n1\r\nx\r\n", "parseInt/parseFloat/read");
    }
    {
        MockHost h; h.stopAfterLoops = 1; h.quiet = true;
        Run r = runSketch("EEPROM", R"(
#include <EEPROM.h>
struct Cfg { int a; float b; };
void setup() {
  Serial.begin(9600);
  EEPROM.write(0, 300); Serial.println(EEPROM.read(0));
  Serial.println(EEPROM.read(5));
  float f = 3.25; EEPROM.put(10, f); float g = 0; EEPROM.get(10, g); Serial.println(g);
  Cfg c = {1234, 1.5}; EEPROM.put(20, c); Cfg d; EEPROM.get(20, d); Serial.println(d.a); Serial.println(d.b);
  EEPROM[100] = 7; Serial.println(EEPROM[100] + 1); Serial.println(EEPROM.length());
}
void loop() {})", h);
        check(r.ok && h.serialOut == "44\r\n255\r\n3.25\r\n1234\r\n1.50\r\n8\r\n1024\r\n", "EEPROM ops");
    }
    auto expectOut = [](const char* name, const std::string& src, const std::string& expect, int loops = 1) {
        MockHost h; h.stopAfterLoops = loops; h.quiet = true;
        Run r = runSketch(name, src, h);
        bool ok = r.compiled && r.ok && h.serialOut == expect;
        check(ok, name);
        if (!ok && r.ok) {
            const std::string& a = h.serialOut;
            size_t i = 0;
            while (i < a.size() && i < expect.size() && a[i] == expect[i]) ++i;
            std::printf("  first diff at %zu: got '%s' expected '%s'\n", i, a.substr(i, 30).c_str(), expect.substr(i, 30).c_str());
        }
    };
    auto expectErr = [](const char* name, const std::string& src, bool compileErr, const std::string& msg, int line) {
        MockHost h; h.stopAfterLoops = 1; h.quiet = true;
        Run r = runSketch(name, src, h);
        bool ok = (compileErr ? !r.compiled : (r.compiled && !r.ok)) && r.err == msg && r.line == line;
        check(ok, name);
    };
    expectOut("pointers/refs/struct arrays/enum class/typedef", R"(
typedef struct { int r, g, b; } RGB;
struct Point { int x; int y; };
enum class Mode { IDLE, RUN };
Point pts[] = {{1, 2}, {3, 4}};
char words[3][10] = {"one", "two", "six"};
String names[] = {"Kim", "Lee"};
void inc(int *v) { (*v)++; }
void fill(char *dst, const char *src) { strcpy(dst, src); }
void setup() {
  Serial.begin(9600);
  while (!Serial) { ; }
  RGB c = {10, 20, 30}; Serial.println(c.r + c.g + c.b);
  Serial.println(pts[1].x * 10 + pts[0].y);
  pts[0].x = 9; Serial.println(pts[0].x);
  Mode m = Mode::RUN; if (m == Mode::RUN) Serial.println("run");
  Serial.println(words[1]); Serial.println(words[2][1]);
  Serial.println(names[1].length() + names[0].length());
  int x = 5; int *p = &x; *p = 7; inc(&x); Serial.println(x);
  char buf[8] = "abc"; char *q = buf; while (*q) { Serial.print(*q); q++; } Serial.println();
  fill(buf, "xyz"); Serial.println(buf);
  int arr[4] = {1, 2, 3, 4}; int *pa = arr; Serial.println(pa[2] + *(pa + 3));
  Serial.println(sizeof(int) + sizeof(long) + sizeof(float) + sizeof(char));
  int i, j; for (i = 0, j = 10; i < j; i++, j--) {} Serial.println(i);
  char ch = 'a' + 1; Serial.println(ch);
  byte bb = 0x0F; Serial.println(bb, HEX); Serial.println(bb, BIN);
  float v = analogRead(A0) * (5.0 / 1023.0); Serial.println(v);
  long duration = pulseIn(7, HIGH); int distance = duration * 0.034 / 2; Serial.println(distance);
  Serial.println(5 / 2 * 2.0);
  unsigned int ua = 5; Serial.println(ua > -1);
  int y; if ((y = 5) > 3) Serial.println(y);
  switch ('b') { case 'a': Serial.println("A"); break; case 'b': Serial.println("B"); default: Serial.println("fall"); }
  String s = String(1500 / 1000) + "s"; Serial.println(s);
  byte data[] = {72, 105, 33}; Serial.write(data, 3); Serial.write('\n');
  int bits = 0; for (int k = 0; k < 4; k++) bitWrite(bits, k, k % 2); Serial.println(bits);
  bitSet(bits, 7); Serial.println(bits); bitClear(bits, 1); Serial.println(bits);
  Serial.println(highByte(0x1234), HEX); Serial.println(lowByte(0x1234), HEX);
}
void loop() {}
)", "60\r\n32\r\n9\r\nrun\r\ntwo\r\ni\r\n6\r\n8\r\nabc\r\nxyz\r\n7\r\n11\r\n5\r\nb\r\nF\r\n1111\r\n0.00\r\n19\r\n4.00\r\n0\r\n5\r\nB\r\nfall\r\n1s\r\nHi!\n10\r\n138\r\n136\r\n12\r\n34\r\n");
    expectOut("BlinkWithoutDelay + millis", R"(
const int ledPin = LED_BUILTIN;
int ledState = LOW;
unsigned long previousMillis = 0;
const long interval = 1000;
void setup() { pinMode(ledPin, OUTPUT); Serial.begin(9600); }
void loop() {
  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;
    ledState = (ledState == LOW) ? HIGH : LOW;
    digitalWrite(ledPin, ledState);
    Serial.print(currentMillis); Serial.print(":"); Serial.println(ledState);
  }
  delay(250);
})", "1000:1\r\n2000:0\r\n3000:1\r\n", 13);
    expectOut("random (avr-libc sequence) + forward use + prototypes", R"(
int twice(int v);
void setup() {
  Serial.begin(9600);
  randomSeed(42);
  for (int i = 0; i < 3; i++) { Serial.print(random(100)); Serial.print(" "); }
  Serial.println(random(10, 20) >= 10);
  Serial.println(twice(21));
  Serial.println(later(2));
}
int twice(int v) { return v * 2; }
long later(long z) { return z * 1000000; }
void loop() {}
)", "94 23 9 1\r\n42\r\n2000000\r\n");
    expectOut("struct fields/global init order/macros/misc", R"(
#define INTERVAL (1000UL * 60)
#define MAX3(a, b, c) max(max(a, b), c)
struct Item { char name[10]; String tag; int qty; };
Item items[2] = {{"apple", "A", 3}, {"pear", "P", 5}};
int g1 = 5;
int g2 = g1 * 2;
int readCount = 0;
int nextVal() { return ++readCount; }
int g3 = nextVal() + nextVal();
void setup() {
  Serial.begin(9600);
  Serial.println(items[1].name); Serial.println(items[0].tag + items[1].qty);
  Item copy = items[0]; strcpy(copy.name, "kiwi"); Serial.print(copy.name); Serial.print(" "); Serial.println(items[0].name);
  Serial.println(g2); Serial.println(g3);
  Serial.println(INTERVAL); Serial.println(MAX3(4, 9, 2));
  noInterrupts(); interrupts();
  char buffer[20]; buffer[0] = '\0'; Serial.print("["); Serial.print(buffer); Serial.println("]");
  String k = "한글"; Serial.println(k.length());
  int x = 0xFFFF; Serial.println(x); int y = 40000; Serial.println(y);
  int v = 3; v %= 2; v <<= 3; v |= 1; v ^= 2; Serial.println(v);
  float avg = (float)7 / 2; Serial.println(avg, 3);
  Serial.println(digitalRead(5) == LOW ? "low" : "high");
  Serial.println(String(-1, HEX)); Serial.println(String(10, BIN));
  unsigned long u = 3000000000UL; Serial.println(u); Serial.println((long)u);
}
void loop() {}
)", "pear\r\nA5\r\nkiwi apple\r\n10\r\n3\r\n60000\r\n9\r\n[]\r\n6\r\n-1\r\n-25536\r\n11\r\n3.500\r\nlow\r\nffff\r\n1010\r\n3000000000\r\n-1294967296\r\n");
    expectOut("PROGMEM/pgm_read/_BV/#if/multi-line macro/String input", R"(
#include <avr/pgmspace.h>
#define VERSION 3
#if VERSION >= 2 && defined(ARDUINO)
#define GREETING "v2+"
#elif VERSION == 1
#define GREETING "v1"
#else
#define GREETING "old"
#endif
#ifndef NOT_DEFINED_THING
#define LOG(msg, val) \
  Serial.print(msg); \
  Serial.println(val)
#endif
const byte table[] PROGMEM = {10, 20, 30};
const int words[] PROGMEM = {1000, 2000};
const char title[] PROGMEM = "Title";
void setup() {
  Serial.begin(9600);
  Serial.println(GREETING);
  LOG("t=", pgm_read_byte(&table[1]) + pgm_read_word(&words[1]));
  char buf[10]; strcpy_P(buf, title); Serial.println(buf);
  Serial.println(_BV(3));
  int reg = 0; bitToggle(reg, 2); Serial.println(reg);
  Serial.println(random(5, 5));
  String cmd = "  led on \n"; cmd.trim(); if (cmd == "led on") Serial.println("ON!");
  int idx = cmd.indexOf(' '); Serial.println(cmd.substring(0, idx)); Serial.println(cmd.lastIndexOf('o'));
  cmd.remove(0, 4); Serial.println(cmd); cmd.setCharAt(0, 'O'); Serial.println(cmd); Serial.println(cmd.charAt(1));
  Serial.println(cmd.equalsIgnoreCase("ON")); Serial.println(cmd.endsWith("n")); Serial.println(cmd[1]);
  String num = String(42) + String('!') + String(1.5); Serial.println(num);
  char arr2[6]; num.toCharArray(arr2, 6); Serial.println(arr2);
  Serial.println(String(65, BIN).length());
}
void loop() {}
)", "v2+\r\nt=2020\r\nTitle\r\n8\r\n4\r\n5\r\nON!\r\nled\r\n4\r\non\r\nOn\r\nn\r\n1\r\n1\r\nn\r\n42!1.50\r\n42!1.\r\n7\r\n");
    {
        // 자주 쓰이는 예제 스케치: 컴파일 + 몇 루프 실행이 오류 없이 되는지
        const char* sketches[] = {
            // Fade
            R"(int led = 9; int brightness = 0; int fadeAmount = 5;
void setup() { pinMode(led, OUTPUT); }
void loop() { analogWrite(led, brightness); brightness = brightness + fadeAmount;
  if (brightness <= 0 || brightness >= 255) { fadeAmount = -fadeAmount; } delay(30); })",
            // StateChangeDetection
            R"(const int  buttonPin = 2; const int ledPin = 13;
int buttonPushCounter = 0; int buttonState = 0; int lastButtonState = 0;
void setup() { pinMode(buttonPin, INPUT); pinMode(ledPin, OUTPUT); Serial.begin(9600); }
void loop() {
  buttonState = digitalRead(buttonPin);
  if (buttonState != lastButtonState) {
    if (buttonState == HIGH) { buttonPushCounter++; Serial.println("on"); Serial.print("number of button pushes: "); Serial.println(buttonPushCounter); }
    else { Serial.println("off"); }
    delay(50);
  }
  lastButtonState = buttonState;
  if (buttonPushCounter % 4 == 0) { digitalWrite(ledPin, HIGH); } else { digitalWrite(ledPin, LOW); }
})",
            // Smoothing
            R"(const int numReadings = 10;
int readings[numReadings]; int readIndex = 0; int total = 0; int average = 0; int inputPin = A0;
void setup() { Serial.begin(9600); for (int thisReading = 0; thisReading < numReadings; thisReading++) { readings[thisReading] = 0; } }
void loop() { total = total - readings[readIndex]; readings[readIndex] = analogRead(inputPin); total = total + readings[readIndex];
  readIndex = readIndex + 1; if (readIndex >= numReadings) { readIndex = 0; } average = total / numReadings; Serial.println(average); delay(1); })",
            // toneMelody
            R"(#define NOTE_C4  262
#define NOTE_G3  196
#define NOTE_A3  220
#define NOTE_B3  247
int melody[] = { NOTE_C4, NOTE_G3, NOTE_G3, NOTE_A3, NOTE_G3, 0, NOTE_B3, NOTE_C4 };
int noteDurations[] = { 4, 8, 8, 4, 4, 4, 4, 4 };
void setup() {
  for (int thisNote = 0; thisNote < 8; thisNote++) {
    int noteDuration = 1000 / noteDurations[thisNote];
    tone(8, melody[thisNote], noteDuration);
    int pauseBetweenNotes = noteDuration * 1.30;
    delay(pauseBetweenNotes);
    noTone(8);
  }
}
void loop() {})",
            // traffic light state machine
            R"(enum State { RED_ON, GREEN_ON, YELLOW_ON };
State state = RED_ON; unsigned long last = 0;
const int pins[3] = {10, 11, 12};
void setLights(bool r, bool g, bool y) { digitalWrite(pins[0], r); digitalWrite(pins[1], g); digitalWrite(pins[2], y); }
void setup() { for (int i = 0; i < 3; i++) pinMode(pins[i], OUTPUT); Serial.begin(9600); }
void loop() {
  unsigned long now = millis();
  switch (state) {
    case RED_ON: setLights(true, false, false); if (now - last >= 300) { state = GREEN_ON; last = now; } break;
    case GREEN_ON: setLights(false, true, false); if (now - last >= 200) { state = YELLOW_ON; last = now; } break;
    case YELLOW_ON: setLights(false, false, true); if (now - last >= 100) { state = RED_ON; last = now; } break;
  }
  delay(100);
})",
            // Knight rider + ultrasonic + LCD
            R"(#include <Wire.h>
#include <LiquidCrystal_I2C.h>
LiquidCrystal_I2C lcd(0x27, 16, 2);
const int trigPin = 9, echoPin = 10;
int ledPins[] = {2, 3, 4, 5, 6, 7};
int pinCount = 6;
long readDistance() {
  digitalWrite(trigPin, LOW); delayMicroseconds(2); digitalWrite(trigPin, HIGH); delayMicroseconds(10); digitalWrite(trigPin, LOW);
  long duration = pulseIn(echoPin, HIGH);
  return duration * 0.034 / 2;
}
void setup() {
  lcd.init(); lcd.backlight(); lcd.setCursor(0, 0); lcd.print("Hello, world!");
  for (int thisPin = 0; thisPin < pinCount; thisPin++) pinMode(ledPins[thisPin], OUTPUT);
  pinMode(trigPin, OUTPUT); pinMode(echoPin, INPUT);
}
void loop() {
  for (int thisPin = 0; thisPin < pinCount; thisPin++) { digitalWrite(ledPins[thisPin], HIGH); delay(10); digitalWrite(ledPins[thisPin], LOW); }
  for (int thisPin = pinCount - 1; thisPin >= 0; thisPin--) { digitalWrite(ledPins[thisPin], HIGH); delay(10); digitalWrite(ledPins[thisPin], LOW); }
  long d = readDistance();
  lcd.setCursor(0, 1); lcd.print("Dist: "); lcd.print(d); lcd.print(" cm   ");
})",
            // keypad-like char matrix + serial command parser
            R"(const byte ROWS = 4; const byte COLS = 3;
char keys[ROWS][COLS] = { {'1','2','3'}, {'4','5','6'}, {'7','8','9'}, {'*','0','#'} };
String inputString = ""; bool stringComplete = false;
void handle(const String &cmd) { if (cmd.startsWith("set ")) { int v = cmd.substring(4).toInt(); Serial.println(v * 2); } }
void setup() { Serial.begin(9600); inputString.reserve(200); Serial.println(keys[3][2]); }
void loop() {
  while (Serial.available()) { char inChar = (char)Serial.read(); inputString += inChar; if (inChar == '\n') { stringComplete = true; } }
  if (stringComplete) { inputString.trim(); handle(inputString); inputString = ""; stringComplete = false; }
})",
        };
        int idx = 0;
        for (const char* sk : sketches) {
            MockHost h; h.stopAfterLoops = 5; h.quiet = true;
            for (char ch : std::string("set 21\n")) h.serialIn.push_back(ch);
            std::string nm = "example sketch #" + std::to_string(idx++);
            Run r = runSketch(nm.c_str(), sk, h);
            check(r.compiled && r.ok, nm.c_str());
        }
    }
    expectErr("compile error: unclosed brace", "void setup() {\n  if (1) {\n    delay(1);\n}\nvoid loop() {}\n", true,
              "'}' 가 필요합니다 (함수 'setup' 의 끝 '}' 가 빠진 것 같습니다 - 함수 안에서 다른 함수 'loop' 를 정의할 수 없습니다)", 5);
    expectErr("compile error: unknown function", "void setup() {\n  foo(1);\n}\nvoid loop() {}\n", true, "정의되지 않은 함수: foo", 2);
    expectErr("compile error: Serial typo", "void setup() {\n  Serial.prinln(1);\n}\nvoid loop() {}\n", true, "Serial 에는 'prinln' 함수가 없습니다", 2);
    expectErr("compile error: wrong arg count", "void setup() {\n  digitalWrite(13);\n}\nvoid loop() {}\n", true,
              "함수 'digitalWrite' 의 인자 개수가 맞지 않습니다 (필요 2개, 전달 1개)", 2);
    expectErr("compile error: unknown type", "Foo bar;\nvoid setup() {}\nvoid loop() {}\n", true,
              "알 수 없는 타입: Foo (지원하지 않는 라이브러리이거나 오타입니다)", 1);
    expectErr("compile error: const assign", "const int a = 1;\nvoid setup() {\n  a = 2;\n}\nvoid loop() {}\n", true,
              "const 변수 'a' 에는 값을 대입할 수 없습니다", 3);
    expectErr("runtime error: library method", "Servo s;\nvoid setup() {\n  s.foo(1);\n}\nvoid loop() {}\n", false,
              "Servo 에는 'foo' 함수가 없습니다", 3);
    expectErr("compile error (const-folded): literal + number", "void setup() {\n  Serial.begin(9600);\n  Serial.println(\"Temp: \" + 25);\n}\nvoid loop() {}\n", true,
              "문자열 상수 + 숫자는 포인터 연산입니다 (문자열로 붙이려면 String(\"...\") + 값 형태로 쓰세요)", 3);
    {
        MockHost h;
        Run r = runSketch("runtime error: deep recursion", "int f(int n) { return f(n + 1) + 1; }\nvoid setup() { f(0); }\nvoid loop() {}\n", h);
        check(r.compiled && !r.ok && r.line == 1, "recursion limit -> error");
    }
    {
        MockHost h; h.stopAfterLoops = 1; h.quiet = true;
        Run r = runSketch("speed test", R"(
void setup() {
  Serial.begin(9600);
  long sum = 0;
  for (long i = 0; i < 1000000; i++) {
    sum += i;
  }
  Serial.println(sum);
  int arr[10]; long acc = 0;
  for (long i = 0; i < 200000; i++) { arr[i % 10] = i; acc = acc + arr[(i + 3) % 10] * 2; if (acc > 100000) acc -= 100000; }
  Serial.println(acc);
}
void loop() {})", h);
        check(r.ok && has(h.serialOut, "1783293664\r\n"), "sum wraps to 32-bit long");
        // 첫 루프: 반복당 문장 ~3개 (조건, sum+=, i++), 두 번째: 반복당 ~6개
        double stmts = 1000000.0 * 3 + 200000.0 * 6;
        std::printf("  speed: %.3f s total, ~%.1f M statements/sec (%.1f M loop iterations/sec)\n", r.secs,
                    stmts / r.secs / 1e6, 1200000.0 / r.secs / 1e6);
    }
    {
        MockHost h; h.stopAfterLoops = 1; h.quiet = true;
        Run r = runSketch("speed test: recursive calls fib(22)", R"(
int fib(int n) { if (n < 2) return n; return fib(n - 1) + fib(n - 2); }
void setup() { Serial.begin(9600); Serial.println(fib(22)); }
void loop() {})", h);
        check(r.ok && h.serialOut == "17711\r\n", "fib(22)");
        std::printf("  speed: fib(22) = 57313 calls in %.3f s -> %.2f M calls/sec\n", r.secs, 57313.0 / r.secs / 1e6);
    }
    std::printf("\nRESULT: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
