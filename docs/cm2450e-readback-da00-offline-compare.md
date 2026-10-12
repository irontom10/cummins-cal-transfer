# Offline comparison: pre-flash CM2450E readback vs INSITE DA00 image

Analysis date: 2026-10-12. **Offline only.** No ECM programming, credentials, or raw calibration bytes are in this document.

Input CCAL SHA-256: `0b423648504da1f0f8c586f096a2eb8e8687651c254fb857dfc7fa6af3740a36`.
This input is a generated 2026-10-11 readback for CM2450E PN 6449953, calibration 67.70.24.19 / BNN / interface 4.6.2.0, **not an original OEM INCAL**.

The uploaded 15,882,308-byte text file passed the independent vendor CRC16 check (stored/computed: `51B2`), all 206,232 Intel HEX data-record checksums, ordering checks, and the single-EOF check. Its four ranges comprise 6,599,392 bytes:

| Start–end (inclusive) | Bytes |
|---|---:|
| `00001080–0000FFF7` | 61,304 |
| `00011000–0001F8DF` | 59,616 |
| `00840000–00E6DA77` | 6,478,456 |
| `02000000–0200000F` | 16 |

Replay of the **three major memory ranges** using the existing `clip46_flash.c` framing rules, aligned against the October 11 passive OEM CAN capture on the first region announcement:

| Measure | Count |
|---|---:|
| Compared DA00 frames | 948,449 |
| Exactly matching frames | 946,517 (99.7963%) |
| Differing payload bytes | 8,482 |
| Frame-marker mismatches | 2 |

| Region | Actual image-byte differences |
|---|---:|
| `00001080` | 216 |
| `00011000` | 8,265 |
| `00840000` | **0 / 6,478,456** |

The large `0x00840000` image is **byte-for-byte identical to the OEM-transmitted image**. A separate check of the `0x02000000` 16-byte pre-announcement metadata region found **three different bytes**. The first small region's additional one-byte wire difference is in a final super-block header rather than memory-image data. Two special end-of-region header encodings remain unproven.

These differences cannot be dismissed as harmless. The supplied readback predates the OEM flash; the smaller regions likely include calibration-specific/configuration/identity/state data, but exact semantics have not been verified. Do not replay or write them blindly.

**Programming remains blocked.** Before enabling native DA00 flash, complete fresh-session SecurityAccess validation, the DA00 init/control state machine, special tail-header semantics, the OEM identity merge, transport pacing/ack handling, encrypted CLIP service-11 commit, post-flash verification and a tested recovery process. File checks and offline image agreement alone do not prove an acceptable OEM programming package.

Reference: `docs/cm2450e-secure-flash-capture.md`. Keep the actual CCAL and CAN capture private.
