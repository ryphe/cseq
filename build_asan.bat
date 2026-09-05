@echo off
setlocal

echo =======================================================
echo building cseq - AddressSanitizer debug
echo =======================================================

set "RC_NAME="
if exist cseq.rc set "RC_NAME=cseq.rc"

set "RES_FILE="
if defined RC_NAME (
    if exist Inter.ttf (
        rc /nologo /I headers /fo cseq_asan.res %RC_NAME%
        if %ERRORLEVEL% EQU 0 if exist cseq_asan.res set "RES_FILE=cseq_asan.res"
    )
)

:: Project sources: ASan + debug info + runtime checks, no /GL so we can link
:: with the incremental-friendly plain link. Vendored code compiles with ASan
:: too but keeps /W3 leniency.
cl /nologo /c /MTd /Od /Zi /fsanitize=address /W3 /wd4244 /wd4267 /std:c17 /Iheaders main.c eq.c
if %ERRORLEVEL% NEQ 0 exit /b 1
cl /nologo /c /MTd /Od /Zi /fsanitize=address /W3 /wd4244 /wd4267 /std:c17 /Iheaders vorbis.c miniaudio.c
if %ERRORLEVEL% NEQ 0 exit /b 1

set "LINK_INPUTS=main.obj eq.obj vorbis.obj miniaudio.obj"
if defined RES_FILE set "LINK_INPUTS=%LINK_INPUTS% %RES_FILE%"

link /nologo /DEBUG:FULL /INCREMENTAL:NO /SUBSYSTEM:WINDOWS %LINK_INPUTS% user32.lib gdi32.lib shell32.lib comdlg32.lib msimg32.lib ole32.lib winmm.lib advapi32.lib /OUT:cseq_asan.exe

del *.obj cseq_asan.res 2>nul
echo [SUCCESS] Build complete: cseq_asan.exe
endlocal
