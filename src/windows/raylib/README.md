# Raylib frontend (Windows / x86)

This is a third UI for Calibration Transfer, written in ISO C89 with raylib controls and Windows native Open/Save dialogs. It links the **existing native RP1210, J1939, and calibration core directly**, with no .NET or WinForms dependency.

## Build

Install CMake and Visual Studio's 32-bit MSVC C/C++ toolchain. Run from the repository root:

    build-raylib.bat

The build first checks for an installed raylib 5.5 package, otherwise fetches the pinned upstream source and builds it. Result:

    build/raylib/app/Release/CalibrationTransferRaylib.exe

For an offline build with pre-downloaded raylib source:

    cmake -S src/windows/raylib -B build/raylib -A Win32 -DFETCHCONTENT_SOURCE_DIR_RAYLIB=C:/path/to/raylib
    cmake --build build/raylib --config Release

## UI font: JetBrains Mono Nerd Font

All raylib UI text is rendered with the same font using raylib's `DrawTextEx` / `MeasureTextEx`. The frontend prefers **JetBrainsMono Nerd Font Mono Regular** and includes download/upload/refresh Nerd Font icons.

The executable **does not bundle any font files**. Install JetBrains Mono Nerd Font on Windows or place a font file in a `fonts` folder next to the executable:

    build/raylib/app/Release/fonts/JetBrainsMonoNerdFontMono-Regular.ttf

Automatic font discovery, in order:

1. Full font filename supplied in the `CALTRANSFER_FONT` environment variable.
2. Optional `[ui] font_path = "C:/path/to/JetBrainsMonoNerdFontMono-Regular.ttf"` in the shared TOML config.
3. Next to the executable, including an optional `fonts/` subdirectory.
4. User-installed fonts under `%LOCALAPPDATA%/Microsoft/Windows/Fonts/`.
5. System-installed fonts under `%WINDIR%/Fonts/`.

Supported filename variants include `JetBrainsMonoNerdFontMono-Regular.ttf`, `JetBrainsMonoNerdFont-Regular.ttf`, and `JetBrainsMonoNerdFontPropo-Regular.ttf`. If none can be loaded, the UI remains functional using raylib's default font, and the header indicates that the Nerd Font was not found.

Only the 95 printable ASCII characters and three UI icon glyphs are loaded from the font into raylib's texture atlas. This keeps the atlas compact while preserving the glyphs used by this UI.

## Windows UI feature parity

| Windows feature | Raylib implementation |
| --- | --- |
| RP1210 vendor discovery | rp1210_refresh / rp1210_get |
| Adapter/device selection, automatic device | Scrollable selectors using the existing scanner |
| J1939 and baud (Auto/125k/250k/500k/1M) | Identical native arguments |
| Tool SA / ECM SA | Validated hexadecimal byte fields |
| Read/download ECM to XCAL | rp1210_pull_xcal and Windows Save As dialog |
| Upload XCAL to ECM | rp1210_upload_xcal, Windows Open dialog and explicit confirmation |
| CRC guard / protocol restrictions | Core controls these unchanged |
| Transfer progress/errors | Separate native worker and synchronized progress snapshot |
| Configuration | Shared config_store.c, same config.toml |
| Active-transfer exit guard | App defers closing until the worker is finished |

The application does **not** include a new Windows protocol implementation. The GUI lives in main.c; native_bridge.c/.h isolate Win32 dialogs, threading and callbacks to avoid Win32/raylib name collisions.

**Limitations:** This Windows x86 implementation has not yet been compiled in Windows CI or tested on hardware. Do not use for production ECM programming until bench validated. The raylib widget code can be reused on Linux/Android, but this target currently depends on the Windows RP1210 vendor DLL backend; porting the native bridge and transport remains separate work.
