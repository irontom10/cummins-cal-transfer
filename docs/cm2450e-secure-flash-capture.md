# CM2450E / GTIS4.6 secure flash — passive capture analysis

**Capture:** `insite-clip-id-tracer/flash_capture_20261011-160358.tsv`
(1,302,489 rows, 2026-10-11 16:03:59 – 16:22:09, ~18 minutes)
**Event:** OEM INSITE installing a calibration on a CM2450E over the
USB-Link 3 WiFi adapter, watched passively (receive-only, no address claim,
no debugger attached).
**Purpose:** evidence for a future independently gated secure upload
implementation. No upload code is enabled by this document.

## Top-level architecture — what the flash actually does

The legacy raw-loader upload path (plaintext `02/44/4B/4D/07` services on
the CLIP PGN) is **not used at all**. The GTIS4.6 flash is:

1. **14 separate secure CLIP sessions** (full `02 02 -> 02 03 -> 02 04 ->
   10 01` handshake each, alternating tool source addresses 0xF1 and 0xF9),
   used for control: status queries (`10`, 1,776 total), mode changes
   (`12`, 37 total), and the commit phase (`11`, 719 total).
2. **A bulk data stream on PGN 0x00DA00** — a dedicated flash-data channel
   that does not exist in idle traffic: 948,524 eight-byte tool frames
   (0xF9 -> 0x00) between ~16:08 and ~16:15, plus 13,376 ECM frames back.

Everything on the CLIP session stayed **encrypted** (protected-session
framing); no service-`13` memory reads occurred during the flash (no
read-back verification through the CLIP application layer was observed).

## PGN 0x00DA00 — the flash data channel (fully decoded and verified)

The stream is **block framed**. Every frame is 8 bytes: byte 0 of a block's
first frame is the block type, continuation frames carry position markers
`0x21..0x2F`, and bytes 1..7 of every frame carry payload (112 payload
bytes per full 16-frame block):

```text
type 0x15 block: "DA 36 <seq>" + 109 image bytes     (16 frames)
type 0x20 block: 112 image bytes                     (16 frames) x 12
type 0x20 block: remaining bytes + 0xCC pad          (7 frames)
-------------- one super-block: 215 frames, 1496 image bytes --------------
```

- The `DA 36 <seq>` sequence number **resets to 0x01 at each region** and
  wraps mod 256 within it.
- Region announcements are type-0x10 META blocks:
  `0B 34 00 44 <address BE32> <length BE32>`.
- Between regions the tool sends a one-frame type-0x01 block (`37`).
- Region order in the captured flash: the 16-byte `0x02000000` region rode
  inside the pre-announcement header phase; then
  `[ann 0x00001080][41 supers][01 37][ann 0x00011000][40 supers][01 37]
  [ann 0x00840000][4331 supers]`.
- The final super-block of the two smaller regions carried variant headers
  (`15 BA 36 29`, `14 FA 36 28`) — two samples, semantics not yet decoded.
- ECM side: a three-frame status triplet every ~100 ms during the stream
  (`03 7F 36 78 ..`, `02 76 <n> ..`, `30 00 ..`).

**Implementation verification** (`src/core/clip46_flash.c`, tests in
`tests/clip46_flash_test.c`): framing the INCAL image with the real C code
and comparing against all 948,448 captured data-phase frames produced
**2 structural mismatches** (the two variant end-of-region headers above)
and **633 data-byte mismatches (0.0095%)**, all accounted for by the
OEM tool's identity/calibration-value merge into the template. The framing
layer is byte-exact.

**The identity merge**: the flashed image is the INCAL template with the
ECM's existing identity fields re-inserted (observed: real part numbers
and "Customer Name" fields where the INCAL carries placeholders, plus the
ECM's tuned calibration values around `0x00E0BC92`). A safe uploader must
reproduce this merge — flashing a raw template would clear the ECM's
identity.

## CLIP-session command timeline

```text
16:04:13  session 1 (0xF1): auth, 10 01
16:04:17  session 2 (0xF9): auth, 10 01, 12 03 (0x000B state),
          12 0C, 12 16, then unlock-family mode changes:
          12 94, 12 9C, 12 9D, 12 A0, 12 A1 (dynamic sequences; the legacy
          lock/unlock family used 12 <seq> 00 18 <value>)
16:05-16:07  sessions 3-6: status polls (10 01 every ~24 s), 12 24, 12 3F
16:08-16:15  THE BULK STREAM on PGN 0x00DA00 (session stays open)
16:16:10  three fresh sessions in quick succession (0xF1, 0xF1, 0xF9)
16:16:23  commit phase: 719 x svc=11 commands, sequences 05..FF then
          wrapping 00..03, ~6.5/s, interleaved with 12 E8 / 12 F7 / 12 04
          and one 10 01 status check at 16:18:12
16:18:17-16:18:27  final 12-sequence (29, 2A, 2B, 2C, 2D, 2E, 2F, 31) -
          plausibly the lock/verify/reboot ramp; semantics unverified
16:21:41  two post-flash sessions: auth, 10 01, 12 03, 12 0C, 12 14, 12 5F
          (post-programming verification/status)
```

