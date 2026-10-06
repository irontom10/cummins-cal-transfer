@echo off
setlocal EnableExtensions

cd /d "%~dp0"

set "ROOT=%CD%"
set "VENDOR=%ROOT%\.vendor-rp1210"
set "VENDOR_REPO=irontom10/rp1210-android-test"
set "RC=1"

if exist "%VENDOR%" rmdir /S /Q "%VENDOR%"

echo [android] Staging private RP1210 Android vendor payload...

where gh >nul 2>nul
if not errorlevel 1 (
    gh repo clone "%VENDOR_REPO%" "%VENDOR%" -- --depth 1
) else (
    git clone --depth 1 "https://github.com/%VENDOR_REPO%.git" "%VENDOR%"
)

if errorlevel 1 (
    echo ERROR: Could not clone %VENDOR_REPO%.
    echo Authenticate with GitHub CLI ^(gh auth login^) or Git Credential Manager.
    goto :cleanup
)

if not exist "%VENDOR%\app\src\main\jniLibs\arm64-v8a\libnuln3r32.so" (
    echo ERROR: Vendor RP1210 libraries were not found in the staging repository.
    goto :cleanup
)

if not exist "%VENDOR%\app\src\main\assets\Files\nuln3r32.ini" (
    echo ERROR: Vendor RP1210 INI/assets were not found in the staging repository.
    goto :cleanup
)

if not exist "local.properties" (
    if defined ANDROID_HOME (
        >local.properties echo sdk.dir=%ANDROID_HOME:\=\\%
    ) else if exist "%LOCALAPPDATA%\Android\Sdk" (
        >local.properties echo sdk.dir=%LOCALAPPDATA:\=\\%\\Android\\Sdk
    )
)

echo [android] Building Calibration Transfer APK...
call "%VENDOR%\gradlew.bat" -p "%ROOT%" clean assembleDebug
set "RC=%ERRORLEVEL%"

if "%RC%"=="0" (
    echo.
    echo Android build complete:
    echo   src\android\app\build\outputs\apk\debug\app-debug.apk
)

:cleanup
echo [android] Removing staged private vendor payload...
if exist "%VENDOR%" rmdir /S /Q "%VENDOR%"

exit /b %RC%
