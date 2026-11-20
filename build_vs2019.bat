@echo off
rem ============================================================
rem  아두이노 시뮬레이터 - Visual Studio 2019 솔루션 생성 + 빌드
rem  Qt 6.7.0 (msvc2019_64) 경로가 다르면 QTDIR 을 수정하세요.
rem ============================================================
chcp 65001 > nul
setlocal
set QTDIR=C:\Qt\Qt6.7.0\6.7.0\msvc2019_64
set SRC=%~dp0
set BUILD=%~dp0build

cmake -S "%SRC%" -B "%BUILD%" -G "Visual Studio 16 2019" -A x64 -DCMAKE_PREFIX_PATH="%QTDIR%"
if errorlevel 1 goto :fail

cmake --build "%BUILD%" --config Release -- /m /v:minimal
if errorlevel 1 goto :fail

echo.
echo 빌드 완료: %BUILD%\Release\ArduinoSimulator.exe
echo Visual Studio 솔루션: %BUILD%\ArduinoSimulator.sln
goto :eof

:fail
echo.
echo 빌드 실패
exit /b 1
