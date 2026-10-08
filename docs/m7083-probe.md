# M7083 raw BLE probe

This mode runs one diagnostic session per boot against an exact advertised ring
name. It prints transmitted bytes, every received BLE notification, and a result
for each query. When Wi-Fi and MQTT are configured, supported decoded readings
are also published.

## Configure and flash

Open an ESP-IDF 5.5.2 terminal in this repository and select the ESP32-C3:

```sh
idf.py set-target esp32c3
idf.py menuconfig
```

Under **Smart Ring Gateway → Run mode**, select **Run one M7083 BLE diagnostic
session**. Set **Exact ring advertised name** to `M7083_7904` or the exact full
name shown by your ring. The optional address accepts
`31:35:45:31:79:04` for the ring in the Python capture; leave it empty if the
advertisement name alone is enough. The ring must be advertising and free to
connect; close QRing before running the probe.

For HR history, set an explicit UTC **probe date** in `YYYY-MM-DD` format. If
empty, the gateway tries SNTP when a Wi-Fi SSID is configured. If neither gives
a validated date, only today's and yesterday's HR history are skipped. Steps
use the ring's day offsets and still run. The probe never sets the ring clock.

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
