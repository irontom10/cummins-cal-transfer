# Calibration Transfer diagnostic logging

Event and error logging is on by default. Detailed J1939/RP1210 byte logging is opt-in.

## Contents

The event log records session start/end, adapter/baud/address metadata,
progress checkpoints, RP1210 filter/address commands and return codes,
protocol/CRC/connection errors, and the final result.

With CAN tracing enabled, the bus log contains:
- TX: every outbound J1939 logical payload, PGN, priority, source/destination.
- RX-RAW: every RP1210 receive envelope returned by the adapter, with all bytes.
- RX: each message that matches the J1939 session filter, decoded into PGN,
  priority, source, destination, length, and full hex payload.

Each record carries local wall time and a monotonic millisecond tick. This
captures RP1210/J1939 logical traffic **not promiscuous physical CAN frames**:
the adapter filter and transport protocol may hide other frames and TP segments.

## Windows (default WinAPI)

Turn on the "Capture full CAN trace (sensitive)" checkbox before a transfer.
Logs go to %TEMP%\CalibrationTransfer.log and,
when selected, %TEMP%\CalibrationTransfer-can.log.
This checkbox defaults to off at every program launch.

## Android

Enable the equivalent checkbox on the transfer screen.
Logs are app-private at files/caltransfer.log and files/caltransfer-can.log.
For debuggable builds, retrieve them with ADB:
adb exec-out run-as com.irontom10.calibrationtransfer cat files/caltransfer.log
adb exec-out run-as com.irontom10.calibrationtransfer cat files/caltransfer-can.log

## Other frontends

Without a frontend configuration, paths default to caltransfer.log and
caltransfer-can.log relative to the working directory. Set CT_LOG_FILE and
CT_LOG_CAN_FILE environment variables to override their destinations.
CT_LOG_CAN=1 enables full trace if the frontend does not explicitly set it.
Frontend trace settings override CT_LOG_CAN.

## Safety / limitations

**Raw bus logs can expose challenge-response bytes, credentials, unlock
operations, device IDs and full calibration payloads. Never post them
publicly without careful review/redaction.**

Logger failures never abort calibration transfer. Logging is synchronous,
C89, and designed for one transfer worker at a time. Full CAN tracing flushes
every line to disk and may reduce upload throughput. Log files append and are
not automatically size-capped/rotated; rotate manually as needed.
*.log is ignored by .gitignore.

## Smoke test

cc -std=c89 -Wall -Wextra -Werror -pedantic -Isrc/core \
  tests/ct_logger_test.c src/core/ct_logger.c -o ct_logger_test
./ct_logger_test
