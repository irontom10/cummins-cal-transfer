@echo off
setlocal EnableExtensions
rem GTIS4.6 DA00 OFFLINE planning only. NO ECM PROGRAMMING TRAFFIC.
rem Uses %USERPROFILE%\.config\CalibrationTransfer\config.toml.
cd /d "%~dp0"
set "APP=build\app\CalibrationTransfer.exe"
set "CFG=%USERPROFILE%\.config\CalibrationTransfer\config.toml"

if not exist "%APP%" (
    echo ERROR: %APP% not found. Run build.bat first.
    exit /b 1
)
if not exist "%CFG%" (
    echo ERROR: TOML configuration not found: "%CFG%"
    exit /b 1
)
echo Configuration: %CFG%
echo.
echo Required TOML flags:
echo   [experimental]
echo   gtis46_da00_plan = true
echo.
echo Select UPLOAD and choose a valid CM2450E CCAL.
echo The app will verify its CRC and prepare a DA00 frame plan OFFLINE.
echo The plan intentionally returns a not-programmed status.
echo No adapter connection, unlock, loader entry or programming is done.
echo A live secure DA00 uploader is NOT implemented.
echo.
"%APP%"
set "APPRC=%ERRORLEVEL%"
echo Application exit code: %APPRC%
endlocal
