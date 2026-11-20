@echo off
rem Build SketchInterpreter console test (VS2019 x64)
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
set "HERE=%~dp0"
set "SRC=%HERE%..\src\interpreter"
if not exist "%HERE%out" mkdir "%HERE%out"
cl /nologo /utf-8 /EHsc /std:c++17 /W3 /permissive- /O2 /I"%SRC%" "%SRC%\*.cpp" "%HERE%interp_test.cpp" /Fo"%HERE%out\\" /Fe"%HERE%out\interp_test.exe"
exit /b %ERRORLEVEL%
