# Android prototype

This branch contains an Android front end for the existing C calibration-transfer
core.

The important split is:

```text
Android UI / Storage Access Framework
        |
       JNI
        |
clip_transfer.c / echo_transfer.c / clip_cal.c / clip_crypto.c / ccal_crc.c
        |
android_j1939_transport.c
        |
raw 29-bit CAN frames
        |
SlcanUsbTransport.java
        |
Android USB host -> Lawicel/SLCAN adapter
```

The existing Windows RP1210 source is not compiled into the APK.

## What is implemented

- The existing CLIP, ENI/ELITE II, ECH/ECHO readback, CCAL writer/CRC, and
  validated CLIP programming code are compiled directly into the Android native
  library.
- Android uses its Storage Access Framework for choosing input/output CCAL files.
  Native code works on an app-cache path, so the C file code does not need to
  understand `content://` URIs.
- A raw-CAN J1939 transport replaces RP1210.
- Destination-specific classic J1939 TP RTS/CTS and receive-side BAM are handled
  in native C, including the 1000-byte CLIP reads that RP1210 previously
  segmented/reassembled for us.
- The first hardware backend is Lawicel/SLCAN over USB CDC (CANable-style
  firmware).  Supported fixed rates are 125k, 250k, 500k, and 1M.

## Important limitation

A NEXIQ USB-Link 2/3 does **not** become an RP1210 adapter on Android. RP1210 is
the Windows host API. USB-Link support therefore needs a separate Android USB
backend that speaks the adapter's native USB protocol.

That backend can replace `SlcanUsbTransport` without changing the CLIP/CCAL
core or `android_j1939_transport.c`; it only needs the same four operations:

```text
open(bitrate)
close()
sendFrame(29_bit_can_id, 0..8 bytes)
receiveFrame(timeout)
```

## Build

Current project versions:

- Android Gradle Plugin 9.4.0
- Gradle 9.6
- compile/target SDK 37
- Android NDK r30 (`30.0.16248370`)
- minSdk 26

From the repository root:

```sh
gradle -p android :app:assembleDebug
```

The APK is produced under:

```text
android/app/build/outputs/apk/debug/app-debug.apk
```

## Hardware testing

This is a prototype until it is exercised on an actual vehicle/bench ECM and a
real Android USB CAN interface. Start with **download/readback** before trying
programming. The existing native programming safety checks remain in place:
CCAL CRC is verified before the CAN transport is opened, and legacy
ECH/ECHO/ENI/ELITE II programming remains intentionally blocked.
