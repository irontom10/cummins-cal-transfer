# Experimental GTIS4.6 / CM2450E secure CLIP handshake PoC

**Branch:** `experiment/cm2450e-secure-clip-poc`
**Status:** bench-only authentication request experiment, **NOT** production ECM support.

The original unconditional `-115` stop on a recognized `02 02` challenge
has been replaced by a separate opt-in handler. Legacy CLIP `01 02` / TEA
handling is unaffected.

## What the branch does

1. Recognizes the verified 20-byte `02 02 6B 1F [16-byte challenge]`.
2. Reads a local operator-supplied `CLIP46_POC_FILE` config (never embedded
   into the executable or the repository).
3. Assembles a **test** 99-byte input:
   `challenge[16] || context[51] || opaque32[32]`.
4. Generates a fresh 16-byte IV using the OS cryptographic RNG.
5. Uses self-contained **ISO C89 AES-128-CBC + PKCS#7** to generate
   `IV[16] || ciphertext[112]`.
6. Sends `02 03 || body[128]` and waits for an `02 04` message.
7. Unless separately enabled, reports the 02 04 response and **stops**.
8. With `CLIP46_POC_PULL=1`, requires a separate, locally provided
   **application-session** key and IV. It then constructs
   `10 01 || AES-CBC-PKCS7(01 01 00 22 26)` and tests for a protected
   `01 01` response. The first application payload is a 16-byte block.
   It **does not** yet attempt descriptor or memory readback.
   The actual application cipher mode, session key derivation and IV are
   still subject to verification with a live INSITE crypto trace.

A `02 04` response alone does **not** prove successful authentication.
Its contents and subsequent ECDH/session state remain to be validated.

## Windows test

Pull the experimental branch:

```powershell
git fetch origin
git switch experiment/cm2450e-secure-clip-poc
.\build.bat
```

Create a **local** secret-bearing config from *your own* v1.3 AES capture.
Do this outside the checkout where possible:

```powershell
powershell -File .\tools\clip46_poc_config.ps1 `
  -TraceFile "$env:USERPROFILE\Downloads\INSITE_sensitive_buffers(2).hex" `
  -OutputFile "$env:USERPROFILE\Downloads\clip46-bench-local.cfg"
```

Launch the app with the path set in its environment:

```powershell
$env:CLIP46_POC_FILE = "$env:USERPROFILE\Downloads\clip46-bench-local.cfg"
$env:CLIP46_POC_PULL = "1"
.\build\app\CalibrationTransfer.exe
```

Connect to your **authorized bench ECM** using the ordinary *read* workflow.
Record the RP1210/J1939 traffic and the UI's final error/status message.
The first experiment may fail on the status query: we do **not**
yet know whether the ECM requires ECDH completion before these services.

The first application message is attempted only if the local configuration
has `APP_KEY_HEX` and `APP_IV_HEX`. Without those fields, the program
stops after `02 04` and **will not send another cleartext query**.
It also stops upon an encrypted reply because its decrypt/key-agreement
path is not validated yet.

Use the separate native Windows `INSITEAppCryptoTrace v1.4` to determine
the actual cipher/mode, caller, session key and IV at the first protected
INSITE request before attempting live application requests. A captured
application key or IV may be ephemeral and invalid in a new session.
**Do not select upload/programming**, run on an engine in service, or use
this during flashing.

Without `CLIP46_POC_FILE`, the software still recognizes secure CLIP but
fails with an explanatory error **before sending 02 03**. With malformed
config, it also refuses to send.

## Read-only PoC behavior

In the current branch, the optional experiment can send only service
`0x10` sequence `0x01`, with a protected application body supplied by
the experimental session crypto module. It never sends `0x12` mode
changes, unlock, loader or programming requests and never performs
memory reads while ECDH and response validation are unresolved.

A `02 04` response is **not** proof that application encryption works.
The app-encryption primitive is C89-tested with synthetic CBC vectors,
but actual GTIS4.6 cipher mode, IV policy and per-session key derivation
are not confirmed. The reusable authentication AES key MUST NOT be reused
as the application key by assumption.

## Manual config specification

```text
KEY_HEX=<32 hexadecimal digits>
CONTEXT_HEX=<102 hexadecimal digits>
OPAQUE32_HEX=<64 hexadecimal digits>
APP_KEY_HEX=<32 hexadecimal digits>  # optional; per-session, must be verified
APP_IV_HEX=<32 hexadecimal digits>   # optional; must accompany APP_KEY_HEX
```

Only strict hexadecimal without separators is accepted; omit both optional APP fields until application-key derivation has been verified. There are no hardcoded
keys or credentials in the branch.

**Critical limitation:** The last 32 bytes of the observed plaintext vary
by session, and we have **not** proven how to generate them correctly.
The helper currently copies this field from one captured session. That
may fail authenticating a fresh session. A negative response or timeout
doesn't disprove AES correctness; the AES transform itself reproduced
multiple captures exactly.

The 51-byte context fields also vary and are taken from the selected
capture for this first proof. The captured AES key was observed reused
across multiple test sessions, but its scope and provenance are unknown.

## Tests

```sh
cc -std=c89 -Wall -Wextra -Werror -pedantic -Isrc/core \
  tests/clip46_poc_test.c src/core/clip46_poc_secure.c \
  -o /tmp/clip46_poc_test
/tmp/clip46_poc_test
```

This self-contained unit test uses **synthetic** key, plaintext and IV.
No proprietary secrets, trace files or credentials belong in Git.

## Next implementation milestone

Resolve the fresh ephemeral 32-byte field, session-specific
context parameters and the `02 04` key-agreement validation. Only then
can a full secure session be declared authenticated and admitted to
read-only calibration operations.
