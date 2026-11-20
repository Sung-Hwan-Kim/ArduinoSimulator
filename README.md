# 아두이노 시뮬레이터 (Qt 6.7.0 + Visual Studio 2019)

부품을 화면에 배치하고 연결해서 아두이노 스케치를 실제처럼 테스트하는 데스크톱 시뮬레이터입니다.

## 빌드

필요한 것: Visual Studio 2019(C++ 데스크톱 개발), CMake 3.21+, Qt 6.7.0 `msvc2019_64` (`C:\Qt\Qt6.7.0\6.7.0\msvc2019_64`)

```bat
build_vs2019.bat
```

- 실행 파일: `build\Release\ArduinoSimulator.exe` (빌드 후 `windeployqt` 로 Qt DLL 이 함께 복사됨)
- Visual Studio 솔루션: `build\ArduinoSimulator.sln` → VS2019 에서 열어 F5 로 디버깅 가능

## 주요 기능

| 기능 | 설명 |
|---|---|
| 부품 카탈로그 | 보드 3종(Uno R3 / Nano / Mega 2560) + 부품·센서 40여 종, 모델별 규격 선택 (저항 E24 169종 + 직접 입력, LED 4크기×11색, LDR GL55xx 6종, NTC 5종, MQ 가스 9종, 서보 6종, 트랜지스터/MOSFET 등) |
| 배치·배선 | 끌어다 놓기, 핀 클릭 → 핀 클릭 배선, 꺾임점, 전선 색상, 회전(R)/복제(Ctrl+D)/삭제(Del), 확대(Ctrl+휠)/이동(가운데 버튼) |
| 회로 해석 | 절점 해석 + 다이오드 구간선형 모델 → LED 밝기, 핀 전압/전류, PWM 평균, 플로팅 입력 |
| 테스트·경고 | 핀 과전류(40mA), LED 소손(저항 없이 연결 시), 저항 정격전력 초과, 전원 미연결, 5V 단락, USB 전류 초과, LED 극성 반대 |
| 스케치 실행 | 내장 인터프리터 (C/C++ 부분집합: 함수·배열·String·Serial·인터럽트 등) — 별도 컴파일러 불필요 |
| 라이브러리 | Servo, DHT, LiquidCrystal_I2C, LiquidCrystal, Adafruit_NeoPixel, Stepper, NewPing, Adafruit_BME280/BMP280, Wire |
| 자동화 | 회로 마법사(센서 선택 → 배치·핀 할당·배선·저항 삽입·코드 생성), 자동 배치+배선, 미연결 부품 자동 배선, 코드 자동 생성 |
| 저항 자동 추천 | LED 직렬저항, LDR/NTC 분압저항, 스위치 풀업/풀다운, 트랜지스터 베이스, MOSFET 게이트, I2C 풀업 — 근거 표시 + 원클릭 적용 + 누락 저항 자동 삽입 |
| 환경설정 (INI) | 작업 폴더, 기본 보드, 자동 저장, 시뮬레이션 경고 기준, 저항 추천 기준(E12/E24, LED 목표 전류), 편집기 글꼴, 사용자 센서 추가 |
| 스킨 모드 | 다크 모드(기본) / 라이트 모드 — 환경설정 → 화면·편집기. 회로 캔버스·코드 편집기·부품 라벨까지 함께 바뀜 |
| 창 상태 유지 | 메인 창/대화상자 위치·크기·최대화, 도크 배치, 확대 비율을 INI 에 저장 → 다음 실행 시 그대로 복원 |
| 버전 / 아이콘 | exe 아이콘 + 파일 버전 정보, 창 제목·상태 표시줄·도움말 → 버전 정보에 버전과 빌드 날짜 표시 |

## 버전 올리기

`CMakeLists.txt` 의 `project(ArduinoSimulator VERSION 1.0.0 ...)` 한 곳만 바꾸면 exe 파일 속성, 창 제목, 버전 정보 창, INI(`[General] AppVersion`), 저장 파일(`appVersion`)에 모두 반영됩니다. 변경 내용은 `CHANGELOG.md` 에 기록하세요.

