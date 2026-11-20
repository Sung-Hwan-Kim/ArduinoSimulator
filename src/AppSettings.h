#pragma once
// 환경설정 (INI 파일에 저장)
#include "Catalog.h"
#include <QSettings>
#include <QString>
#include <QStringList>
#include <QVector>
#include <memory>

struct AppSettings {
    // ---- 일반 ----
    QString workDir;                         // 작업 폴더 (프로젝트 저장/열기 기본 위치)
    QString defaultBoard = "arduino_uno";    // 새 회로의 기본 보드
    bool openLastFile = true;                // 시작 시 마지막 파일 열기
    QString lastFile;
    QStringList recentFiles;
    int autoSaveMinutes = 0;                 // 0 = 사용 안 함

    // ---- 시뮬레이션 ----
    double pinCurrentLimit = 40.0;           // mA, 아두이노 핀 절대 최대 정격
    double ledBurnFactor = 2.0;              // LED 정격 전류의 몇 배에서 소손
    double usbCurrentLimit = 500.0;          // mA, USB 5V 공급 한계
    bool warnFloating = true;                // 플로팅 입력 경고

    // ---- 저항 추천 / 자동 배선 ----
    double ledTargetCurrent = 15.0;          // mA, LED 목표 전류
    QString resistorSeries = "E12";          // E12 | E24
    double pullResistor = 10000.0;           // 풀업/풀다운 추천값
    bool autoInsertResistors = true;         // 자동 배선 시 저항 자동 삽입
    bool generateCodeOnAutoWire = true;      // 자동 배선 후 코드 자동 생성

    // ---- 편집기 / 캔버스 ----
    QString theme = "dark";                  // 스킨 모드: dark(기본) | light
    QString editorFont = "Consolas";
    int editorFontSize = 11;
    bool showGrid = true;
    bool snapToGrid = true;

    // ---- 사용자 부품 ----
    QVector<CustomPartSpec> customParts;

    static AppSettings& instance();
    static QString iniPath();
    // 창 위치/크기 등 저장용 QSettings (같은 INI 파일)
    static std::unique_ptr<QSettings> open();

    void load();
    void save() const;
    void addRecentFile(const QString& path);
    QString effectiveWorkDir() const;
};
