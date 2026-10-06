# Android port

This directory is the production Android home for Calibration Transfer.

The rule is simple:

```text
Android UI / Bluetooth selection
            |
            v
Android RP1210 backend
            |
            v
../core/j1939_transport.c
            |
            v
../core/clip_transfer.c and calibration protocol code
```

Do not fork or rewrite the Cummins protocol logic in Java/Kotlin. Android should provide the platform transport/UI layer and compile the same C89 core used by the Windows build.

The separate `rp1210-android-test` repository remains the hardware/transport proving ground. Once a transport behavior is proven there, the reusable Android RP1210 backend belongs here.

Vendor NEXIQ/Cummins/Kubota SDK binaries, INI files, and firmware blobs are intentionally not imported by this reorganization. Their redistribution terms should be settled before they are placed in the public Calibration Transfer repository.
