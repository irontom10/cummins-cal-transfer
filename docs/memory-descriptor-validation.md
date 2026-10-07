# Calibration memory descriptor validation

## Why this exists

Issue #19 exposed a bad assumption in the CLIP calibration-download parser.  A
live ECM returned this descriptor payload after the normal `01 <seq>` positive
reply header:

```text
00 46 06 72 00 05 00 04
00 01 00 00 00 50 00 00 00 60 00 00 00 64 00 00 01 00 02 80
00 04
00 06 00 00 00 0F 00 00 00 00 49 30 00 0C 00 00 00 00 1D 7F
00 04
00 05 11 F8 00 0D 4C A4 00 00 49 30 00 07 6E 30 00 00 0B A4
```

The old parser interpreted the two 16-bit values after `0x00460672` as two
range counts and rejected the reply because they were `5` and `4`.

That interpretation was wrong.  The second value is an element width.

## Measured layout

The descriptor is width-tagged:

```text
u32 descriptor_value
u16 range_count
u16 address_width
address[range_count][address_width]
u16 auxiliary_width
auxiliary[range_count][auxiliary_width]
u16 length_width
length[range_count][length_width]
optional trailer
```

All multi-byte values measured so far are big-endian.

For the live descriptor above:

```text
descriptor_value = 0x00460672
range_count      = 5
address_width    = 4
auxiliary_width  = 4
length_width     = 4

addresses:
  0x00010000
  0x00500000
  0x00600000
  0x00640000
  0x01000280

auxiliary:
  0x00060000
  0x000F0000
  0x00004930
  0x000C0000
  0x00001D7F

lengths:
  0x000511F8
  0x000D4CA4
  0x00004930
  0x00076E30
  0x00000BA4
```

The auxiliary array is deliberately treated as opaque.  It is preserved in the
parsed map but is not used to accept or reject a descriptor.

## Offline corpus measurement

To check that the live ECM was not a one-off, an independent offline scanner
was run against the December 2025 INCAL media.  The scanner did not link
against `clip_cal.c`; it independently reconstructed Intel HEX regions and
looked for width-tagged descriptor tables.

The scan produced 44,175 calibration-payload rows.

| Measurement | Result |
| --- | ---: |
| Parseable width-tagged descriptors | 37,378 |
| 3-range / 4-4-4 width descriptors | 13,501 |
| 4-range / 4-4-4 width descriptors | 20,301 |
| 5-range / 4-4-4 width descriptors | 3,576 |
| Exact descriptor-to-HEX region matches | 37,256 |
| Exact-match rate among detected descriptors | 99.67% |
| Detected descriptors accepted by current parser | 37,378 / 37,378 |
| Exact matches with auxiliary < length | 264 |
| Descriptor/HEX mismatches | 122 |
| Payload rows with no descriptor candidate | 6,797 |

Nine descriptor values were observed:

```text
0x003A06D6  19,194
0x002E06D6  12,473
0x00460672   2,633
0x003A0672   1,097
0x004600F0     943
0x002000F0     585
0x002E00F0     380
0x002E0672      63
0x003A00F0      10
```

The 122 non-exact matches were all ECH-family payloads and all also carried the
scanner's `unknown_ihex_type` condition.  They are therefore not evidence for
a different descriptor layout; the independent scanner did not reconstruct all
ECH record types in those files.

The 264 `auxiliary < length` cases were exact region matches and were all
CM850-family payloads in this scan.  This is direct evidence that the auxiliary
array must not be treated as a simple maximum-size/capacity array.

Some older CCAL wrappers in this particular scan were not metadata-decoded by
the first scanner build.  That affects fields such as module name and product
ID in the report, but not the independently reconstructed Intel HEX regions or
descriptor bytes used for the measurements above.

## Parser fix

`clip_cal_parse_memory_descriptor()` now:

- reads `range_count` and each array's element width separately;
- accepts element widths from 1 through 4 bytes;
- decodes address, auxiliary, and length arrays independently;
- validates range count, descriptor bounds, non-zero lengths, and address/length
  overflow;
- preserves the auxiliary field without assigning unsupported semantics;
- preserves optional trailing bytes.

The original 74-byte live descriptor is a regression test.  Additional
distilled corpus vectors cover all measured range-count/width shapes:
3/4-4-4, 4/4-4-4, and 5/4-4-4.  A separate regression test ensures
`auxiliary < length` remains accepted.

## Scope

These measurements validate the descriptor parser used by calibration
**download/readback**.  They do not by themselves establish programming/write
semantics for ECM families whose upload path is intentionally blocked.
