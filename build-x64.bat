@echo off
setlocal EnableExtensions

rem Optional 64-bit LLVM/WinAPI build. The default build.bat remains x86.
rem An x64 process requires a 64-bit RP1210 vendor DLL at runtime.
cd /d "%~dp0"

set "ROOT=%CD%"
set "SRC_CORE=%ROOT%\src\core"
set "SRC_WIN_NATIVE=%ROOT%\src\windows\native"
set "SRC_WIN_API=%ROOT%\src\windows\win-api"
set "BUILD=%ROOT%\build\x64"
set "OBJ=%BUILD%\obj"
set "APP=%BUILD%\app"

rem Reuse an existing x64 MSVC environment, or initialize the x64 toolchain.
if /I "%VSCMD_ARG_TGT_ARCH%"=="x64" goto :toolchain_ready

set "VCVARS="
if defined VSINSTALLDIR (
    if exist "%VSINSTALLDIR%VC\Auxiliary\Build\vcvarsall.bat" (
        set "VCVARS=%VSINSTALLDIR%VC\Auxiliary\Build\vcvarsall.bat"
    )
)
if defined VCVARS goto :load_toolchain

set "VSWHERE="
if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" (
    set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
)
if not defined VSWHERE (
    for /f "delims=" %%I in ('where vswhere.exe 2^>nul') do if not defined VSWHERE set "VSWHERE=%%I"
)
if not defined VSWHERE (
    echo ERROR: Cannot locate vswhere.exe or the Visual Studio x64 toolchain.
    goto :fail
)

set "VSROOT="
for /f "usebackq tokens=*" %%I in (`"%VSWHERE%" -latest -products * -property installationPath`) do set "VSROOT=%%I"
if not defined VSROOT (
    echo ERROR: No Visual Studio C/C++ installation found.
    goto :fail
)
set "VCVARS=%VSROOT%\VC\Auxiliary\Build\vcvarsall.bat"

:load_toolchain
echo [x64] Switching Visual C++ toolchain to x64...
call "%VCVARS%" x64
if errorlevel 1 goto :fail

:toolchain_ready
if /I not "%VSCMD_ARG_TGT_ARCH%"=="x64" (
    echo ERROR: Expected an x64 MSVC target, got "%VSCMD_ARG_TGT_ARCH%".
    goto :fail
)

where clang-cl.exe >nul 2>nul
if errorlevel 1 (
    echo ERROR: clang-cl.exe not found.
    goto :fail
)
where lld-link.exe >nul 2>nul
if errorlevel 1 (
    echo ERROR: lld-link.exe not found.
    goto :fail
)

rem Build independently: do not delete or overwrite build\app (the x86 EXE).
if exist "%BUILD%" rmdir /S /Q "%BUILD%"
mkdir "%OBJ%"
if errorlevel 1 goto :fail
mkdir "%APP%"
if errorlevel 1 goto :fail

pushd "%OBJ%"
echo [x64] Compiling optimized native WinAPI and RP1210 core...
clang-cl /nologo --target=x86_64-pc-windows-msvc /W3 /clang:-Oz /clang:-flto=full /Gy /Gw /GF /MD /TC ^
    /D_CRT_SECURE_NO_WARNINGS ^
    /I"%SRC_CORE%" /I"%SRC_WIN_NATIVE%" /c ^
    "%SRC_WIN_NATIVE%\rp1210scan.c" ^
    "%SRC_WIN_NATIVE%\rp1210_transport.c" ^
    "%SRC_CORE%\j1939_transport.c" ^
    "%SRC_CORE%\ct_platform.c" ^
    "%SRC_CORE%\clip_transfer.c" ^
    "%SRC_CORE%\echo_transfer.c" ^
    "%SRC_CORE%\clip_crypto.c" ^
    "%SRC_CORE%\clip_cal.c" ^
    "%SRC_CORE%\j_endian.c" ^
    "%SRC_CORE%\xcal_crc.c" ^
    "%SRC_CORE%\config_store.c" ^
    "%SRC_WIN_API%\main.c"
if errorlevel 1 goto :fail_from_obj

echo [x64] Linking PE32+ x64 executable (no icon)...
lld-link /nologo /MACHINE:X64 /SUBSYSTEM:WINDOWS ^
    /OPT:REF /OPT:ICF /DYNAMICBASE /NXCOMPAT ^
    /OUT:"%APP%\CalibrationTransfer.exe" ^
    main.obj ^
    rp1210scan.obj rp1210_transport.obj j1939_transport.obj ^
    ct_platform.obj clip_transfer.obj echo_transfer.obj ^
    clip_crypto.obj clip_cal.obj j_endian.obj xcal_crc.obj config_store.obj ^
    kernel32.lib user32.lib gdi32.lib comctl32.lib comdlg32.lib
if errorlevel 1 goto :fail_from_obj

popd
echo.
echo x64 build complete:
echo   build\x64\app\CalibrationTransfer.exe
echo NOTE: Requires the Microsoft VC++ x64 Redistributable (/MD).
echo NOTE: RP1210 adapters require a matching 64-bit vendor DLL; x86-only DLLs cannot be loaded.
exit /b 0

:fail_from_obj
popd

:fail
echo.
echo x64 BUILD FAILED
exit /b 1
