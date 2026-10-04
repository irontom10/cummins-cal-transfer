# ECH / ECHO calibration readback

ECH/ECHO controllers use the legacy raw Proprietary-A service family rather
than the newer guaranteed CLIP application envelope.

## Detection

After a controller rejects the CLIP-open probe, the legacy discriminator reads
parameter `0x0043`:

```text
43 00 43 00 00 00 03 FF
44 00 43 03 45 43 48 FF
```

The ASCII payload `45 43 48` identifies ProductID `ECH`.

## Readback sequence

The validated readback flow is:

```text
04 FE FE FF FF FF FF FF    open calibration transfer
0C 04 FE FE ...            acknowledgement

43 00 2E 00 00 00 30 FF   read calibration descriptor
44 00 2E 30 ...            descriptor reply

4C <address32> <length32>   read calibration memory
4D <address32> <length32> <data...>

05 FE FE FF FF FF FF FF    close calibration transfer
0C 05 FE FE ...            acknowledgement
```

Descriptor `0x002E` uses the same width-tagged range-table shape as the other
legacy raw platform:

```text
u16 payload_length
u16 flags
u16 range_count
u16 address_width
u32 start[range_count]
u16 length_width
u32 length[range_count]
```

The supplied trace describes:

```text
00008000 + 00038000
00040000 + 00024D86
01000080 + 00001AB8
```

The first two ranges are contiguous in the resulting Intel-HEX image.

## Metadata

The legacy header is populated from raw parameter reads:

```text
0019  CalibrationVersion
0000  ModuleID
0043  ProductID
0001  ModulePN
0044  MarketID
002D  InterfaceLevel
0046  StartBootLoaderVersion
0047  EndBootLoaderVersion
0028  EngineID
0029  FuelSystemID
002A  ByteOrder
002B  AddressLength
002C  CPPDataLink
```

## Programming

Programming is intentionally not implemented for ECH/ECHO.

The supplied validation trace is a readback session: the tool requests memory
with `4C`, and the ECM returns calibration data with `4D`. It does not prove
a safe write/programming sequence. The public upload entry point therefore
blocks this family before programming preflight or loader traffic.
