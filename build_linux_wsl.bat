@echo off
rem ============================================================
rem  Windows PC 에서 WSL(Ubuntu) 로 Linux 버전 빌드
rem  처음 한 번: WSL 안에서 build_linux.sh 상단의 apt 패키지 설치
rem  사용법: build_linux_wsl.bat          (빌드)
rem          build_linux_wsl.bat package  (빌드 + tar.gz)
rem ============================================================
chcp 65001 > nul
wsl.exe --cd "%~dp0" -- bash ./build_linux.sh %*
if errorlevel 1 (
    echo.
    echo Linux 빌드 실패
    exit /b 1
)
