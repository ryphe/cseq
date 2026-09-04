@echo off
setlocal

echo =======================================================
echo building cseq
echo =======================================================

:: 1. Detect which .rc file exists
set "RC_NAME="
if exist cseq.rc set "RC_NAME=cseq.rc"
if exist resource.rc set "RC_NAME=resource.rc"

:: 2. Compile the resource file (only if .rc exists and Inter.ttf is present)
set "RES_FILE="
if defined RC_NAME (
    if not exist Inter.ttf (
        echo [WARN] Inter.ttf not found. Skipping resource compilation.
        echo        Font will be loaded from disk at runtime.
    ) else (
        echo [INFO] Compiling resources from %RC_NAME%...
        rc /nologo /I headers /fo cseq.res %RC_NAME%
        if %ERRORLEVEL% EQU 0 if exist cseq.res (
            set "RES_FILE=cseq.res"
            echo [INFO] Resource compiled successfully: %RES_FILE%
        ) else (
            echo [WARN] Resource compilation failed. Proceeding without embedded font.
        )
    )
)

:: 3. Compile the C source files
set "INC_FLAG="
if exist headers set "INC_FLAG=/Iheaders"

:: Optional profiling harness (§0): set cseq_PROFILE=1 to emit
:: rebuild/strip timings + GDI handle counts via OutputDebugString
set "EXTRA_DEFS="
if defined cseq_PROFILE set "EXTRA_DEFS=/Dcseq_PROFILE"

:: Optional SIMD level: set cseq_AVX2=1 to compile the 8-wide AVX2
:: track-summing path (runtime CPU check still guards it). Default is
:: the SSE2 baseline on x64 with a scalar fallback.
set "ARCH_FLAGS="
if defined cseq_AVX2 set "ARCH_FLAGS=/arch:AVX2"

echo [INFO] Compiling C source files...

:: Project code is held to /W4 /WX. Vendored code - miniaudio and vorbis -
:: is compiled separately at /W3 with leniency so upstream warnings never
:: fail the build. All objects are linked together with /LTCG below.
set "COMMON_FLAGS=/DNDEBUG /MT /O1 /Os /Oi /Gy /Gw /GS- /fp:fast %ARCH_FLAGS% /GL /std:c17 /permissive- /Zc:inline /Zc:strictStrings %EXTRA_DEFS% /Iheaders"
set "PROJ_FLAGS=%COMMON_FLAGS% /W4 /WX /wd4324"
set "VEND_FLAGS=%COMMON_FLAGS% /W3 /wd4244 /wd4267 /wd4456 /wd4457 /wd4701 /wd4702 /wd4703 /wd5045 /wd4191"

cl /nologo /c %PROJ_FLAGS% main.c eq.c
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Compilation failed - project sources
    exit /b %ERRORLEVEL%
)
cl /nologo /c %VEND_FLAGS% vorbis.c miniaudio.c
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Compilation failed - vendored sources
    exit /b %ERRORLEVEL%
)

set "LINK_INPUTS=main.obj eq.obj vorbis.obj miniaudio.obj"
if defined RES_FILE set "LINK_INPUTS=%LINK_INPUTS% %RES_FILE%"

link /nologo /INCREMENTAL:NO /SUBSYSTEM:WINDOWS /OPT:REF /OPT:ICF /LTCG /FILEALIGN:512 %LINK_INPUTS% user32.lib gdi32.lib shell32.lib comdlg32.lib msimg32.lib ole32.lib winmm.lib /OUT:cseq.exe

if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Compilation failed!
    exit /b %ERRORLEVEL%
)

:: Optional: Clean up intermediate build artifacts
del *.res *.obj 2>nul

echo [SUCCESS] Build complete: cseq.exe
endlocal