## 명령줄 옵션 (자동 테스트용)

```bat
ArduinoSimulator.exe --example 6 --run --seconds 5 --screenshot out.png --geometry 2380,1060,1440,1030 --theme dark
```

| 옵션 | 설명 |
|---|---|
| `--example N` | 예제 N 번(1~15) 불러오기 |
| `--parts id,id,...` | 지정한 부품을 자동 배치·배선하고 코드 생성 (예: `led,button,dht`) |
| `--run` | 시뮬레이션 실행 |
| `--seconds S` | S 초 뒤 스크린샷 저장 후 종료 |
| `--screenshot 파일` | 창 캡처 PNG + `파일.txt`(시리얼 출력·메시지·코드) 저장 |
| `--geometry x,y,w,h` | 창 위치/크기 (이 옵션으로 띄운 창 위치는 INI 에 저장하지 않음) |
| `--theme dark\|light` | 이번 실행만 스킨 모드 지정 |

설정 파일은 실행 파일과 같은 폴더의 `ArduinoSimulator.ini` 입니다. 그 폴더에 쓸 수 없으면 `%APPDATA%\ArduinoSimulator\ArduinoSimulator.ini` 를 사용합니다.

## 사용 순서

1. **도구 → 회로 마법사(Ctrl+M)** 에서 센서를 체크 → 자동으로 배치·배선·코드 생성
2. 또는 부품 목록에서 직접 배치 → **자동 배치+배선(Ctrl+Shift+W)** 또는 직접 배선
3. **저항 자동 추천·설정(Ctrl+R)** 으로 저항값 점검
4. **F5** 실행 → 버튼/스위치/PIR 등은 클릭, 센서값은 속성 창 슬라이더나 마우스 휠로 조절
5. 시리얼 모니터 / 메시지 창에서 출력과 경고 확인

## 프로젝트 구조

```
src/
  Catalog.*          부품 정의 (핀, 규격, 코드 템플릿) + 사용자 부품
  PartItem.*         부품 그리기·조작        WireItem.*  전선
  CircuitScene.*     배치/배선/저장          CircuitView.*  확대/이동
  Netlist.*          넷 계산                 CircuitSolver.*  DC 회로 해석
  Simulator.*        시뮬레이션 엔진 (ArduinoHost 구현, 라이브러리 흉내)
  ResistorAdvisor.*  저항 추천               AutoWire.*  자동 배치·배선·코드 생성
  PropertyPanel.*    속성 창                 PartLibrary.*  부품 목록
  SettingsDialog.*   환경설정                WizardDialog.*  회로 마법사
  AppSettings.*      INI 저장/불러오기       MainWindow.*  메인 창
  interpreter/       아두이노 스케치 인터프리터 (Qt 비의존 C++17)
tests/               인터프리터 단위 테스트
```

## 제한 사항

- 실제 AVR 기계어를 실행하지 않고 스케치 소스를 해석 실행합니다. 포인터 연산, 템플릿, 클래스 정의 등 고급 C++ 문법과 위에 없는 라이브러리는 지원하지 않습니다.
- 회로 해석은 DC(정상 상태)입니다. 커패시터는 개방으로 처리되고 RC 시정수 같은 과도 응답은 계산하지 않습니다. PWM 은 시간 평균으로 계산합니다.
- DHT / I2C / 초음파 같은 통신 프로토콜은 라이브러리 함수 수준에서 흉내 냅니다 (비트 단위 타이밍은 계산하지 않음).

## 라이선스

[MIT License](LICENSE) © 2026 김성환 (Sung-Hwan-Kim)

배포용 zip 에 함께 들어 있는 Qt 6 라이브러리는 Qt 의 라이선스(LGPLv3)를, VC++ 런타임 DLL 은 Microsoft Visual C++ 재배포 조건을 따릅니다.
