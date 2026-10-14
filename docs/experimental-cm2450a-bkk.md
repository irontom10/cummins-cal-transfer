# Experimental CM2450A/BKK high-address short region

**Experimental branch only. Do not merge to master as a proven live upload.**

## Observed file layout

A CM2450A / BKK .ccal that passes the file-level CRC and Intel HEX checks
contains four ordered regions:

| Start | Bytes | Upload path |
| --- | ---: | --- |
| `0x00001080` | 61,304 | 24-bit `0x4D` bulk |
| `0x00011000` | 54,752 | 24-bit `0x4D` bulk |
| `0x00840000` | 5,617,016 | 24-bit `0x4D` bulk |
| `0x02000000` | 16 | experimental 32-bit `0x4B` short |

The sample file's CRC token was `1636` and its computed CRC matched.
No calibration contents are checked into this repository.

## Scope of the change

`src/core/clip_cal.c` continues to accept the legacy `0x00A00000`
short region. On this branch **only**, it additionally recognizes a terminal
`0x02000000` short region with **exactly 16 data bytes** in a **four-region**
image. It reuses the existing `0x4B` command that carries a 32-bit address,
with the 16-byte payload plus a two-byte CRC-16/KERMIT suffix.

Other addresses above the normal 24-bit boundary remain rejected.
The original 24-bit `0x4D` bulk length/address bounds are unchanged.
The short region is transmitted first by the existing sender, despite its
last-file-region position. That command order is a **hypothesis**, not a
verified CM2450A/BKK write trace.

## Tests and validation boundary

The C89 protocol tests use **synthetic** file contents. They verify:
- preflight accepts this one new shape;
- the emitted `0x4B` request encodes all four bytes of `0x02000000`;
- the sender declares 18 bytes (16 data + two CRC bytes), followed by three
  bulk writes at the expected addresses;
- metadata lengths 15 and 17, wrong high address, and missing bulk region
  are rejected; the rejected image sends zero messages;
- existing three-region/legacy short-region paths remain intact.

Run the local protocol regression suite:

```sh
mkdir -p build/tests
cc -std=c89 -Wall -Wextra -pedantic -Isrc/core \
  tests/clip_cal_protocol_test.c src/core/clip_cal.c \
  src/core/ccal_crc.c src/core/ct_platform.c \
  -o build/tests/clip_cal_protocol_test
build/tests/clip_cal_protocol_test
```

**Not proven:** live CM2450A loader acceptance of the new `0x4B` address,
correct ordering, or recovery after interrupted programming. The test
transport only supplies mocked acknowledgements. Successful structural
preflight and mock writes do not make a live flash safe.

## Experimental already-unlocked response (2026-10-14)

`caltoollog.6.jlog` captured these two application-level transactions:

```text
12 16 00 18 00  ->  01 16 FF FC   (attempted unlock)
12 17 00 18 01  ->  01 17 FF FC   (attempted rollback/re-lock)
```

Neither is the proven successful `01 <seq> FF FF` acknowledgement.
There are **no raw loader-entry requests or calibration writes** in this capture.
The same `FF FC` reply appears on a requested lock, so the exact semantic
meaning of this status is not independently proven.

On this branch only, for a requested **`0x0018` unlock (`value=0`)**,
an exact matching `01 <seq> FF FC` reply is treated provisionally as
"possibly already unlocked"; the upload coordinator logs that fact and
continues to the ordinary upload preflight/loader transition. It does **not**
treat FF FC as proof that the ECM is unlocked.

A requested **re-lock (`value=1`)** still requires `01 <seq> FF FF`.
Unexpected replies, wrong sequence, short messages, unrelated negative
responses and transport errors still abort. An error before loader entry
does not send an automatic re-lock when the initial response indicated an
already-unlocked condition (this preserves the initial state, as far as
the tool can tell). Loader failure and post-upload recovery retain their
existing confirmation requirements.

**Live boundary:** a parser accepting FF FC is not proof that the CM2450A
permits programming. Do not claim success without the raw loader handshake,
a complete transfer, application recovery, and a confirmed re-lock.
