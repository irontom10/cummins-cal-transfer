@echo off
setlocal EnableExtensions
rem GTIS4.6 secure CLIP bench test: exercise the derived connection ID
rem (slot rule) with the passive bus sniffer capturing the handshake.
rem READ workflow only, stationary authorized bench ECM. Never upload/flash.
cd /d "%~dp0"

set "APP=build\app\CalibrationTransfer.exe"
set "SNIFFER=..\insite-clip-id-tracer\build\clip_bus_sniff.exe"

if not exist "%APP%" (
    echo ERROR: %APP% not found. Run build.bat first.
    exit /b 1
)

rem Optional argument 1: local CLIP46 config (KEY_HEX + CONTEXT_HEX only).
if "%~1"=="" (
    set "CFG=%USERPROFILE%\Downloads\clip46-bench-local.cfg"
) else (
    set "CFG=%~1"
)
if not exist "%CFG%" (
    echo ERROR: config not found: "%CFG%"
    echo Generate one from your own v1.3 capture:
    echo   powershell -File tools\clip46_poc_config.ps1 -TraceFile ^<trace^> -OutputFile "%CFG%"
    exit /b 1
)

for /f %%i in ('powershell -NoProfile -Command "Get-Date -Format yyyyMMdd-HHmmss"') do set "STAMP=%%i"
set "BUSLOG=bus_trace_%STAMP%.tsv"



set "CLIP46_POC_FILE=%USERPROFILE%\Downloads\clip46-bench-local.cfg"
set "CLIP46_POC_PULL=1"
set "CLIP46_POC_FLASH="
set "CLIP46_POC_CONN_ID="

echo Config:  %CFG%
echo Bus log: %BUSLOG%
echo.
echo Use the ordinary READ workflow in the application window.
echo Do NOT select upload/programming. Stationary bench ECM only.
echo.

if exist "%SNIFFER%" (
    start "clip-bus-sniffer" /MIN "%SNIFFER%" --duration 1800 --out "%BUSLOG%"
    echo Passive bus sniffer started, 30 min cap. Closing its window stops it.
) else (
    echo WARNING: bus sniffer not found at %SNIFFER% - continuing without it.
)

"%APP%"
set "APPRC=%ERRORLEVEL%"

rem Closing the sniffer's console window triggers its clean CTRL_CLOSE path.
taskkill /FI "WINDOWTITLE eq clip-bus-sniffer*" >nul 2>&1

echo.
echo Application exit code: %APPRC%
echo.
if exist "%BUSLOG%" (
    echo First protected 10 01 requests on the bus:
    findstr /C:"CLIP_10_01" "%BUSLOG%"
    echo.
    echo Handshake envelopes 02 02 / 02 03 / 02 04:
    findstr /C:"CLIP_ENVELOPE" "%BUSLOG%" | findstr /C:"02_02..02_04"
    echo.
    echo Compare the 10 01 outer byte with the cid the app reported in its
    echo progress note - slot 0x03 gives 0x0B, slot 0x04 gives 0x0A - and
    echo record the reported 02 04 plaintext offsets to pin the field.
    echo Full bus capture: %BUSLOG%
)
endlocal
