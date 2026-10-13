> **2026-10-13 update:** The experimental branch now uses config.toml, not CLIP46_POC_* environment variables; see [current configuration](gtis46-config-da00.md). Earlier test commands below are historical.

# Experimental CM2450E / GTIS4.6 secure CLIP ECDH

**Branch:** `experiment/cm2450e-secure-clip-poc`
**Status:** experimental; a live bench session (2026-10-11) completed the full
`02 02 -> 02 03 -> 02 04` handshake with fresh ECDH, derived the connection ID
from the announced slot 0x03, and exchanged a protected application
query/reply that decrypted correctly. Read-only calibration readback over the
protected session is implemented and awaits live validation; programming
remains intentionally blocked.

## Protected application framing — bench verified

The legacy CLIP application PDU is carried transparently: its first two
bytes (service, sequence) are transmitted in the clear as the visible
prefix, and the remaining PDU bytes are AES-128-CBC/PKCS#7 encrypted with
the ECDH-derived application key and IV. The proven first protected request
`10 01 || ct(01 01 00 00 84)` is exactly
`clip_cal_build_query_request(0x01, 0x000084)` split at byte 2, and the
bench-decrypted reply confirms the same rule in the ECM direction
(visible `01 <seq>`, encrypted remainder). `clip_exchange()` routes through
`clip46_exchange()` when the protected session is active, so `clip_cal`
request building and reply parsing are unchanged. Outer byte 4 is the
derived connection ID on the session's first request, 0x00 on service-0x12
state requests, and the dominant observed 0x02 otherwise; visible 0x03
negative replies are handled like the legacy exchange.

The secure readback flow after authentication is: the verified 0x0084 status
query (sequence 01), the multi-query (02), the 0x000B state entry (03), then
descriptor, metadata, and memory reads through the same codec. The legacy
preflight probes and separate MAIN phase entry were not observed in the
secure reference session and are skipped. `rp1210_upload_ccal()` refuses to
program over a GTIS4.6 secure session.

**Byte 4 is a transport block-count acknowledgement (bench-proven).**
Outer byte 4 of each request acknowledges the transport size of the
previous inbound reply: `ceil(wire_length / 16)` blocks. The reference
capture confirms it directly (23-byte reply -> 0x02, 823-byte reply ->
0x34), and the first protected request's 0x0B/0x0A values are exactly
this count for the observed 167/151-byte `02 04` replies. The earlier
"connection ID" interpretation of those bytes — including the
`0x0E - slot` derivation that briefly lived here — was a coincidence
between the announced slot and the `02 04` envelope sizes, disproved by
two live stall runs: when the tool misreported the count (hardcoded 0x02
on reads, 0x34 on a synthetic ack), the deficit accumulated and the ECM
stopped answering after roughly 180 unacknowledged blocks (~3 KB of
consecutive misreported 1 KB reads, then ~16 KB with mildly misreported
counts). The codec now computes the count mechanically from every
received reply, which also removes the need for any special
flow-control request; the reference tool's "0x34 queries" were ordinary
requests carrying their accurate counts. `CLIP46_POC_CONN_ID` remains
as an override of the first request's byte 4 for verification.

That session family also confirmed end-to-end: slot 0x03 announcement,
descriptor (with region-table logging), nine metadata queries, and
memory reads all flowing over the protected session.

## Implemented

The normal `02 02 -> 02 03 -> 02 04` handshake remains on the OEM protocol path.
For each `02 02` challenge the tool generates a fresh P-256 key pair using checked
OS entropy, retains its private scalar in RAM, and transmits the fresh public X
coordinate as plaintext offsets 67-98 in `02 03`.

`02 04` is decrypted using locally provided authentication-envelope AES credentials.
Only strict PKCS#7 and the observed 124 / 136-byte plaintext structures are
accepted. The last 32 bytes are interpreted as the ECM public X coordinate;
both compressed-point parities are independently validated. ECDH must produce
the same raw 32-byte X coordinate for both parities (an elliptic-curve
negation identity). The application IV = shared[0..15], and AES-128 key =
shared[16..31]. No SSKDF is added. Private scalars and derived material are
wiped after this experiment, including failure paths.

All AES work is ISO C89. P-256 uses the BSD-2-Clause micro-ecc source vendored
under `src/core/third_party/micro_ecc/` and compiled into `clip46_p256.c`.
The platform shim is modified to force an explicit checked OS RNG callback:
CryptoAPI on Windows (x86) and `/dev/urandom` on Linux/Android. No x64 OpenSSL
binaries are required by Windows.

## Byte 4 — investigation record

A combined capture (INSITE debugger timeline + passive RP1210 bus sniff, one
CM2450E, two consecutive read-only sessions, 2026-10-11) initially suggested
a connection-ID rule from the ECM frame slot (byte 3):

