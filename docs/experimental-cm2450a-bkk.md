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
