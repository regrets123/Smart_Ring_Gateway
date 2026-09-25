# Smart Ring Gateway

ESP-IDF C++ project skeleton for an ESP32-C3 Super Mini gateway and a COLMI R09 ring.
The intended path is BLE GATT client → COLMI packet handling → validated readings →
JSON → MQTT over TLS. This repository currently contains interfaces and stubs only;
it does not scan for a ring, decode packets, connect to Wi-Fi, or publish data yet.

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
| `main/protocol/BigDataReassembler.*` | Collect fragmented history transfers |
| `main/protocol/RingParsers.*` | Parse heart rate, SpO2, sleep, and steps |
| `main/models` | Validated reading types; fields are still to be defined |
| `main/network` | Wi-Fi and MQTT publishing; TLS policy is still to be defined |

All unimplemented operations return `ESP_ERR_NOT_SUPPORTED`. `app_main` only prints
a startup message so this skeleton can be built without pretending that a sync works.

## Build

From an ESP-IDF terminal:

```sh
idf.py set-target esp32c3
idf.py build
```

The ESP32-C3 Super Mini board's flash size, pin wiring, ring discovery rules,
credential handling, and broker settings need to be confirmed before deployment.