| session | tool SA | ECM frame byte 3 ("slot") | first `10 01` outer byte 4 | `02 04` wire length |
|---|---|---|---|---|
| 1 | 0xF1 | 0x03 (every ECM frame) | 0x0B | 167 (ceil = 11 = 0x0B) |
| 2 | 0xF9 | 0x04 (every ECM frame) | 0x0A | 151 (ceil = 10 = 0x0A) |

The `0x0E - slot` derivation reproduced both rows and ten historical
first-session traces, and slot 0x03 is still announced in the CLIP link open
(`81 02 01 03 FF 01 FF 00 00`) because it matches the reference tool's
first-session behavior. The rule was retired when the 16016-byte stall run
showed byte 4 must be the block-count acknowledgement: the slot correlation
only worked because every observed slot-03 session also happened to carry a
167-byte `02 04`. `clip46_poc_derive_connection_id()` is retained with its
tests as the investigation record.

## Limitations and strict gates

- The 51-byte context is still supplied locally; dynamic fields need live analysis.
- A decrypted `02 04` does not by itself prove that the ECM accepted the
  authentication; the protected status query and readback traffic are the
  live evidence being collected.
- Byte 4 is the mechanical block-count acknowledgement described above.
  The optional protected session still requires `CLIP46_POC_PULL=1`;
  `CLIP46_POC_CONN_ID` overrides the first request's byte 4 for verification.
- The secure readback flow (multi-query, 0x000B state entry, descriptor,
  metadata, memory reads) follows the observed reference-session shape but
  has not yet completed a full live calibration download; each step reports
  precise errors if the ECM rejects it.
- Secure calibration upload/unlock/programming is intentionally blocked.
  A passive capture of a full OEM CM2450E flash (2026-10-11) shows the
  GTIS4.6 upload uses a completely different mechanism from the legacy
  raw loader: encrypted CLIP-session control (unlock `12` commands and a
  719-command `11` commit phase) plus an unencrypted bulk image stream on
  PGN 0x00DA00. See [cm2450e-secure-flash-capture.md](cm2450e-secure-flash-capture.md).
  The raw-loader preflight also rejects CM2450E pulls independently: the
  0x02000000 identity region lies outside the captured 24-bit programming
  range, and the 4-range layout matches neither captured loader layout.

## Local config — never commit any real credentials

Set `CLIP46_POC_FILE` to an operator-owned file outside the repo containing:

```text
KEY_HEX=<32 hexadecimal digits>
CONTEXT_HEX=<102 hexadecimal digits>
```

`OPAQUE32_HEX`, `APP_IV_HEX`, and `APP_KEY_HEX` are now rejected.

The existing PowerShell helper `tools/clip46_poc_config.ps1` now exports
only `KEY_HEX` and `CONTEXT_HEX` from a local trace. The captured 32-byte
field is never exported. Its source context may still be session-specific;
review that independently before a live test. Do not put generated configs
under source control.
Captured application keys/IVs and peer points from previous sessions must
not be supplied or reused.

## Compile synthetic tests

```sh
cc -std=c89 -Wall -Wextra -pedantic -Werror -Isrc/core \
  tests/clip46_poc_test.c src/core/clip46_poc_secure.c -o /tmp/clip46_aes_test
/tmp/clip46_aes_test
cc -std=c89 -Wall -Wextra -pedantic -Werror -Isrc/core \
  tests/clip46_p256_test.c src/core/clip46_p256.c \
  src/core/clip46_poc_secure.c -o /tmp/clip46_p256_test
/tmp/clip46_p256_test
```

CI also checks x86 MSVC and Android NDK C90 compile paths. All fixtures
use synthetic scalars, not captured traces.

## Next authorized bench run

On a stationary, isolated authorized ECM, run `test_gtis46.cmd` (or the app
with `CLIP46_POC_FILE` and `CLIP46_POC_PULL=1`) and use the ordinary
download workflow: the protected session opens during authentication and
readback continues through the secure codec. Keep the passive bus sniffer
running and record where the flow stops if the ECM rejects a step; the
error text names the exact service and sequence. Compare the tool's
requests against the reference capture (`10 <seq>` queries, `12 <seq>`
state entries, big descriptor/memory replies) and record the reported
02 04 plaintext offsets to pin the connection-ID field across sessions.
Do not log private keys or derived AES material.

## Outstanding library edge-case

The stand-alone deterministic public-from-private helper rejected the valid
minimal scalar `1` in a preliminary Linux regression. The final synthetic
full-width scalar pair and a fresh OS-generated pair pass, including a mutual
ECDH shared secret and malformed-peer rejection. The minimal-scalar corner
case remains open for cryptographic review. This does not imply live ECM
verification or complete authentication.
