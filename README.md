# Smart Ring Gateway

ESP-IDF C++ gateway for an ESP32-C3 Super Mini and an M7083 ring.
It reads the ring over BLE, decodes supported readings, and publishes JSON over MQTT/TLS.

This repository contains the ESP32 gateway. The separate `Smart_Ring_Subs` project
subscribes to MQTT, stores readings in SQLite, and exposes them through an HTTPS API.
The complete chain has been tested with real hardware.

See [the gateway architecture](docs/architecture.md) and
[M7083 setup and capture guide](docs/m7083-probe.md).

The [M7083 BLE probe](docs/m7083-probe.md) runs a diagnostic session against the
QRing-compatible HAVIT ring. It prints raw command and Big Data responses plus
a per-query summary, and publishes supported decoded readings as JSON when MQTT
is configured. HRV history parsing is based on a real M7083 packet capture.

## Protocol references

- [patmorli/colmi-r09-smart-ring](https://github.com/patmorli/colmi-r09-smart-ring):
  a Python fork of an R02 client with R09 connection notes.
- [reuhenbhalod/DayBreak](https://github.com/reuhenbhalod/DayBreak):
  an independent Kotlin R09 implementation with a separate protocol module.

These are references for comparing protocol behavior. The current M7083 decoding
is based on ring captures; remaining packet semantics are described in the probe guide.

## Layout

| Path | Responsibility |
| --- | --- |
| `main/ble` | Discover and connect to the ring, write GATT commands, receive notifications |
| `main/protocol/ColmiProtocol.*` | Encode and decode normal command packets |
| `main/protocol/RingParsers.*` | Parse heart rate, HRV history, SpO2, and sleep |
| `main/models/RingData.h` | Decoded reading types |
| `main/serialization/RingJson.*` | Convert decoded readings to JSON |
| `main/network` | Wi-Fi and MQTT publishing with clock synchronization and CA verification |
| `main/certs` | Public MQTT CA certificate embedded in the firmware |
| `tests` | Host-only protocol and HRV history checks |

Configure Wi-Fi, the broker, credentials, and topic in menuconfig. The gateway
publishes version 1 records for supported readings; see
[payload examples](main/models/payloadExample.json). Historical HRV uses
`kind: "hrvHistory"` and `metric: "hrv_composite_ms"`.

## Build

`main/idf_component.yml` pins the managed `nlohmann/json` dependency to 3.12.0.
ESP-IDF downloads it and the pinned ESP-MQTT dependency during configuration.
The JSON serialization layer covers the supported decoded readings.

From an ESP-IDF terminal:

```sh
idf.py set-target esp32c3
idf.py build
```

The connected ESP32-C3 Super Mini reports 4 MB of flash.
