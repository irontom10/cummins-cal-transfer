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

## Limitations and strict gates

- The 51-byte context is still supplied locally; dynamic fields need live analysis.
- A syntactically valid/decrypted `02 04` does not prove that the ECM accepted
  the authentication or that an application session was successfully negotiated.
- The application connection ID is still unresolved. No automatic extraction
  from outer envelope bytes is permitted. A single opt-in protected status query
  requires both `CLIP46_POC_PULL=1` and a session-correct
  `CLIP46_POC_CONN_ID` override from independent evidence.
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
with only local `KEY_HEX` and `CONTEXT_HEX` configured; leave
`CLIP46_POC_PULL` unset. Capture the J1939 handshake and the tool's
non-success status; do not log private keys or derived AES material.
Verify the `02 03` public X changes across fresh sessions and assess the
51-byte context structure and response semantics. Do not enable the
protected status query until the negotiated application connection ID
has been independently identified for that session.

## Outstanding library edge-case

The stand-alone deterministic public-from-private helper rejected the valid
minimal scalar `1` in a preliminary Linux regression. The final synthetic
full-width scalar pair and a fresh OS-generated pair pass, including a mutual
ECDH shared secret and malformed-peer rejection. The minimal-scalar corner
case remains open for cryptographic review. This does not imply live ECM
verification or complete authentication.
