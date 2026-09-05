@echo off
setlocal enabledelayedexpansion

rem =====================================================================
rem  test_all.bat — comprehensive verification runner for cseq.
rem
rem  Builds the project and every standalone test harness, runs them all,
rem  and writes a complete, timestamped log. Returns a non-zero exit code
rem  if any check failed (so it can gate a CI / pre-merge step).
rem
rem  Sections:
rem    1. Toolchain setup (MSVC vcvars64)
rem    2. Release build sanity check (project must compile at /W4 /WX)
rem    3. Compile each standalone harness
rem    4. Run each harness and capture its pass/fail exit code
rem    5. Aggregated summary
rem    6. Cleanup of intermediate build artifacts (exe/obj/inc)
rem
rem  Log:  test_all.log (always overwritten) plus test_all_<timestamp>.log
rem
rem  Notes:
rem  - The harnesses are compiled with -I.. (project root) on the include path
rem    so ogg.h's __has_include("vorbis.c") finds vorbis.c in the source root
rem    and enables OGG decoding (no "[NOTICE] vorbis.c not found" noise).
rem  - All intermediate artifacts (harness .exe/.obj and the extracted
rem    _fade_*.inc files) are removed at the end; the log is the only output.
rem =====================================================================

set "ROOT=%~dp0"
cd /d "%ROOT%"

set "VCVARS=C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
set "BUILD_DIR=%ROOT%build_tmp"

set "PASS=0"
set "FAIL=0"

rem --- Timestamp (Windows %DATE%/%TIME% locale-independent-ish) ---------
for /f "tokens=2-4 delims=/ " %%a in ("%DATE%") do set "D=%%c%%a%%b"
for /f "tokens=1-2 delims=: " %%a in ("%TIME%") do set "T=%%a%%b"
set "STAMP=%D%_%T%"
set "LOG=%ROOT%test_all.log"
set "LOGTS=%ROOT%test_all_%STAMP%.log"

set "CONSOLE=1"
if /i "%~1"=="-q" set "CONSOLE=0"

rem Small helper: print to console and append to the log.
> "%LOG%" echo cseq test_all — started %DATE% %TIME%
>>"%LOG%" echo ====================================================================

call :LOG "cseq test_all"
call :LOG "root : %ROOT%"
call :LOG "log  : %LOGTS%"
call :LOG

rem ---------------------------------------------------------------------
rem  1. Toolchain
rem ---------------------------------------------------------------------
call :LOG "== [1/6] Toolchain setup =="
if not exist "%VCVARS%" (
    call :LOG "ERROR: vcvars64.bat not found at %VCVARS%"
    call :FAIL_STEP "toolchain"
    goto :done
)
call "%VCVARS%" >nul 2>&1
if errorlevel 1 (
    call :LOG "ERROR: vcvars64.bat failed"
    call :FAIL_STEP "toolchain"
    goto :done
)
call :LOG "MSVC environment ready."
call :LOG ""

rem ---------------------------------------------------------------------
rem  2. Release build sanity check
rem ---------------------------------------------------------------------
call :LOG "== [2/6] Release build (project must compile at /W4 /WX) =="
if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"
call build.bat >"%BUILD_DIR%\build.log" 2>&1
set "BRC=!errorlevel!"
if exist "%BUILD_DIR%\build.log" (
    type "%BUILD_DIR%\build.log" >>"%LOG%"
)
if "%BRC%"=="0" (
    call :LOG "BUILD: PASS (cseq.exe produced, zero /W4 /WX errors)"
    set /a PASS+=1
) else (
    call :LOG "BUILD: FAIL (see build_tmp\build.log)"
    set /a FAIL+=1
)
call :LOG

rem ---------------------------------------------------------------------
rem  3. Compile the standalone harnesses
rem ---------------------------------------------------------------------
call :LOG "== [3/6] Compile standalone harnesses =="

rem fade_wedge_verify needs the real fade code blocks extracted first.
call :LOG "  extracting fade code blocks from headers..."
sed -n "/^enum FadeCurveType {/,/^};/p" headers\config.h > tests\_fade_enum.inc
sed -n "/^static inline float compute_fade_gain/,/^}/p" headers\dsp.h > tests\_fade_gain.inc
sed -n "/^#define CSEQ_FADE_CURVE_ALPHA/,/^static inline void draw_aa_line/p" headers\ui.h | sed "$d" > tests\_fade_tpl.inc
if not exist tests\_fade_tpl.inc (
    call :LOG "  ERROR: fade template extraction failed"
    set /a FAIL+=1
) else (
    call :LOG "  fade extraction ok."
)

set "COMPILE_OK=1"

