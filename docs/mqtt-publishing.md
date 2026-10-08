# Publish to the existing MQTT infrastructure

This page describes the initial mock publishing test. The current M7083 probe
also publishes decoded readings, including `hrvHistory`; see
[the probe guide](m7083-probe.md) and [payload examples](../main/models/payloadExample.json).
Use the existing Mosquitto server and webhook; this repository contains no
broker, webhook, or storage backend.

## Topic and payload

| Setting | Current value |
| --- | --- |
| Broker URI | `mqtts://mqtt.saxedesign.se:8883` |
| Publish topic | `gateway/mock/readings` (configurable) |
| QoS | `1` |
| Retain | `false` |
| Publish interval | 10 seconds by default (configurable) |
| MQTT client ID | `gateway-01` by default; uses the configured `gatewayId` |
| Payload | UTF-8 JSON |

```json
{
  "schemaVersion": 1,
  "recordId": "example-stable-record-id-001",
  "deviceId": "ring-01",
  "gatewayId": "gateway-01",
  "userId": "user-01",
  "kind": "heartRate",
  "observedAt": "2026-10-01T12:00:00Z",
  "data": {"bpm": 72}
}
```

IDs accept 1-64 ASCII letters, digits, hyphens, or underscores. Device, gateway,
and user IDs and the mock `bpm` are configurable through menuconfig. The mock
reuses the example `recordId` and `observedAt` on every publish; they are fixed
sample values, not unique event metadata or a live timestamp. The real ring
contract will follow BLE discovery.

## Existing broker and webhook

Allow the gateway's MQTT account to publish to `gateway/mock/readings`, or the
replacement topic you configure. Give each gateway a distinct MQTT client ID.
Configure the existing webhook's MQTT subscription/filter for the same topic;
parse the payload as JSON and check the fields above. The ESP32 publishes
MQTT only; it does not call the webhook over HTTP.

Only this one topic is implemented. Status, command, and database-acknowledgement
topics are not part of the current publishing test. QoS 1 may redeliver messages;
the mock's example `recordId` is reused and cannot distinguish publish cycles.

## Configure and flash the ESP32

From an ESP-IDF terminal in the repository root, use a separate configuration to
preserve the existing `sdkconfig`:

```sh
idf.py -B build-mock -D SDKCONFIG=sdkconfig.mock -D IDF_TARGET=esp32c3 menuconfig
```

Under **Gateway MQTT publishing**, set:

- Wi-Fi SSID and password.
- Broker URI: `mqtts://mqtt.saxedesign.se:8883` (also set this explicitly if an
  older local configuration still has an empty URI).
- MQTT username/password as required by your server.
- Publish topic: default `gateway/mock/readings`, or your existing topic.
- Device, gateway, and user IDs; mock heart rate and publish interval.

TLS uses the public CA certificate embedded from `main/certs/mqtt_ca.pem`, copied
from the Smart Climate System project and verified against this broker. After
Wi-Fi connects, the gateway
synchronizes its clock with `pool.ntp.org` before starting TLS. A synchronization
attempt waits up to 30 seconds; the publishing loop retries on failure. The
network must allow NTP traffic (UDP port 123). The certificate trusts the private
`saxedesign MQTT CA`; server certificate and hostname verification remain enabled.
The public CA certificate is ignored by Git. Place it at `main/certs/mqtt_ca.pem`
before building a fresh checkout so the firmware can embed it.
The CA private key stays on the server. If the broker moves to another CA, replace
this certificate and rebuild the firmware.

GitHub repository secrets (`MQTT_USER`, `MQTT_PASS`, `WIFI_PASSWORD`) are available
to GitHub Actions workflows. They do not populate a local ESP-IDF build. For a
local build, enter their values in menuconfig, along with the Wi-Fi SSID. The
MQTT username is the broker login; `userId` identifies the wearer in the JSON.
Keep credentials local; `sdkconfig.mock` and build output are ignored by Git.

```sh
idf.py -B build-mock -D SDKCONFIG=sdkconfig.mock -D IDF_TARGET=esp32c3 build
idf.py -B build-mock -D SDKCONFIG=sdkconfig.mock -D IDF_TARGET=esp32c3 -p YOUR_SERIAL_PORT flash monitor
```

## Check publishing and stop there

In the ESP32 monitor, expect serialized JSON, `Clock synchronized`, `Broker connected`, and
`Broker acknowledged message ...`. Confirm the same JSON arrives at the existing
webhook. That completes the current test; it does not imply SQLite storage.

An optional MQTT subscriber on your existing infrastructure can inspect messages:

```sh
mosquitto_sub -h BROKER_HOST -p BROKER_PORT -t gateway/mock/readings -q 1 -v
```

Use the broker's authentication/TLS options and your configured topic. On the
ESP32, `127.0.0.1` refers to the ESP32 itself, not the broker.

## Gateway serializer check

A host-only test compiles the actual `RingJson.cpp` without ESP-IDF and checks
mock serialization, invalid identity handling, and real-reading
stubs. It starts no broker, backend, or database:

```sh
cmake -S tests -B build-tests
cmake --build build-tests
ctest --test-dir build-tests --output-on-failure
```

Use a Linux/WSL C++ build environment for this host test. Hardware MQTT delivery
is verified against the existing infrastructure after configuring and flashing.
