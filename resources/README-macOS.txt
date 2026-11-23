아두이노 시뮬레이터 - macOS 설치 및 처음 실행 방법
==================================================

1. 설치
   ArduinoSimulator.app 을 옆의 Applications(응용 프로그램) 폴더로 끌어다 놓습니다.

2. 처음 실행 (한 번만)
   이 앱은 Apple 유료 개발자 인증서로 서명·공증되지 않아서 처음 실행할 때 경고가 나옵니다.

   - macOS 15 (Sequoia) 이상
     앱을 한 번 열어 경고 창을 [완료]로 닫은 뒤,
     시스템 설정 → 개인정보 보호 및 보안 → 아래쪽의 [그래도 열기] 를 누르고 암호를 입력합니다.

   - macOS 11 ~ 14
     Finder 에서 앱을 Control+클릭(오른쪽 클릭) → [열기] → 경고 창에서 다시 [열기].

   - 또는 터미널에서 (모든 버전):
       xattr -dr com.apple.quarantine /Applications/ArduinoSimulator.app

   한 번 허용하면 다음부터는 그냥 실행됩니다.

3. 지원 환경
   macOS 11 (Big Sur) 이상, Apple Silicon(M1 이후) 과 Intel 모두 지원 (Universal)

4. 파일 위치
   설정 파일 : ~/Library/Application Support/ArduinoSimulator/ArduinoSimulator.ini
   작업 폴더 : ~/Documents/ArduinoSimulator (환경설정에서 변경 가능)
   처음 저장할 때 "문서 폴더 접근" 허용 창이 나오면 [허용] 을 누르세요.

5. 단축키
   Windows 의 Ctrl 단축키는 macOS 에서 ⌘(Command) 입니다. (예: ⌘S 저장, ⌘R 저항 추천, ⌘, 환경설정)
   회로 확대/축소: ⌘ + 스크롤

라이선스: MIT (LICENSE.txt), Qt 6 라이브러리는 LGPLv3
https://github.com/Sung-Hwan-Kim/ArduinoSimulator
