# ECH / ECHO download validation

ECH/ECHO support is based on the supplied raw J1939 transfer capture and its
matching calibration file.

## Platform identification

The capture uses J1939 Proprietary-A PGN 61184 between tool source address
0xFA and ECM address 0x00. The ECH product identifier is read with the legacy
parameter service:

```text
43 00 43 00 00 00 03 FF
44 00 43 03 45 43 48 FF
```

The returned ASCII value is `ECH`. Metadata later identifies the engine
family as `ECHO`.

## Transfer sequence

The observed read sequence is:

```text
04 FE FE FF FF FF FF FF    enter calibration transfer
0C 04 FE FE FF FF FF FF    acknowledgement

43 00 2E 00 00 00 30 FF    read descriptor 0x002E
44 00 2E 30 ...             descriptor reply

4C <addr32> <len32>          block memory read
4D <addr32> <len32> <data>   block memory reply

05 FE FE FF FF FF FF FF    leave calibration transfer
0C 05 FE FE FF FF FF FF    acknowledgement
```

Reads are issued in blocks of at most 1000 bytes.

## Descriptor 0x002E

The 48-byte descriptor contains three address arrays:

```text
u16 payload_length
u16 flags
u16 range_count
u16 address_width
be32 start[range_count]
u16 allocated_length_width
be32 allocated_length[range_count]
u16 used_length_width
be32 used_length[range_count]
```

For the supplied ECH ECM the ranges are:

```text
start       allocated   used
0x00008000  0x00038000  0x00038000
0x00040000  0x00030000  0x00024D86
0x01000080  0x00001F7E  0x00001AB8
```

Only the used lengths are read. The resulting memory image contains 387,134
bytes in two contiguous runs:

```text
0x00008000 - 0x00064D85
0x01000080 - 0x01001B37
```

## Metadata mapping

The reference transfer reads these parameters for the ECHO compatibility
header:

```text
0x0019  CalibrationVersion
0x0000  ModuleID
0x0043  ProductID
0x0001  ModulePN
0x0044  MarketID
0x002D  InterfaceLevel
0x0046  StartBootLoaderVersion
0x0047  EndBootLoaderVersion
0x0028  EngineID
0x0029  FuelSystemID
0x002A  ByteOrder
0x002B  AddressLength
0x002C  CPPDataLink
```

## File validation

The ECHO writer reproduces the reference INI-style header, type-FF record,
16-byte Intel HEX rows, extended linear address records, and four-character
whole-file CRC layout.

Reconstructing the supplied calibration from the captured 0x4D memory replies
produced exactly 1,089,418 bytes, byte-for-byte identical to the supplied
reference `.ccal`.

Only download/readback is enabled. ECH/ECHO programming remains disabled until
a programming trace is independently validated.
