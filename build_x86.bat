@echo off
setlocal EnableExtensions

rem Build the native RP1210/CLIP core as x86, then publish the .NET 10
rem WinForms front end as a self-contained x86 single-file executable.

cd /d "%~dp0"

if /I "%VSCMD_ARG_TGT_ARCH%"=="x86" goto :toolchain_ready

echo [build] Current VS target is "%VSCMD_ARG_TGT_ARCH%"; switching to x86...

if defined VSINSTALLDIR (
    if exist "%VSINSTALLDIR%VC\Auxiliary\Build\vcvarsall.bat" (
        call "%VSINSTALLDIR%VC\Auxiliary\Build\vcvarsall.bat" x86
        if errorlevel 1 goto :fail
        goto :toolchain_ready
    )
)

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo ERROR: vswhere.exe not found and VSINSTALLDIR is not usable.
    echo Install Visual Studio Desktop development with C++ or run from a Developer shell.
    goto :fail
)

set "VSROOT="
for /f "usebackq tokens=*" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSROOT=%%I"

if not defined VSROOT (
    echo ERROR: Could not locate Visual Studio C++ x86 tools.
    goto :fail
)

if not exist "%VSROOT%\VC\Auxiliary\Build\vcvarsall.bat" (
    echo ERROR: vcvarsall.bat not found under "%VSROOT%".
    goto :fail
)

call "%VSROOT%\VC\Auxiliary\Build\vcvarsall.bat" x86
if errorlevel 1 goto :fail

:toolchain_ready
if /I not "%VSCMD_ARG_TGT_ARCH%"=="x86" (
    echo ERROR: MSVC target is still "%VSCMD_ARG_TGT_ARCH%"; refusing to build the wrong architecture.
    goto :fail
)

where dotnet >nul 2>nul
if errorlevel 1 (
    echo ERROR: dotnet was not found. Install the .NET 10 SDK.
    goto :fail
)

if not exist build mkdir build

echo [build] Native x86 DLL...
cl /nologo /W3 /O2 /TC /D_CRT_SECURE_NO_WARNINGS /LD ^
    rp1210scan.c rp1210clip.c clip_crypto.c clip_cal.c cummins_crc.c ^
    /link /MACHINE:X86 /DEF:rp1210scan.def ^
    /OUT:build\rp1210scan.dll ^
    /IMPLIB:build\rp1210scan.lib ^
    /PDB:build\rp1210scan.pdb ^
    kernel32.lib user32.lib
if errorlevel 1 goto :fail

echo [build] CRC utility...
cl /nologo /W3 /O2 /TC /D_CRT_SECURE_NO_WARNINGS ^
    crc_call.c cummins_crc.c /Fe:build\crc_call.exe
if errorlevel 1 goto :fail

echo [build] WinForms single-file executable...
if exist build\publish rmdir /S /Q build\publish
dotnet publish .\caltool.csproj ^
    -c Release ^
    -r win-x86 ^
    --self-contained true ^
    -o .\build\publish
if errorlevel 1 goto :fail

rem Keep generated linker/compiler intermediates out of the source tree.
del /Q *.obj rp1210scan.exp 2>nul

echo.
echo Build complete:
echo   build\publish\CumminsCalTransfer.exe
echo   build\crc_call.exe
echo.
echo rp1210scan.dll is embedded in CumminsCalTransfer.exe and extracted at runtime.
exit /b 0

:fail
echo.
echo BUILD FAILED
exit /b 1
