#pragma once
// 부품 카탈로그: 부품 종류, 핀 배치, 규격(파라미터), 코드 생성 템플릿
#include <QColor>
#include <QPointF>
#include <QSizeF>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVector>

// 자동 배선 / 저항 추천에서 쓰는 핀의 역할
enum class PinRole {
    NC,             // 연결 안 함
    Passive,        // 일반 단자 (자동 배선 대상 아님)
    Gnd,            // GND 로 연결
    Vcc,            // 5V(또는 3.3V) 로 연결
    SigDigitalOut,  // 부품이 디지털 신호를 내보냄 → 아두이노 디지털 입력
    SigAnalogOut,   // 부품이 아날로그 전압을 내보냄 → 아두이노 아날로그 입력
    SigDigitalIn,   // 부품이 디지털 제어를 받음 ← 아두이노 디지털 출력
    SigPwmIn,       // 부품이 PWM 을 받음 ← 아두이노 PWM 핀
    SigBidir,       // 양방향 데이터 (DHT 등) ↔ 디지털 핀
    I2cSda,
    I2cScl,
    LedAnode,       // LED 애노드 → 직렬 저항 → 아두이노 (PWM 우선)
    LedCathode,     // LED 캐소드 → GND
    SwitchToPin,    // 스위치 한쪽 → 디지털 핀 (INPUT_PULLUP)
    SwitchToGnd,    // 스위치 다른쪽 → GND
    DividerHigh,    // 분압 상단 → 5V
    DividerOut,     // 분압 출력 → 아날로그 핀 + 하단 저항 → GND
    Board           // 아두이노 보드 핀
};

enum class BoardPinKind { None, Digital, AnalogOnly, P5V, P3V3, Vin, Gnd, Aref, Reset, IoRef, NC };

struct PinDef {
    QString name;
    QPointF pos;                 // 부품 로컬 좌표 (연결점)
    PinRole role = PinRole::Passive;
    bool optional = false;       // 자동 배선 시 생략
    // ---- 보드 핀 ----
    BoardPinKind bkind = BoardPinKind::None;
    int ardPin = -1;             // 아두이노 핀 번호 (D13 → 13, Uno A0 → 14)
    int adc = -1;                // 아날로그 채널
    bool pwm = false;
    int irq = -1;
    QString func;                // 부가 기능 (SDA, TX ...)
};

enum class ParamType { Number, Choice, Bool };

struct ParamOption {
    QString label;
    QVariant value;
    QVariantMap sets;            // 이 옵션을 고르면 함께 설정되는 다른 파라미터
};

struct ParamDef {
    QString key;
    QString label;
    ParamType type = ParamType::Number;
    QVariant def;
    QString unit;
    double minV = 0.0, maxV = 1e12;
    QVector<ParamOption> options;   // Choice: 선택지 / Number: 추천값 목록
    bool live = false;              // 시뮬레이션 중 조절 가능 (센서 환경값)
    bool hidden = false;
    bool logScale = false;          // 슬라이더 로그 스케일
};

struct PartDef {
    QString id;
    QString category;
    QString name;
    QString desc;
    QString prefix;              // 참조 기호 (R, LED, SW, U ...)
    QString varPrefix;           // 코드 생성 변수 이름 접두사
    QString shape;               // 그리기 유형
    QString icon;                // 모듈 아이콘
    QColor color = QColor(30, 90, 160);
    QSizeF size;
    QVector<PinDef> pins;
    QVector<ParamDef> params;
    QVector<QVector<int>> internalNets;  // 내부적으로 연결된 핀 묶음
    bool isBoard = false;
    QString clickParam;          // 시뮬레이션 중 클릭 시 바뀌는 파라미터
    bool clickMomentary = false; // 누르는 동안만 true
    // ---- 코드 생성 템플릿 ({N} 변수명, {L} 참조기호, {P:핀이름|핀이름}, {A:파라미터}) ----
    QString cgInclude, cgGlobal, cgSetup, cgRead, cgAct, cgPrint;

    int pinIndex(const QString& pinName) const;
    const ParamDef* param(const QString& key) const;
    QVariantMap defaultParams() const;
};

// 환경설정에서 사용자가 추가하는 센서/모듈 정의 (INI 에 저장)
struct CustomPartSpec {
    QString name;                // 표시 이름 (예: "MAX4466 마이크")
    QString desc;                // 설명
    QString pins;                // 핀 이름 목록 "VCC,GND,OUT"
    QString output = "analog";   // analog | digital | digital_low
    double vmin = 0.0, vmax = 5.0;   // 아날로그 출력 범위 (V)
    QColor color = QColor(30, 90, 160);
};

class Catalog {
public:
    static const Catalog& instance();
    // 사용자 부품 갱신 (환경설정 변경 시)
    static void setCustomParts(const QVector<CustomPartSpec>& specs);
    static PartDef makeCustomPart(const CustomPartSpec& spec);

    const QVector<PartDef>& parts() const { return m_parts; }
    const PartDef* find(const QString& id) const;
    QStringList categories() const;

private:
    Catalog();
    static Catalog& mutableInstance();
    QVector<PartDef> m_parts;
    int m_builtinCount = 0;
};
