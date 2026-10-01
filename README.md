# Smart Ring Gateway

ESP-IDF C++ project skeleton for an ESP32-C3 Super Mini gateway and a COLMI R09 ring.
The intended path is BLE GATT client → COLMI packet handling → validated readings →
JSON → MQTT over TLS.

This repository is the ESP32 gateway only. It publishes to the existing Mosquitto
server; webhook routing is handled by that infrastructure. A future Raspberry Pi
MQTT subscriber and SQLite backend will
live in a separate repository.

See [the gateway architecture](docs/architecture.md) and
[MQTT publishing setup](docs/mqtt-publishing.md). Start with the one owned ring;
keep device identities and protocol handling extensible for future devices/users.

## Protocol references

- [patmorli/colmi-r09-smart-ring](https://github.com/patmorli/colmi-r09-smart-ring):
  a Python fork of an R02 client with R09 connection notes.
- [reuhenbhalod/DayBreak](https://github.com/reuhenbhalod/DayBreak):
  an independent Kotlin R09 implementation with a separate protocol module.

These are references for comparing behavior. No code from either project has been
copied into this skeleton. Ring firmware behavior and packet formats still need to
be checked against both sources and, eventually, real R09 captures.

## Layout

| Path | Planned responsibility |
| --- | --- |
| `main/ble` | Discover and connect to the ring, write GATT commands, receive notifications |
| `main/protocol/ColmiProtocol.*` | Encode and decode normal command packets |
| `main/protocol/RingParsers.*` | Parse heart rate, SpO2, sleep, and steps |
| `main/models/RingData.h` | Real reading types; fields are still to be defined |
| `main/models/MockData.h` | Temporary publishing-test reading and IDs |
| `main/serialization/RingJson.*` | Convert decoded readings to JSON; upload format is still to be defined |
| `main/network` | Wi-Fi and MQTT publishing with clock synchronization and CA verification |
| `main/certs` | Public MQTT CA certificate embedded in the firmware |
| `tests` | Host-only checks for the gateway JSON serializer |

The mock publisher sends a version 1 `heartRate` record with `data.bpm` to
`gateway/mock/readings` (QoS 1, not retained). Configure Wi-Fi, your
existing broker, credentials, and topic in menuconfig. Real BLE parsing and
serialization remain stubs returning `ESP_ERR_NOT_SUPPORTED`; the real upload
format is still undecided.

## Build

`main/idf_component.yml` pins the managed `nlohmann/json` dependency to 3.12.0.
ESP-IDF downloads it and the pinned ESP-MQTT dependency during configuration.
The JSON serialization layer currently implements only the mock payload.

From an ESP-IDF terminal:

```sh
idf.py set-target esp32c3
idf.py build
```

The connected ESP32-C3 Super Mini reports 4 MB of flash. Pin wiring, ring discovery
rules, credential handling, and broker settings need to be confirmed before deployment.
