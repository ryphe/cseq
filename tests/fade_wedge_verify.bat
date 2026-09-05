@echo off
setlocal
rem Extracts the real fade code blocks from the shipping headers and builds
rem tests\fade_wedge_verify.exe against them, then runs it.
cd /d "%~dp0.."

sed -n "/^enum FadeCurveType {/,/^};/p" headers\config.h > tests\_fade_enum.inc
sed -n "/^static inline float compute_fade_gain/,/^}/p" headers\dsp.h > tests\_fade_gain.inc
sed -n "/^#define CSEQ_FADE_CURVE_ALPHA/,/^static inline void draw_aa_line/p" headers\ui.h | sed "$d" > tests\_fade_tpl.inc

call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
if errorlevel 1 (
    echo [ERROR] vcvars64 failed
    exit /b 1
)

cl /nologo /W3 /D NDEBUG /MT /O1 /std:c17 /I headers /Fe:tests\fade_wedge_verify.exe tests\fade_wedge_verify.c /link user32.lib gdi32.lib msimg32.lib
if errorlevel 1 exit /b 1
tests\fade_wedge_verify.exe
endlocal
