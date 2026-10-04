# CalPull / CalUpload - Cummins CLIP over RP1210

This is the combined C89/x86 tool for **pulling a Cummins ECM calibration to a
`.ccal` file and programming a verified `.ccal` back to the ECM** through an
RP1210/J1939 adapter.

The WinForms front end exposes both operations:

- **Pull ECM CAL** - authenticates CLIP, reads the calibration map/data, writes
  a Calterm-style `.ccal`, stamps the native Cummins file CRC, then verifies it.
- **Upload CCAL** - verifies the Cummins file CRC **before opening the RP1210
  adapter**, performs the captured Calterm programming preflight/loader
  transition, then sends the captured raw calibration-loader protocol.

## Build

Use Visual Studio Developer PowerShell or a VS Developer Command Prompt:

```bat
build_x86.bat
```

The script switches its own MSVC target to x86 if necessary.

Outputs:

```text
build\CalPull.exe
build\CalPull.exe.config
build\rp1210scan.dll
build\crc_call.exe
```

No Cummins `CRCUtility.dll` is required.  The recovered CRC implementation is
compiled directly into `rp1210scan.dll` and into the standalone `crc_call.exe`.

## CRC gate

Before upload, native code requires all three recovered Cummins checks to pass:

```text
CheckCalFileCRC
CheckHeaderFileCRC
CheckFileCRC
```

You can check a file manually with:

```bat
build\crc_call.exe check mycal.ccal
```

A failed CRC aborts before the RP1210 vendor DLL is loaded, so a bad file emits
**zero programming packets**.

## Upload flow recovered from the Calterm capture

The uploader performs:

1. RP1210 connect, J1939 address claim, and CLIP filter setup.
2. CLIP open / seed / encrypted context authentication.
3. The captured Calterm programming preflight and session-access requests.
4. `12 <seq> 00 17 00` to transition the ECM to its calibration loader.
5. Wait for the ECM CLIP close and the observed loader startup interval.
6. Raw loader identification/readback preamble.
7. Calibration stream (60-second completion timeout; the captured initial `02`
   takes about 21.96 seconds to receive its final completion):

```text
02                            enter transfer
44 00 18 04 <length32>        announce region length + 2-byte region CRC
4B ...                        short 0x00A00000 region
4D ...                        bulk region blocks, max 0x0640 bytes
44 00 0B 02 2B 16 FF FF      captured final parameter
07 FF FF FF FF FF FF FF       finish transfer
```

Each calibration region gets the CRC-16/KERMIT value observed in the capture.
The `.ccal` file-level CRC is a separate Cummins CRC and is checked before any
of the steps above begin.

## Offline validation performed

Using the supplied known-good `.ccal` and Calterm upload recording:

- native Cummins CRC: all three checks returned `True`;
- deliberately corrupted first-line CRC: upload returned `CLIP_CAL_ERR_CRC`
  with **0 send callbacks**;
- generated raw programming stream: **2,287 requests / 3,664,593 bytes**;
- compared against the Calterm raw programming stream: **2,287 / 2,287
  requests matched byte-for-byte, zero mismatches**;
- strict C89 syntax check passed for all native sources.  The only Linux test
  harness warnings are `_stricmp` declarations supplied by real MSVC headers
  on Windows.

## Important live-test boundary

The raw programming stream itself is byte-for-byte capture matched.  The only
piece not independently proven on a live ECM in this environment is the
CLIP-to-loader shutdown handshake timing around Calterm's initiator close.
Calterm's 9-byte initiator close contains five session-dependent trailing bytes;
their serializer has not been independently recovered, so this code deliberately
does **not** replay stale bytes from the recording.

Instead it waits for the ECM's close, follows the observed ~11 second loader
startup interval, and retries only the idempotent raw loader identification
sequence.  If the first live upload stops there, capture the traffic around:

```text
12 <seq> 00 17 00
81 05 04 ...
43 00 05 00 00 00 06 FF
```

That isolates the remaining handoff without risking guessed session-dependent
close data.

## Source layout

```text
Program.cs          WinForms UI: Pull ECM CAL / Upload CCAL
rp1210scan.c/.h     RP1210 API/device discovery
rp1210clip.c/.h     RP1210 transport, CLIP state, pull and programming flow
clip_crypto.c/.h    recovered CLIP seed/context cryptography
clip_cal.c/.h       readback PDU helpers + raw CCAL programming protocol
cummins_crc.c/.h    native Cummins file CRC implementation
crc_call.c          standalone CRC checker/setter CLI
rp1210scan.def      stable DLL exports for P/Invoke
build_x86.bat       x86 MSVC + WinForms build
```
