# CM2450E secure CLIP handshake: observed wire format

A field J1939 log shows a different authentication family on a
Cummins CM2450E than the legacy four-byte-seed CLIP handshake.
This note records only what was visible in the network capture;
it does **not** imply that the authenticated protocol is implemented.

## Transport

- 500 kbit/s J1939, Proprietary-A PGN 61184 (0xEF00).
- ECM source address 0x00. The reference sessions used tool source
  addresses 0xF1 and 0xF9 (adapter/tool source addresses are configurable).
- Both families use the 0x81 guaranteed-transfer envelope.
- The connection-open exchange uses the existing 0x81 02 ... /
  0x81 01 02 ... envelope, so an older client can report a successful
  open even though it cannot finish authentication.

## Seed negotiation

The existing client's request matches the capture at the application
level:

```text
TX  81 00 03 00 00 | 01 01 00 00
```

However, the corresponding ECM application reply begins with
`02 02`, **not** the legacy `01 02`:

```text
RX  81 00 03 xx 01 | 02 02 [2 opaque bytes] [16 challenge bytes]
```

There are 20 application bytes in the observed reply. The meaning of
the two opaque bytes is unverified and the 16-byte value changes
between sessions. This must not be passed to the existing legacy
`clip_parse_seed_reply` / four-byte-seed TEA path.

After this exchange, the reference session uses a separate
`02 03` request and `02 04` response, with substantially longer,
session-dependent contents. Subsequent application commands use
opaque protected payloads. The authentication mechanism and key
material cannot be established from this network trace alone.

## Implemented in this PR

- Recognize and validate the `02 02` seed reply without waiting for the
  legacy `01 02` response until timeout.
- Report that the ECM requires a distinct authentication exchange.
- Stop before sending an incompatible legacy context or any calibration
  programming command.
- Preserve the legacy `01 02` / TEA path.
- Add C89 regression tests for both formats and malformed replies.

**Not implemented:** the `02 03` / `02 04` authenticated exchange,
secure session reads, calibration export or programming on this
family. No credentials or key-exchange behavior are inferred or
hard-coded from a single passive trace. Do not merge or advertise
this as CM2450E calibration-transfer support.

## Next verification

Obtain authorized protocol documentation or a separately validated
implementation of the secure session, then verify authentication,
read-only memory-descriptor access, address/size checks and
file-integrity handling against a test ECM. Programming must remain
blocked until independent compatibility and recovery tests pass.