## Byte-4 block-count rule — third independent confirmation

Every session's first `10 01` carried exactly ceil(02 04 wire length / 16):

| session type | 02 04 length | first 10 01 byte 4 |
|---|---|---|
| 0xF1 sessions (all) | 167 | 0x0B |
| 0xF9 sessions (all) | 151 | 0x0A |

This settles the earlier "connection ID" question conclusively: INSITE
itself derives byte 4 mechanically per session from the `02 04` size, which
is why the value alternates between its two source addresses.

## Staged flash experiment (CLIP46_POC_FLASH=1)

`clip46_staged_flash()` in `clip_transfer.c` tests whether the CM2450E
accepts the **legacy loader transition** through the secure codec — a
much simpler path than the DA00 streaming protocol INSITE uses:

1. secure session (requires `CLIP46_POC_PULL=1` and `CLIP46_POC_FILE`)
2. multi-query + `0x000B` state entry (the observed session shape)
3. legacy-format unlock/lock/unlock dance (`12 <seq> 00 18 <value>`)
4. **legacy loader transition** `12 <seq> 00 17 00` — if the ECM
   accepts it, it drops the CLIP session and boots the raw calibration
   loader; the existing legacy upload path (raw preamble, `4B`/`4D`
   writes, verify, re-lock) can then take over unchanged

**No image data is ever sent by this experiment.** A positive reply
means the full legacy programming path should work; a negative reply
means the CM2450E requires the DA00 streaming protocol and the auth
block / svc-11 derivation work must continue. Run via `test_flash.cmd`.

## Implementation status

- `src/core/clip46_flash.c/.h` implements the verified DA00 framing
  (blocks, super-blocks, region announcements, status parsing) with
  synthetic tests; it is framing only, no ECM I/O.
- Remaining unknowns before any upload can be enabled, each explicitly
  gated: the per-session DA00 authentication block (`22 27 08` + 32
  session-derived bytes), the pre-stream init command semantics, the
  unlock `12` command sequence, the encrypted service-`11` commit phase,
  the end-of-region header variants, and the identity-merge map.
- The DA00 stream is unencrypted on the wire; the CLIP session carries the
  encrypted control plane.
- Upload remains intentionally blocked in this repository until the
  command semantics are independently established and a recovery-tested
  implementation exists.

## INSITE-side architecture (send-path trace, 2026-10-11 17:11)

A hardware breakpoint on `NULN3R32.dll!RP1210_SendMessage` during an INSITE
flash identified the full send-side call structure:

| Component | Role | Site |
|---|---|---|
| `PCLWrapper.dll` | CLIP PGN 0xEF00 control plane (session, unlock `12`s, queries) | send ret `+0x00A3A2C0`, caller `+0x00A39AD0` |
| `caldnld.dll` | **the flash engine**: DA00 init commands, region announcements, BNN metadata, the DA00 auth block, and the 1504-byte super-block messages | send ret `+0x0066B7C0`, caller `+0x0066AFD0` |

`C:\Intelect\INSITE\caldnld.dll` (18,736,672 bytes, SHA-256
`15a2681692fb16c7a0c1600e4f9adcae7f98b17e771347d842276d9c90921fa9`).
The DA00 auth block was captured at the RP1210 level: message length 40,
block `22 27 08 <32 session-derived bytes>` — a second sample confirming
the bytes differ per session (`54 44 47 01 ...` vs the bus capture's
`35 D2 B1 C2 ...`).

caldnld's DA00-family RP1210 messages use a non-standard header:
`[01][18][DA][..][..]` followed by the wire block payload from byte 5
(e.g. the 1504-byte super messages carry `DA 36 <seq>` + 1496 image
bytes). The NEXIQ driver adds the wire block-type/position framing when
segmenting to 8-byte PGN 0xDA00 frames. The trace also captured the
region announcement (`.. 34 00 44 02 00 00 00 ...` = 0x02000000) and the
BNN metadata block at the same call site.

**Remaining work for the auth block:** disassemble caldnld.dll at
`+0x0066B7C0` / `+0x0066AFD0` and walk the caller chain to the
construction of the 32 bytes. Two full samples now exist (both flashes).

## Capture provenance and safety

Passive RP1210 sniffing only (client with all-filters-pass, no transmit,
no J1939 address claim). No debugger is attached to INSITE during the
bulk image stream: the send-path tracer self-disarms at the first DA00
bulk message. The captures contain calibration image bytes on PGN
0x00DA00; keep them local and uncommitted, like all bench traces.
