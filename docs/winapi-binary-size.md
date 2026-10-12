# Native WinAPI executable size

This document covers the existing Windows x86 WinAPI frontend, NOT the separate raylib frontend. Work is on the experiment/winapi-microscopic branch; the raylib branch is untouched.

## Measured binaries (Windows x86 Release, core identical to master)

| Build | Exact EXE size | KiB | Compared with baseline |
| --- | ---: | ---: | ---: |
| Original WinAPI executable | 338,944 bytes | 331.0 | Baseline |
| Compact standalone /MT, reduced icon resource | **242,688 bytes** | **237.0** | **-28.4%** |
| Runtime-linked /MD, reduced icon resource | **99,840 bytes** | **97.5** | **-70.5%** |
| Standalone /MT, no icon | **210,944 bytes** | **206.0** | **-37.8%** |
| Runtime-linked /MD, no icon | **70,656 bytes** | **69.0** | **-79.2%** |
| Experimental Clang/LLVM /MD, no icon | **66,048 bytes** | **64.5** | **-80.5%** |

Measured from GitHub Actions artifacts:
- Original: https://github.com/irontom10/cummins-cal-transfer/actions/runs/38205690636
- Current standalone /MT (icon): https://github.com/irontom10/cummins-cal-transfer/actions/runs/38209013714
- Current tiny /MD (icon): https://github.com/irontom10/cummins-cal-transfer/actions/runs/38209013845
- Current no-icon MSVC and LLVM variants: https://github.com/irontom10/cummins-cal-transfer/actions/runs/38209013956

These figures are uncompressed PE32 executable sizes, not ZIP archive sizes. The measurements shown here were retested after reverting all src/core/ changes; the earlier, slightly smaller results with modified export headers are intentionally excluded.

## Build commands

Windows with x86 MSVC C/C++ tools:

    build.bat
    rem Builds: compact standalone WinAPI (/MT, no VC++ redistributable needed).

    build.bat win-api-tiny
    rem Builds: smallest EXE (/MD, VC++ x86 redistributable required).

    build.bat win-api-no-icon
    rem Builds: /MT standalone, no icon resources (generic Windows icon).

    build.bat win-api-tiny-no-icon
    rem Builds: /MD runtime-linked, no icon resources (generic Windows icon).

    build.bat win-api-clang-no-icon
    rem Experimental: clang-cl -Oz, LLVM full LTO, lld-link (Windows x86).
    rem Requires LLVM clang-cl and lld-link on PATH and MSVC x86 build tools.

    build.bat win-forms
    rem Retains the independent legacy WinForms build path.

Each command outputs build/app/CalibrationTransfer.exe. Build one target at a time: the output path is overwritten. The native RP1210 DLL, TOML DLL and CRC utility continue to build.

## How this works

1. The EXE uses a separate optimized object set. The separately built RP1210 and TOML DLLs keep their existing exported ABI.
2. All files under src/core/ are byte-for-byte identical to master. Existing DLL export declarations are retained in the EXE; this costs approximately 1–1.5 KiB compared with the earlier experimental header modifications.
3. Size-first /O1 /GL /Gy /Gw /GF and /LTCG /OPT:REF /OPT:ICF /INCREMENTAL:NO optimize the program and discard unused functions.
4. The original ICO has seven PNG frames and occupies about 111 KiB. src/windows/tools/compact-icon.ps1 copies the 16, 32, 48, and 128 pixel frames byte-for-byte, reducing resources to roughly 29 KiB without changing artwork or overwriting the source icon. The 256px frame is omitted; Windows may upscale 128px for very large icons.
5. Both optional no-icon modes omit the Win32 ICON resource but keep all application functionality. The Windows titlebar/taskbar use a generic icon. The regular build remains unchanged.
6. The optional clang-cl target uses /MD, -Oz, full LLVM LTO, and lld-link; the native APIs and protocol sources are identical to other targets. It requires both MSVC x86 SDK/libraries and LLVM tools at build time, and the VC++ x86 runtime at deployment time. CI compilation is validated; hardware behavior has not been tested.
7. Optional win-api-tiny uses the shared Microsoft C runtime (/MD), rather than linking CRT code inside the executable (/MT). **It is not dependency-free:** the target Windows PC must have the compatible Microsoft Visual C++ x86 runtime.

The compact build retains ASLR (DYNAMIC_BASE), DEP (NX_COMPAT), normal CRT initialization, common dialogs, threads, CRC validation, and the full native calibration-transfer core. No EXE packers, custom CRT entry points, ROM patches, bootloader hacks, or protocol shortcuts.

## Validation

- [x] Standalone WinAPI compiled in Windows x86 CI
- [x] Tiny dynamic-CRT WinAPI compiled in Windows x86 CI
- [x] Both MSVC icon-free variants compiled in Windows x86 CI
- [x] Clang/LLVM -Oz LTO icon-free x86 EXE compiled in Windows CI
- [x] Core headers and implementation exactly match master (zero src/core/ diff)
- [x] Existing native protocol tests passed in CI
- [x] Existing Android build passed in CI
- [x] Verified PE imports, exports, security flags and icon resources of the built binaries
- [ ] Bench-test adapter discovery and ECM readback
- [ ] Bench-test ECM programming with a compatible validated calibration

Do not use the changed executable for production ECM programming until hardware validation. A small executable alone does not prove equivalent runtime behavior.
