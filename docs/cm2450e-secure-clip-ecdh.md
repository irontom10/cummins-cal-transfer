# Experimental CM2450E / GTIS4.6 secure CLIP ECDH

**Branch:** `experiment/cm2450e-secure-clip-poc`
**Status:** experimental authentication analysis; *not* a validated live secure session.

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

## Application connection ID — evidence and automatic derivation

A combined capture (INSITE debugger timeline + passive RP1210 bus sniff, one
CM2450E, two consecutive read-only sessions, 2026-10-11) resolved the
previously open connection-ID question:

| session | tool SA | ECM frame byte 3 ("slot") | first `10 01` outer byte 4 |
|---|---|---|---|
| 1 | 0xF1 | 0x03 (every ECM frame) | 0x0B |
| 2 | 0xF9 | 0x04 (every ECM frame) | 0x0A |

The ECM's `02 04` outer byte 4 is a constant 0x08 in every observed session
(including this tool's own SA 0xFA bench run) and is transport bookkeeping,
not the application ID. The tool's byte 4 is a per-opcode value (00/02/...)
everywhere **except** the first protected `10 01`, which carries the
negotiated connection ID exactly once. Ten historical first-session INSITE
traces also used 0x0B.

The implemented rule is `connection_id = 0x0E - slot`, with the slot taken
from the ECM frames of the live session (byte 3, constant within a session).
It reproduces every observation, adapts to the connection count, and uses no
source-address mapping or hardcoded 0x0A/0x0B. Slots outside the observed
0x03..0x0B window are rejected rather than guessed, and `CLIP46_POC_CONN_ID`
still overrides the derivation for independent verification.

After the `02 04` decrypts and ECDH completes, the tool reports the observed
slot, the derived ID, and the offsets where that byte occurs in the decrypted
`02 04` body outside the trailing 32-byte peer X (metadata only). Pinning a
stable plaintext offset across sessions remains open; until then the slot rule
is a two-live-session rule that matched all available evidence.

## Limitations and strict gates

- The 51-byte context is still supplied locally; dynamic fields need live analysis.
- A syntactically valid/decrypted `02 04` does not prove that the ECM accepted
  the authentication or that an application session was successfully negotiated.
- The connection ID is now derived from the ECM frame slot as described above.
  The optional protected status query still requires `CLIP46_POC_PULL=1`;
  `CLIP46_POC_CONN_ID` remains supported as an explicit verification override.
- Even if a protected `10 01` reply decrypts, response semantics remain
  unverified. The routine always returns a non-success result; descriptor
  reads, memory readback, unlock, mode changes and programming are inaccessible.

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

On a stationary, isolated authorized ECM, run the ordinary read workflow
with only local `KEY_HEX` and `CONTEXT_HEX` configured and
`CLIP46_POC_PULL=1`; leave `CLIP46_POC_CONN_ID` unset so the derived ID is
exercised. Run the passive bus sniffer from `insite-clip-id-tracer`
(`build\clip_bus_sniff.exe`) alongside to capture the handshake, and compare
the tool's first `10 01` outer byte with the derived value. Record the
progress note listing the 02 04 plaintext offsets of the derived byte; a
stable offset across sessions upgrades the rule from slot-derived to
plaintext-confirmed. Do not log private keys or derived AES material.
Verify the `02 03` public X changes across fresh sessions and assess the
51-byte context structure and response semantics.

## Outstanding library edge-case

The stand-alone deterministic public-from-private helper rejected the valid
minimal scalar `1` in a preliminary Linux regression. The final synthetic
full-width scalar pair and a fresh OS-generated pair pass, including a mutual
ECDH shared secret and malformed-peer rejection. The minimal-scalar corner
case remains open for cryptographic review. This does not imply live ECM
verification or complete authentication.
