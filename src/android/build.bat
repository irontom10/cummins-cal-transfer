@echo off
setlocal EnableExtensions

cd /d "%~dp0"

set "ROOT=%CD%"
set "VENDOR=%ROOT%\.vendor-rp1210"
set "CACHE=%ROOT%\.nexiq-cache"
set "SDK_URL=https://download.nexiq.com/Nexiq/SDK/RP1210_Mobile_Native_Android_SDK.zip"
set "SDK_ZIP=%CACHE%\RP1210_Mobile_Native_Android_SDK.zip"
set "SDK_PART=%SDK_ZIP%.part"
set "WRAPPER_JAR=%ROOT%\gradle\wrapper\gradle-wrapper.jar"
set "WRAPPER_URL=https://raw.githubusercontent.com/gradle/gradle/v8.9.0/gradle/wrapper/gradle-wrapper.jar"
set "REMOVE_WRAPPER_JAR=0"
set "RC=1"

if not defined ANDROID_BUILD_VARIANT set "ANDROID_BUILD_VARIANT=Debug"
if /I "%ANDROID_BUILD_VARIANT%"=="Debug" (
    set "GRADLE_TASK=assembleDebug"
    set "APK_PATH=src\android\app\build\outputs\apk\debug\app-debug.apk"
) else if /I "%ANDROID_BUILD_VARIANT%"=="Release" (
    set "GRADLE_TASK=assembleRelease"
    set "APK_PATH=src\android\app\build\outputs\apk\release\app-release.apk"
) else (
    echo ERROR: ANDROID_BUILD_VARIANT must be Debug or Release.
    exit /b 1
)

if exist "%VENDOR%" rmdir /S /Q "%VENDOR%"
if not exist "%CACHE%" mkdir "%CACHE%"
if errorlevel 1 goto :cleanup

if exist "%SDK_ZIP%" (
    echo [android] Using cached NEXIQ RP1210 SDK:
    echo           %SDK_ZIP%
) else (
    echo [android] Downloading RP1210 Mobile Native Android SDK from NEXIQ...
    curl.exe --fail --location --retry 3 --retry-delay 2 --continue-at - --output "%SDK_PART%" "%SDK_URL%"
    if errorlevel 1 (
        echo ERROR: Could not download the NEXIQ Android RP1210 SDK.
        echo        Partial download, if any, was kept at:
        echo        %SDK_PART%
        goto :cleanup
    )

    move /Y "%SDK_PART%" "%SDK_ZIP%" >nul
    if errorlevel 1 (
        echo ERROR: Could not finalize the cached NEXIQ SDK archive.
        goto :cleanup
    )
)

mkdir "%VENDOR%"
if errorlevel 1 goto :cleanup

echo [android] Extracting the vendor runtime payload...
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%ROOT%\stage-vendor.ps1" -Archive "%SDK_ZIP%" -StageRoot "%VENDOR%"
if errorlevel 1 (
    echo ERROR: Could not stage the NEXIQ RP1210 runtime.
    echo        Cached SDK was kept for the next build:
    echo        %SDK_ZIP%
    goto :cleanup
)

if not exist "%VENDOR%\app\src\main\jniLibs\arm64-v8a\libnuln3r32.so" (
    echo ERROR: NEXIQ SDK did not contain the expected arm64 RP1210 libraries.
    goto :cleanup
)

if not exist "%VENDOR%\app\src\main\jniLibs\armeabi-v7a\libnuln3r32.so" (
    echo ERROR: NEXIQ SDK did not contain the expected 32-bit ARM RP1210 libraries.
    goto :cleanup
)

if not exist "%VENDOR%\app\src\main\assets\Files\nuln3r32.ini" (
    echo ERROR: NEXIQ SDK did not contain the expected RP1210 INI/assets.
    goto :cleanup
)

if not exist "%WRAPPER_JAR%" (
    echo [android] Fetching Gradle wrapper bootstrap...
    if not exist "%ROOT%\gradle\wrapper" mkdir "%ROOT%\gradle\wrapper"
    curl.exe --fail --location --retry 3 --retry-delay 2 --output "%WRAPPER_JAR%" "%WRAPPER_URL%"
    if errorlevel 1 (
        echo ERROR: Could not download the Gradle wrapper bootstrap JAR.
        goto :cleanup
    )
    set "REMOVE_WRAPPER_JAR=1"
)

if not exist "local.properties" (
    if defined ANDROID_HOME (
        >local.properties echo sdk.dir=%ANDROID_HOME:\=\\%
    ) else if exist "%LOCALAPPDATA%\Android\Sdk" (
        >local.properties echo sdk.dir=%LOCALAPPDATA:\=\\%\\Android\\Sdk
    )
)

echo [android] Building Calibration Transfer %ANDROID_BUILD_VARIANT% APK...
call "%ROOT%\gradlew.bat" clean %GRADLE_TASK%
set "RC=%ERRORLEVEL%"

if "%RC%"=="0" (
    echo.
    echo Android build complete:
    echo   %APK_PATH%
)

:cleanup
echo [android] Removing temporary staged NEXIQ runtime files...
if exist "%VENDOR%" rmdir /S /Q "%VENDOR%"

if exist "%SDK_ZIP%" (
    echo [android] Keeping cached NEXIQ SDK:
    echo           %SDK_ZIP%
)

if "%REMOVE_WRAPPER_JAR%"=="1" (
    if exist "%WRAPPER_JAR%" del /Q "%WRAPPER_JAR%"
)

exit /b %RC%
