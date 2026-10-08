# M7083 raw BLE probe

The gateway keeps running after boot. When a sync is due, it scans for the exact
advertised ring name, performs a session when the ring comes into range, and then
waits 23 hours after a complete history session. An unsuccessful scan or history
session retries after 10 seconds. The successful sync time is stored in NVS, so a
reboot does not force another session. Wi-Fi, MQTT, and a valid clock are required
before scanning because there is no durable offline upload queue.

Each session prints transmitted bytes, received BLE notifications, and query
results. Supported decoded readings are also published. Live HR and SpO₂ remain
part of a session as extra samples from the time the ring was nearby.

## Configure and flash

Open an ESP-IDF 5.5.2 terminal in this repository and select the ESP32-C3:

```sh
idf.py set-target esp32c3
idf.py menuconfig
```

Under **Smart Ring Gateway**, set **Exact ring advertised name** to `M7083_7904` or the exact full
name shown by your ring. The optional address accepts
`31:35:45:31:79:04` for the ring in the Python capture; leave it empty if the
advertisement name alone is enough. The ring must be advertising and free to
connect; close QRing before running the probe.

Leave the UTC **probe date** empty for continuous operation. The gateway uses
SNTP to date history; a configured date would remain fixed across sessions.
The probe never sets the ring clock unless the one-shot UTC time option is set.

Historical `recordId` values are stable for a device, reading kind, and
measurement day. HRV, sleep, and SpO₂ history responses are split into one message per day so
overlapping ring history can be upserted by `recordId`. Live readings receive
unique IDs because each session measures them again. `observedAt` is the gateway
publish time; ring history dates remain in the payload or are inferred from the
probe date and `days_ago`. MQTT QoS 1 acknowledgement confirms broker delivery,
not storage by a downstream database.

An SpO₂ history payload contains consecutive 49-byte day records: one `days_ago`
byte and 24 hourly `(max, min)` byte pairs. Zero/zero pairs are omitted; each
published sample has a `slot` from 0 to 23. The M7083 capture contained three
day records in one 147-byte payload. This layout agrees with
[openring's on-device decoder](https://github.com/robinojw/openring/blob/main/src/protocol/decoders/spo2.ts).

Sleep stage codes are 2=light, 3=deep, 4=REM, and 5=awake. The captured
`days_ago=2` night starts at minute 1368 (22:48) and ends at minute 422
(07:02 the next day). Its 494-minute interval matches the sum of all stage
durations when REM is included. Since a `days_ago=0` record already exists at
midday but starts in the evening, the offset appears to name the wake date. The
absolute dates inferred from `days_ago` assume the ring and gateway agree on
the calendar day. MQTT sleep JSON uses `start_time` and `end_time` as `HH:MM`
clock strings; the raw signed minute offsets stay inside the firmware. These
clock strings have no timezone or date of their own.

Build, flash, and watch the monitor:

```sh
idf.py build
idf.py -p COM_PORT flash monitor
```

Replace `COM_PORT` with the ESP32's serial port. To keep the existing mock
`sdkconfig` untouched, you can use the included probe defaults in a separate
build directory instead of menuconfig:

```sh
idf.py -B build-probe -D SDKCONFIG=sdkconfig.probe -D SDKCONFIG_DEFAULTS=sdkconfig.probe.defaults build
idf.py -B build-probe -D SDKCONFIG=sdkconfig.probe -p COM_PORT flash monitor
```

## Reading the output

The battery exchange should begin with these bytes from the working Python
script:

```text
PROBE TX battery ch=command len=16 hex=03 00 00 00 00 00 00 00 00 00 00 00 00 00 00 03
PROBE RX battery ch=command len=16 matched=1 cmd=03 checksum=valid hex=03 38 00 00 00 00 00 00 00 00 00 00 00 00 00 3b
BATTERY candidate_percent=56 candidate_charge_flag=0
```

`56` is only a candidate battery percentage until the ESP32 capture confirms
the same firmware behavior. Each `PROBE RESULT` shows `RESPONSE`, `NO_DATA`,
`TIMEOUT`, `UNSUPPORTED_CHANNEL`, `TRANSPORT_ERROR`, `SKIPPED_NO_DATE`,
`SKIPPED_DISCONNECTED`, or `MALFORMED`. `matched=0` means a packet did not
match the current request; its raw bytes are still printed. The final `PROBE
SUMMARY` lists all 12 probes and `lost_notifications`. A nonzero lost count
means the transcript omitted that many notification chunks.

The sequence is battery, five standard Device Information reads, HR logging
settings, steps for offsets 0 and 1, HR history for today and yesterday, paged
HRV, sleep and SpO₂ Big Data history, then live HR and SpO₂. Each live sensor
is stopped after at most 30 seconds. The program does not send binding,
settings, reboot, or power commands. It sets ring time only when the optional
one-shot UTC timestamp is configured.

For HRV, look for `HRV REQUEST page=0`, followed by `HRV HEADER page=0
reported_pages=...`. The probe then requests each reported page and prints
`HRV PAGE page=... payload_hex=...` for valid replies. A final `39 ff` packet
is printed as `HRV END marker=ff` after pages have arrived, or `HRV NO_DATA
marker=ff` if it arrives without any pages. The accompanying `PROBE RX hrv` line
contains the complete 16-byte packet, including its checksum, for checking
decoded samples against the M7083 capture.

The decoded HRV history is published as `kind: "hrvHistory"`. Each sample has
`days_ago`, a zero-based `slot`, and `value_ms`; the data object also has
`metric: "hrv_composite_ms"` and `interval_minutes` (30 in the captured session).
Zero and `0xff` slot values are omitted. When a probe date is available, the
data includes `probe_midnight_utc`, the gateway's date anchor. The ring packets
do not contain absolute timestamps or a timezone, so this anchor is not an
independently verified ring timestamp. The value is a firmware-computed HRV
composite, not a validated RMSSD measurement. The M7083 capture indicates
four day offsets, 0 through 3; another firmware may retain a different span.

Protocol layout references: [Colmi BLE API HRV](https://colmi.puxtril.com/commands/)
and [openring's experimental HRV decoder](https://github.com/robinojw/openring/blob/main/src/protocol/decoders/hrv.ts).

To save evidence on Windows PowerShell, run the monitor with a transcript:

```powershell
Start-Transcript -Path .\m7083-monitor.txt
idf.py -p COM_PORT monitor
Stop-Transcript
```

Capture from `Scanning exact_name=...` through `PROBE SUMMARY END`, including
all TX and RX lines. An ESP32 monitor run on the user's ring is required to
establish which queries this M7083 actually answers. The battery result from
Python alone does not validate the other commands or their payload meanings.

Protocol reference: [openring PROTOCOL.md](https://github.com/robinojw/openring/blob/main/PROTOCOL.md)
and [tahnok/colmi_r02_client](https://github.com/tahnok/colmi_r02_client).
