# M7083 BLE runtime probe design

Status: approved in chat on 2026-10-01; awaiting review of this written spec.

## Purpose and success criteria

The ESP32-C3 gateway should connect to the verified HAVIT M7083 ring and run a
repeatable diagnostic sequence visible in `idf.py monitor`. The user will use the
output to establish which Colmi/QRing queries this firmware actually answers
before defining reading models, JSON, or MQTT behavior. A successful run shows
the selected peer, GATT channels found, each exact transmitted packet, every
received notification in hex, and a per-query result of response, no data,
timeout, unsupported GATT channel, or transport error. The firmware must never
infer that an untested command works merely because the battery query worked.

The first target is the user's M7083_7904. Selection uses a configurable exact
advertised name (default `M7083_7904`), with an optional configured BLE address
when needed. The probe
must not silently connect to a different M7083 or to the R09. The existing mock
MQTT mode remains selectable; probe mode does not publish ring data.

## Chosen approach

Implement a boot-time, sequential probe mode. This is smaller and easier to
reproduce than a serial command shell. A full ring parser and JSON pipeline
would hide the raw evidence needed at this stage and is deferred.

`RingBleClient` owns NimBLE initialization, scanning, peer selection, connection,
GATT discovery, subscriptions, characteristic writes, disconnection, and link
events. It exposes the normal 16-byte command channel and a separate Big Data
channel without deciding what a payload means. `ColmiProtocol` builds 16-byte
packets and checks notification length/checksum. A `RingProbe` runner defines
the requests, waits for responses, logs raw packets/chunks, applies bounded
timeouts, and prints a session summary. The BLE callbacks do minimal work and
pass data to the runner; no blocking wait or serial formatting occurs in a BLE
callback.

On discovery, log whether these characteristics exist and support the needed
properties:

| Channel | Service | Write | Notify |
| --- | --- | --- | --- |
| Command | `6e40fff0-b5a3-f393-e0a9-e50e24dcca9e` | `6e400002-b5a3-f393-e0a9-e50e24dcca9e` | `6e400003-b5a3-f393-e0a9-e50e24dcca9e` |
| Big Data | `de5bf728-d711-4e47-af26-65e3012a5dc7` | `de5bf72a-d711-4e47-af26-65e3012a5dc7` | `de5bf729-d711-4e47-af26-65e3012a5dc7` |

The command packet is `[command, 14 payload bytes, checksum]`. The verified
battery request is `03 00 ... 00 03`. Use an 8-bit byte sum (`sum & 0xff`) for
new packets, as the reference implementations do. The battery capture cannot
distinguish that from `% 255`, since its sum is below 255. Log checksum failures
but still print the raw received packet, so firmware differences remain visible.
Do not apply this 16-byte checksum rule to Big Data frames.

## Probe sequence

Run one request at a time, with a label, packet bytes, response count, elapsed
time, and a finite wait. Continue after a failed or timed-out probe when the
link remains connected. On a lost link, report remaining probes as skipped and
end the session. Run the sequence once per boot to avoid repeated sensor
activation and excessive history output.

| Probe | Wire action | Expected evidence |
| --- | --- | --- |
| Battery | Command `0x03` | Raw `0x03` response; show byte 1 as a candidate percent and byte 2 as a candidate charge flag. |
| Device information | Read available standard Device Information GATT values | UUID, raw bytes, and printable text when present; absence is reported. |
| HR log settings | Read command `0x16` | Raw settings response, without changing sampling interval. |
| Steps/activity | Command `0x43` for ring day offsets 0 and 1 | Header, all detail packets, no-data marker or timeout. |
| HR history | Command `0x15` for today and yesterday | Header and indexed response packets; no-data marker or timeout. |
| HRV history | Experimental command `0x39` for today | Raw multi-packet response or explicit lack of response. |
| Sleep history | Big Data ID `0x27`, all retained days | Every fragment plus total received length and completion status. |
| SpO2 history | Big Data ID `0x2a`, all retained days | Every fragment plus total received length and completion status. |
| Live HR | Command `0x69` start, bounded receive window, `0x6a` stop | Raw measurement/error notifications and confirmation that stop was sent. |
| Live SpO2 | Command `0x69` start, bounded receive window, `0x6a` stop | Raw measurement/error notifications and confirmation that stop was sent. |

Only read requests and the start/stop pair for the two approved live sensors are
sent automatically. No time set, sampling-setting writes, binding, reboot,
shutdown, LED, alarm, or app-push commands are part of the sequence. Live
measurement stop is attempted on timeout or early exit while connected.

For requests requiring a calendar date, use an explicit `YYYY-MM-DD` probe date
from menuconfig when supplied, otherwise obtain a validated current date from
the configured Wi-Fi network and SNTP.
The selected date and offset are printed before transmission. If neither clock
source is available, mark those probes `SKIPPED_NO_DATE` while continuing
date-independent probes. Never send a guessed epoch or silently set ring time.
Steps use ring-relative day offsets and do not require an ESP32 clock.

The Big Data read request is seven bytes:
`bc <data-id> 01 00 ff 00 ff`. The final `ff` selects all retained days.
Responses are fragmented. Log each chunk without assuming a single GATT
notification is a complete record. Track the six-byte response header and
declared payload length with bounded counters; report complete, no data,
truncated, or timeout. Do not decode sleep stages or SpO2 samples yet.

## Monitor output and failure behavior

Use stable tags and concise single-line events, for example `PROBE TX hr_live
ch=command hex=...`, `PROBE RX ch=command len=16 hex=...`, and `PROBE RESULT
hr_live status=response packets=3 elapsed_ms=...`. Include checksum status for
16-byte command notifications and the response command byte; retain unmatched
notifications as `unsolicited` instead of discarding them. An explicit end
summary lists every planned probe and outcome. The documentation explains how
to configure, build, flash, and capture the monitor transcript.

Scanning, connection, characteristic discovery, subscribe, write, and per-query
waits have finite deadlines. A missing Big Data service skips only its two
probes. Missing command characteristics prevents command probes but still
produces a useful GATT inventory and session summary. Limit logging or tracking
only when necessary to protect memory; if any bytes are omitted, print exact
truncation counts so the transcript is not mistaken for complete history.

## Verification and limits

Host tests cover packet construction with sums above 255, the battery capture,
date/request encodings, and response classification using synthetic multi-packet
sequences. Build for ESP32-C3 with the repository's pinned ESP-IDF environment.
The hardware acceptance check is a monitor run on the user's ring: it must show
the battery exchange matching the Python capture and a trustworthy per-command
summary. Only that run can establish which other commands work on M7083 firmware.

This milestone emits diagnostic bytes and limited candidate labels. It does not
create `RingData` values, define the real JSON schema, or publish BLE readings.

## Protocol sources

- User's M7083 Python battery request and response (2026-10-01 conversation).
- [tahnok/colmi_r02_client](https://github.com/tahnok/colmi_r02_client): command
  framing and request implementations.
- [Puxtril/colmi-docs](https://github.com/Puxtril/colmi-docs): command inventory.
- [smittytone/RingCLI](https://github.com/smittytone/RingCLI): independent ring
  behavior and firmware-variant notes.
- [robinojw/openring protocol reference](https://github.com/robinojw/openring/blob/main/PROTOCOL.md): command confidence tiers.
- [robinojw/openring Big Data transport](https://github.com/robinojw/openring/blob/main/src/protocol/bigData.ts): on-device GATT UUID correction and framing.
- [audrebytes/colmi](https://github.com/audrebytes/colmi): additional QRing
  alternative reference; no code copied.
