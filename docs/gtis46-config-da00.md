# GTIS4.6 experimental configuration and DA00 preparation

All GTIS4.6 test switches use the **existing** `config.toml` store.
On Windows it lives at `%USERPROFILE%\.config\CalibrationTransfer\config.toml`.
On Linux, `~/.config/CalibrationTransfer/config.toml` (or `XDG_CONFIG_HOME`).
Android continues using its app-private configuration path.

Example (populate the secrets with your own LOCAL values; do not commit):

```toml
[adapter]
api = "NULN3R32"
device = 2
baud = 0

[j1939]
tool_sa = 0xFA
ecm_sa = 0x00

[experimental]
gtis46_secure_read = true
gtis46_da00_plan = true
# Leave at zero to use dynamically computed acknowledgment count.
gtis46_conn_override = 0

[gtis46-config]
KEY_HEX = "<your local 32-hex-character key>"
CONTEXT_HEX = "<your local 102-hex-character context>"
# Other older experimental fields may remain, but are NOT an input to
# the CLIP 02 03 handshake and are not used by this code.
```

- `gtis46_secure_read` opts into fresh P-256/ECDH protected **read** sessions.
- `gtis46_da00_plan` opts into a **no-transmit** CCAL-to-DA00 framing plan
  when selecting Upload. It validates all file CRC checks, parses the CCAL
  without legacy 24-bit restrictions, counts region announcements and
  DA00 superblocks, and stops BEFORE opening an RP1210 connection.
- Missing or malformed authentication material blocks the handshake.
- The 0x02000000/16-byte identity region is tracked separately; INSITE
  carries it in a special header and merges existing ECM identity into
  the image. This plan does NOT reproduce that merge.
- The existing legacy raw loader remains available for validated
  non-GTIS4.6 calibrations.
- No environment variables are required or honored for GTIS4.6 options.

## What is not yet implemented

There is **no safe live GTIS4.6 uploader**. Passive capture leaves these
required steps unresolved: session-derived DA00 authentication `22 27 08`,
prestream init/flow control, OEM identity/value merge, end-of-region
header variants, encrypted `0x11` commit, and post-write recovery.
The code must not guess the bytes or treat successful authentication
as authorization to write. `gtis46_da00_plan = true` will finish with
an explicit not-programmed result even when its frame plan succeeds.

The old GTIS4.6-to-legacy raw-loader fallthrough was removed because
its second 24-bit layout check could abort after placing an ECM into
programming mode. Real DA00 transmission will need a separate,
verified state machine and a bench recovery procedure.
