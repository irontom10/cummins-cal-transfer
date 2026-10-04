@echo off
setlocal EnableExtensions

rem CalPull must be built x86:
rem   - Program.cs is compiled /platform:x86
rem   - most RP1210 implementations are 32-bit
rem   - native Cummins CRC verification is compiled into rp1210scan.dll
rem
rem A normal "Developer PowerShell" often targets amd64.  If that happened,
rem switch this batch file's own environment to the x86 MSVC toolchain before
rem compiling.  The .def file deliberately uses UNDECORATED C names;
rem MSVC LINK resolves the target-specific C decoration itself.

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
    echo Run this from a Visual Studio Developer shell or install Desktop C++ tools.
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

echo [build] MSVC target: %VSCMD_ARG_TGT_ARCH%

if not exist build mkdir build

cl /nologo /W3 /O2 /TC /D_CRT_SECURE_NO_WARNINGS /LD ^
    rp1210scan.c rp1210clip.c clip_crypto.c clip_cal.c cummins_crc.c ^
    /link /MACHINE:X86 /DEF:rp1210scan.def /OUT:build\rp1210scan.dll kernel32.lib user32.lib
if errorlevel 1 goto :fail

cl /nologo /W3 /O2 /TC /D_CRT_SECURE_NO_WARNINGS ^
    crc_call.c cummins_crc.c /Fe:build\crc_call.exe
if errorlevel 1 goto :fail

set "CSC=%WINDIR%\Microsoft.NET\Framework\v4.0.30319\csc.exe"
if not exist "%CSC%" set "CSC=csc"

"%CSC%" /nologo /platform:x86 /target:winexe /optimize+ ^
    /reference:System.dll ^
    /reference:System.Core.dll ^
    /reference:System.Windows.Forms.dll ^
    /reference:System.Drawing.dll ^
    /out:build\CalPull.exe Program.cs
if errorlevel 1 goto :fail

copy /Y CalPull.exe.config build\CalPull.exe.config >nul

echo.
echo Build complete:
echo   build\CalPull.exe
echo   build\rp1210scan.dll
echo   build\crc_call.exe
echo.
echo Verify native DLL architecture/exports with:
echo   dumpbin /headers build\rp1210scan.dll ^| findstr /I "machine"
echo   dumpbin /exports build\rp1210scan.dll ^| findstr /I "rp1210_"
echo.
echo Cummins CCAL CRC verification is built into rp1210scan.dll.
echo Upload refuses a bad CRC before opening the RP1210 adapter.
exit /b 0

:fail
echo.
echo BUILD FAILED
exit /b 1
