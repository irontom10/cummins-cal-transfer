# Native WinAPI executable size

This document covers the existing Windows x86 WinAPI frontend, NOT the separate raylib frontend. Work is on the experiment/winapi-microscopic branch; the raylib branch is untouched.

## Measured binaries (Windows x86, MSVC Release)

| Build | Exact EXE size | KiB | Compared with baseline |
| --- | ---: | ---: | ---: |
| Original WinAPI executable | 338,944 bytes | 331.0 | Baseline |
| Size-first whole-program optimization, original icon | 323,584 bytes | 316.0 | -4.5% |
| Compact standalone /MT, reduced icon resource | **241,152 bytes** | **235.5** | **-28.9%** |
| Optional runtime-linked /MD, reduced icon resource | **98,816 bytes** | **96.5** | **-70.8%** |
| Standalone /MT, no icon | **209,408 bytes** | **204.5** | **-38.2%** |
| Runtime-linked /MD, no icon | **69,632 bytes** | **68.0** | **-79.5%** |

Measured from GitHub Actions artifacts:
- Original: https://github.com/irontom10/cummins-cal-transfer/actions/runs/38205690636
- Size-first LTCG: https://github.com/irontom10/cummins-cal-transfer/actions/runs/38207140241
- Compact standalone: https://github.com/irontom10/cummins-cal-transfer/actions/runs/38207318006
- Tiny runtime-linked: https://github.com/irontom10/cummins-cal-transfer/actions/runs/38207474326
- Icon-free both variants: https://github.com/irontom10/cummins-cal-transfer/actions/runs/38207946064

These figures are uncompressed PE32 executable sizes, not ZIP archive sizes. Numbers are specific to the named source revisions.

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

    build.bat win-forms
    rem Retains the independent legacy WinForms build path.

Each command outputs build/app/CalibrationTransfer.exe. Build one target at a time: the output path is overwritten. The native RP1210 DLL, TOML DLL and CRC utility continue to build.

## How this works

1. The EXE uses a separate optimized object set. The separately built RP1210 and TOML DLLs keep their existing exported ABI.
2. Static-link-specific preprocessor flags prevent 20 unnecessary exported functions from being retained in the EXE.
3. Size-first /O1 /GL /Gy /Gw /GF and /LTCG /OPT:REF /OPT:ICF /INCREMENTAL:NO optimize the program and discard unused functions.
4. The original ICO has seven PNG frames and occupies about 111 KiB. src/windows/tools/compact-icon.ps1 copies the 16, 32, 48, and 128 pixel frames byte-for-byte, reducing resources to roughly 29 KiB without changing artwork or overwriting the source icon. The 256px frame is omitted; Windows may upscale 128px for very large icons.
5. Both optional no-icon modes omit the Win32 ICON resource but keep all application functionality. The Windows titlebar/taskbar use a generic icon. The regular build remains unchanged.
6. Optional win-api-tiny uses the shared Microsoft C runtime (/MD), rather than linking CRT code inside the executable (/MT). **It is not dependency-free:** the target Windows PC must have the compatible Microsoft Visual C++ x86 runtime.

The compact build retains ASLR (DYNAMIC_BASE), DEP (NX_COMPAT), normal CRT initialization, common dialogs, threads, CRC validation, and the full native calibration-transfer core. No EXE packers, custom CRT entry points, ROM patches, bootloader hacks, or protocol shortcuts.

## Validation

- [x] Standalone WinAPI compiled in Windows x86 CI
- [x] Tiny dynamic-CRT WinAPI compiled in Windows x86 CI
- [x] Both icon-free variants compiled in Windows x86 CI
- [x] Existing native protocol tests passed in CI
- [x] Existing Android build passed in CI
- [x] Verified PE imports, exports, security flags and icon resources of the built binaries
- [ ] Bench-test adapter discovery and ECM readback
- [ ] Bench-test ECM programming with a compatible validated calibration

Do not use the changed executable for production ECM programming until hardware validation. A small executable alone does not prove equivalent runtime behavior.