call :COMPILE adsr_verify.c        "adsr_verify"        "/W4 /WX /O2 /I.."                      ""
call :COMPILE dsp_verify.c         "dsp_verify"         "/W3 /O2 /I.. /I..\headers"             "user32.lib gdi32.lib msimg32.lib"
call :COMPILE reverse_verify.c     "reverse_verify"     "/W3 /O2 /I.. /I..\headers"             "user32.lib gdi32.lib msimg32.lib"
call :COMPILE repro_halo.c         "repro_halo"         "/W3 /O2 /std:c17 /I.. /I..\headers"    "user32.lib gdi32.lib msimg32.lib"
call :COMPILE fade_wedge_verify.c  "fade_wedge_verify"  "/W3 /D NDEBUG /MT /O1 /std:c17 /I.. /I..\headers" "user32.lib gdi32.lib msimg32.lib"

if not "%COMPILE_OK%"=="1" (
    call :LOG "One or more harnesses failed to compile; aborting run."
    goto :done
)
call :LOG

rem ---------------------------------------------------------------------
rem  4. Run the harnesses
rem ---------------------------------------------------------------------
call :LOG "== [4/6] Run harnesses =="

call :RUN adsr_verify       "ADSR envelope math (audio.h MIDI audition)"
call :RUN dsp_verify        "DSP / FX modules + TrackFilter + peak pyramid"
call :RUN reverse_verify    "Sample clip reversing + undo/redo"
call :RUN repro_halo        "Piano-roll paint path for all clip kinds (GDI)"
call :RUN fade_wedge_verify "Fade wedge vs. envelope curve alignment"

call :LOG

rem ---------------------------------------------------------------------
rem  5. Cleanup of intermediate build artifacts
rem ---------------------------------------------------------------------
call :LOG "== [5/6] Cleanup =="
call :LOG "  removing harness exe/obj and extracted _fade_*.inc files..."
pushd "%ROOT%tests"
del /q adsr_verify.exe adsr_verify.obj ^
        dsp_verify.exe dsp_verify.obj ^
        reverse_verify.exe reverse_verify.obj ^
        repro_halo.exe repro_halo.obj ^
        fade_wedge_verify.exe fade_wedge_verify.obj ^
        _fade_enum.inc _fade_gain.inc _fade_tpl.inc 2>nul
popd
call :LOG "  cleanup done."
call :LOG

rem ---------------------------------------------------------------------
rem  6. Summary
rem ---------------------------------------------------------------------
call :LOG "================================================================"
call :LOG "RESULT: %PASS% passed, %FAIL% failed"
if "%FAIL%"=="0" (
    call :LOG "ALL CHECKS PASSED"
) else (
    call :LOG "SOME CHECKS FAILED — inspect %LOGTS%"
)
call :LOG "================================================================"

rem Copy the log to the timestamped name and echo the summary line.
copy /y "%LOG%" "%LOGTS%" >nul
if "%CONSOLE%"=="1" (
    echo.
    echo RESULT: %PASS% passed, %FAIL% failed
    if "%FAIL%"=="0" (echo ALL CHECKS PASSED) else (echo SOME CHECKS FAILED)
    echo Full log: %LOGTS%
)

:done
endlocal & set "FINAL=%FAIL%"
exit /b %FINAL%

rem =====================================================================
rem  Subroutines
rem =====================================================================

:LOG
set "MSG=%~1"
if "%CONSOLE%"=="1" echo(%MSG%
>>"%LOG%" echo(%MSG%
exit /b 0

:FAIL_STEP
set /a FAIL+=1
call :LOG "  -> %~1: FAILED"
exit /b 0

rem Compile one harness.  Usage: :COMPILE <src> <name> <cflags> <libs>
:COMPILE
set "SRC=%~1"
set "NM=%~2"
set "CF=%~3"
set "LB=%~4"
call :LOG "  compiling %NM%..."
pushd "%ROOT%tests"
cl /nologo %CF% "%SRC%" /Fe:%NM%.exe /link %LB% >>"%LOG%" 2>&1
set "RC=!errorlevel!"
popd
if not "!RC!"=="0" goto :compile_fail
>>"%LOG%" echo   -^> %NM%: compiled ok
if "%CONSOLE%"=="1" echo   -^> %NM%: compiled ok
exit /b 0
:compile_fail
>>"%LOG%" echo   -^> %NM%: COMPILE FAILED (exit !RC!)
if "%CONSOLE%"=="1" echo   -^> %NM%: COMPILE FAILED (exit !RC!)
set "COMPILE_OK=0"
exit /b 0

rem Run one harness.  Usage: :RUN <exe> <description>
:RUN
set "NM=%~1"
set "DESC=%~2"
call :LOG
call :LOG "--- %NM% : %DESC% ---"
pushd "%ROOT%tests"
"%NM%.exe" >>"%LOG%" 2>&1
set "RC=!errorlevel!"
popd
if not "!RC!"=="0" goto :run_fail
>>"%LOG%" echo   -^> %NM%: PASS (exit 0)
if "%CONSOLE%"=="1" echo   -^> %NM%: PASS (exit 0)
set /a PASS+=1
exit /b 0
:run_fail
>>"%LOG%" echo   -^> %NM%: FAIL (exit !RC!)
if "%CONSOLE%"=="1" echo   -^> %NM%: FAIL (exit !RC!)
set /a FAIL+=1
exit /b 0
