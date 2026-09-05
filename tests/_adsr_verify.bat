@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
cd /d "%~dp0"
cl /nologo /W4 /WX /O2 adsr_verify.c /Fe:adsr_verify.exe
if %ERRORLEVEL% NEQ 0 exit /b %ERRORLEVEL%
adsr_verify.exe
