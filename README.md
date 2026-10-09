# Smart Ring Gateway

ESP-IDF C++ gateway for an ESP32-C3 Super Mini and a HAVIT M7083 smart ring. The ring supplies physical heart-rate, SpO2, sleep, and history data over BLE. The gateway decodes supported readings and publishes JSON over MQTT/TLS. The separate [Smart Ring Subscriber](https://github.com/regrets123/Smart_Ring_Subs) receives the messages on a Raspberry Pi, stores them in SQLite, and provides an authenticated HTTPS API. This repository contains the gateway firmware; both repositories are needed for the full data flow.

## Documentation

- [Architecture and data flow](docs/arkitektur.md)
- [API and message contract](docs/api.md)
- [Security analysis](docs/sakerhet.md)
- [Troubleshooting and fault cases](docs/felsokning.md)
- [M7083 setup, capture, and serial logs](docs/m7083-probe.md)

## Requirements

- ESP32-C3 Super Mini, HAVIT M7083 ring, USB connection, Wi-Fi, and access to an MQTT broker.
- ESP-IDF 5.5.2. `main/idf_component.yml` pins the managed `nlohmann/json` dependency to 3.12.0; ESP-IDF also downloads the pinned ESP-MQTT dependency during configuration.
- A broker CA certificate at `main/certs/mqtt_ca.pem`. This public certificate must verify the configured broker hostname.
- For the complete system: the separately deployed subscriber, SQLite database, and HTTPS API described in the [subscriber README](https://github.com/regrets123/Smart_Ring_Subs#readme).

## Configure, build, and start

In an ESP-IDF terminal, from this repository:

```sh
idf.py set-target esp32c3
idf.py menuconfig
```

In menuconfig, set **Component config -> ESP System Settings -> Main task stack size** to `8192` bytes. Under **Partition Table -> Partition Table**, select **Single factory app (large), no OTA**. Under **Smart Ring Gateway**, configure the ring's exact advertised name, Wi-Fi SSID and password, MQTT broker URI and credentials, MQTT topic, and device, gateway, and user IDs. The optional BLE address narrows the ring match. Leave the optional probe date empty for normal operation so SNTP supplies the current date. The default broker URI is `mqtts://mqtt.saxedesign.se:8883` and the default topic is `gateway/readings`; the subscriber must use the same topic.

Keep `sdkconfig` and private credentials out of Git. Place the broker's public CA certificate at `main/certs/mqtt_ca.pem`; this path is ignored by Git. See the [M7083 guide](docs/m7083-probe.md) for ring-specific configuration and capture instructions.

Build, flash, and watch serial output:

```sh
idf.py build
idf.py -p COM_PORT flash monitor
```

Replace `COM_PORT` with the board's serial port. Close the ring's phone app so the ESP32 can connect to the advertising ring. The gateway first connects to Wi-Fi and synchronizes its clock, then connects to MQTT and scans for the ring when a sync is due. A complete history sync is scheduled again after 23 hours; an incomplete one retries after 10 seconds. The last successful sync time is saved in NVS.

## Verify the data flow

The serial monitor shows the Wi-Fi IP address, clock and broker connection, BLE query results, a per-query `PROBE SUMMARY`, submitted records, and broker acknowledgements. `PROBE SUMMARY lost_notifications` is a simple monitoring measure for BLE reception; per-query packet counts and the subscriber's stored/updated/duplicate/rejected logs provide further visibility. The gateway acknowledgement confirms delivery to the broker, not a SQLite commit.

For an end-to-end check, find a published `recordId` in the gateway log, check that the subscriber logged storage, then retrieve the same ID through `GET /readings`. The [API guide](docs/api.md) gives the authenticated request and response format. No secrets need to be printed in the monitor or committed to the repository.

## Repository layout

| Path | Responsibility |
| --- | --- |
| `main/ble` | Discover and connect to the ring; exchange GATT messages |
| `main/protocol` | Encode commands and decode ring packets |
| `main/models` | Reading types and JSON payload examples |
| `main/serialization` | Convert decoded readings to JSON data objects |
| `main/network` | Wi-Fi, clock synchronization, and MQTT publishing |
| `docs` | Installation detail, architecture, API, security, and troubleshooting |
| `tests` | Host-side protocol and HRV history checks |

## Known limitations

The gateway has no durable offline queue and receives no database-level acknowledgement. A broker acknowledgement alone does not guarantee that SQLite stored the reading. Ring history dates are inferred from the gateway's probe date rather than independently verified ring timestamps. The BLE ring link is not encrypted by this firmware. The subscriber validates message structure but does not verify whether the claimed device, gateway, and user belong together. See the [architecture](docs/arkitektur.md) and [security analysis](docs/sakerhet.md) for the consequences.

## Protocol references

- [patmorli/colmi-r09-smart-ring](https://github.com/patmorli/colmi-r09-smart-ring): Python fork of an R02 client with R09 connection notes.
- [reuhenbhalod/DayBreak](https://github.com/reuhenbhalod/DayBreak): independent Kotlin R09 implementation.

The current M7083 decoding is based on ring captures; unresolved packet semantics are described in the [M7083 guide](docs/m7083-probe.md).
