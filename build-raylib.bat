@echo off
setlocal EnableExtensions
cd /d "%~dp0"
where cmake.exe >nul 2>nul
if errorlevel 1 (
    echo ERROR: CMake is required for the raylib build.
    exit /b 1
)
rem Build the 32-bit backend in an isolated directory.
cmake -S "src\windows\raylib" -B "build\raylib" -A Win32 %*
if errorlevel 1 exit /b 1
cmake --build "build\raylib" --config Release
if errorlevel 1 exit /b 1
echo.
echo Raylib frontend built:
echo   build\raylib\app\Release\CalibrationTransferRaylib.exe
exit /b 0
