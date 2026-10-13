# GTIS4.6 experimental mode

Uses the existing C89 TOML configuration store; no batch-file or environment-variable switches.

Configuration paths:

- Windows: %USERPROFILE%\.config\CalibrationTransfer\config.toml
- Linux: ~/.config/CalibrationTransfer/config.toml (or XDG_CONFIG_HOME)
- Android: app-private config.toml

Copy your operator-owned credentials into **your local configuration only**.
Do not commit keys or captured seeds to GitHub.

~~~toml
[adapter]
api = "NULN3R32"
device = 2
baud = 0

[j1939]
tool_sa = 0xFA
ecm_sa = 0x00

[experimental]
gtis46_secure_read = true
gtis46_da00_plan = false
gtis46_live_programming = true
gtis46_conn_override = 0

[gtis46-config]
tool_version = "000F"
tool_instance = "10000006"
tool_family = "<12 hex digits>"
include_optional = true
optional_byte = "02"
uds_tool_sa = "F9"
aes_key_hex = "<32 hexadecimal digits>"
KEY_HEX = "<independent CLIP 02 03 key>"
CONTEXT_HEX = "<CLIP context hex>"
~~~

## Behavior

With gtis46_live_programming=true, pressing the ordinary **Upload**
button uses a new physical J1939 connection on PGN 0xDA00 and the
configured UDS source address (F9 in the example). It performs:

1. DiagnosticSessionControl 10 02.
2. Fresh SecurityAccess RequestSeed 27 07; derives AES-CBC/PKCS#7
   SendKey 27 08 from the returned 32-byte seed and local tool metadata.
3. RoutineControl 31 01 FF00 (prepare; observes 7F 31 78 pending).
4. Four RequestDownload / TransferData / TransferExit regions in captured
   order, starting with 0x02000000 and then three contiguous CCAL regions.
   Transfers 1496 bytes of image data per 36 request with UDS sequence
   rollover and verifies the ECM's 76 acknowledgments.
5. RoutineControl 31 01 FF01 and ECUReset 11 01.

ISO-TP first/consecutive frames, flow control, block size, STmin, and
multi-frame responses use an independently testable C89 codec.
The writer verifies CRC/Intel-HEX and requires exactly three bulk
regions plus one 16-byte identity region. The earlier 24-bit raw-loader
validator is not used by this path.

If gtis46_live_programming=false and gtis46_da00_plan=true, pressing
Upload performs the existing **offline, no-transmit frame plan**.

## Actual verification status

The AES-128-CBC cryptogram with supplied tool metadata was compared
against the October 11, 2026 OEM capture: exact **32/32 bytes** for
the one recorded accepted 27 08 exchange. Fresh-session acceptance is
still to be determined on hardware. The physical programming path has
not been tested against an ECM. A rejection/error at any stage stops
transmission and reports that stage and (when available) UDS NRC.

This is NOT yet a complete INSITE-equivalent flash sequence:

- The OEM performs protected CLIP unlock/mode transitions **before**
  DA00 SecurityAccess. The new uploader does not reproduce those.
  Consequently the ECM may reject a UDS programming service.
- The OEM's final protected CLIP service-11 commit and later status
  verification are not implemented. After a fully acknowledged DA00 UDS
  transfer, the tool reports **-117: commit unverified**. The UI
  distinguishes that from a transport/programming failure.
- The OEM's INCAL-to-ECM identity/value merge is absent. A selected CCAL
  is transmitted verbatim, including its 16-byte metadata range, and
  may differ from OEM programming data.
- The code reproduces UDS transfer size/framing but has not reproduced
  every special end-of-region wire-header variant observed from the
  NEXIQ driver.

This is live experimental ECM programming: it can alter nonvolatile
memory and may leave the ECM unusable or requiring an OEM recovery.
The operator controls the feature through TOML, rather than a fixed
application policy.
