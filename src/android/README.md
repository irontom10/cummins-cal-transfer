# Android Calibration Transfer

This is the production Android front end for the shared Calibration Transfer C89 core.

The Android app does **not** keep the mobile RP1210 SDK binaries in this repository. The build helper downloads NEXIQ's official `RP1210_Mobile_Native_Android_SDK.zip` directly from NEXIQ, extracts the required `.so` files and `assets/Files` payload into a temporary staging directory, builds the APK, and deletes the staged SDK/runtime files when the build finishes or fails.

## Build

From the repository root on Windows:

```powershell
.\build-android.bat
```

or directly:

```powershell
cd src\android
.\build.bat
```

No GitHub authentication or private dependency repository is required. The build downloads the SDK from:

```text
https://download.nexiq.com/Nexiq/SDK/RP1210_Mobile_Native_Android_SDK.zip
```

The downloaded/extracted vendor payload exists only at:

```text
src/android/.vendor-rp1210/
```

during the build. It is removed afterward. The built APK still contains the vendor runtime payload required by the selected RP1210 driver.

The staging helper locates the ARM64 and ARMv7 RP1210 libraries and the RP1210 `Files` directory by their contents, so it does not depend on a particular top-level SDK ZIP directory layout.

APK:

```text
src/android/app/build/outputs/apk/debug/app-debug.apk
```

## Architecture

```text
Android UI / Bluetooth discovery + pairing
                 |
                 | selected MAC + RP1210 implementation
                 v
rp1210_transport_android.c
                 |
                 | dlopen / RP1210_ClientConnect / Send / Read / Command
                 v
vendor Android RP1210 .so
                 |
                 v
src/core/j1939_transport.c
                 |
                 v
src/core/clip_transfer.c
                 |
                 +-- CLIP
                 +-- ECH/ECHO readback
                 +-- ENI / ELITE II readback
                 +-- validated supported upload path
```

The Java UI remains intentionally thin. File selection, Bluetooth, progress display, and Android permissions live in Java. Calibration protocol behavior stays in `src/core/`.

## UI

The screen mirrors the Windows utility:

- Vendor / RP1210 implementation
- Bluetooth diagnostic adapter
- J1939 protocol
- baud rate
- tool source address
- ECM source address
- Pull ECM CAL
- Upload CCAL
- progress and status

Android additionally has Refresh Paired, Scan Bluetooth, and Pair controls. Discovery filters out ordinary speakers/headphones and only lists names that look like supported diagnostic adapters.

Pull uses Android's document creator and Upload uses Android's document picker. The native C core always works on an app-private temporary file; the Java layer copies to/from the selected Android document URI.
