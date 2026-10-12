@echo off
setlocal EnableExtensions
rem GTIS4.6 secure upload: preflight bypass + encrypted codec + legacy
rem loader transition + raw loader programming. Requires CLIP46_POC_PULL=1
rem (secure session) and CLIP46_POC_FLASH=1 (upload through the codec).
rem Stationary authorized bench ECM with stable power.
cd /d "%~dp0"

set "APP=build\app\CalibrationTransfer.exe"

if not exist "%APP%" (
    echo ERROR: %APP% not found. Run build.bat first.
    exit /b 1
)

rem Optional argument 1: CCAL to upload.
if "%~1"=="" (
    set "CCAL=00-_Kenworth_T880H_6449953_80703465_10-11-2026-15-41-54.ccal"
) else (
    set "CCAL=%~1"
)
if not exist "%CCAL%" (
    echo ERROR: calibration file not found: "%CCAL%"
    exit /b 1
)

set "CLIP46_POC_FILE=%USERPROFILE%\Downloads\clip46-bench-local.cfg"
set "CLIP46_POC_PULL=1"
set "CLIP46_POC_FLASH=1"
set "CLIP46_POC_CONN_ID="

echo Config: %CLIP46_POC_FILE%
echo CCAL:   %CCAL%
echo.
echo GTIS4.6 secure upload - flow:
echo   1. CRC-only preflight, secure session established
echo   2. multi-query, 0x0B state entry
echo   3. unlock (encrypted mode command)
echo   4. loader transition 12 seq 00 17 00 (encrypted)
echo   5. if accepted: raw loader protocol, 4B/4D writes, verify, re-lock
echo.
echo Use the app's UPLOAD workflow and select this CCAL.
echo Stable power. Do not interrupt. Recovery = INSITE reflash if needed.
echo.

"%APP%"
set "APPRC=%ERRORLEVEL%"

echo.
echo Application exit code: %APPRC%
echo The progress log shows each stage. If the loader transition was
echo accepted, the raw loader preamble and 4B/4D write progress follow.
echo If any stage failed, the error names the exact step.
endlocal
