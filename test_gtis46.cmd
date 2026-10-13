@echo off
setlocal EnableExtensions
rem GTIS4.6 secure CLIP bench READ test using TOML, not env switches.
rem Never use the Upload workflow with a live ECM.
cd /d "%~dp0"

set "APP=build\app\CalibrationTransfer.exe"
set "SNIFFER=..\insite-clip-id-tracer\build\clip_bus_sniff.exe"
set "CFG=%USERPROFILE%\.config\CalibrationTransfer\config.toml"

if not exist "%APP%" (
    echo ERROR: %APP% not found. Run build.bat first.
    exit /b 1
)
if not exist "%CFG%" (
    echo ERROR: config.toml not found: "%CFG%"
    exit /b 1
)

for /f %%i in ('powershell -NoProfile -Command "Get-Date -Format yyyyMMdd-HHmmss"') do set "STAMP=%%i"
set "BUSLOG=bus_trace_%STAMP%.tsv"

echo Configuration: %CFG%
echo.
echo Required TOML settings:
echo   [experimental]
echo   gtis46_secure_read = true
echo   gtis46_conn_override = 0
echo.
echo   [gtis46-config]
echo   KEY_HEX = "...local secret..."
echo   CONTEXT_HEX = "...local context..."
echo.
echo Use ordinary READ. Session block acknowledgment is derived from
echo the actual 02 04 envelope size, not an arbitrary connection slot.
echo.
if exist "%SNIFFER%" (
    start "clip-bus-sniffer" /MIN "%SNIFFER%" --duration 1800 --out "%BUSLOG%"
    echo Passive sniffer started, 30 min maximum.
) else (
    echo WARNING: no bus sniffer at %SNIFFER%.
)

"%APP%"
set "APPRC=%ERRORLEVEL%"
taskkill /FI "WINDOWTITLE eq clip-bus-sniffer*" >nul 2>&1
echo Application exit code: %APPRC%
if exist "%BUSLOG%" (
    echo Captured bus log: %BUSLOG%
)
endlocal
