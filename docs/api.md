# API and data contract

The ESP32-C3 gateway publishes JSON records to MQTT. The separate [Smart Ring Subscriber](https://github.com/regrets123/Smart_Ring_Subs) validates and stores them in SQLite, then exposes the stored records through an HTTPS API on the Raspberry Pi. The gateway itself does not run an HTTP server.

## MQTT message

The gateway publishes one record per message to `gateway/readings` by default, using MQTT over TLS on port `8883`, QoS 1, and no retained message. The broker URI and topic are configurable. The subscriber must subscribe to the same topic. A broker acknowledgement means the broker accepted the publish; it does not confirm database storage.

Example version 1 message (illustrative values):

```json
{
  "schemaVersion": 1,
  "recordId": "00000000000000000000000000000001",
  "deviceId": "ring-01",
  "gatewayId": "gateway-01",
  "userId": "user-01",
  "kind": "heartRate",
  "observedAt": "2026-10-07T12:00:00Z",
  "data": { "bpm": 72 }
}
```

`schemaVersion` is `1`. `recordId` identifies the record for duplicate handling; live records use 32 hexadecimal characters, while historical records use stable 16-character IDs for a device, reading kind, and measurement day. `deviceId`, `gatewayId`, and `userId` identify the claimed source and owner. `kind` selects the shape of `data`. `observedAt` is the gateway's UTC **publish time**, including for historical data; it is not necessarily the physical measurement time. The subscriber adds a separate `receivedAt` timestamp when storing the record.

Supported kinds are `heartRate`, `spo2`, `heartRateHistory`, `hrvHistory`, `spo2History`, and `sleep`. Their field definitions and validation rules are in the [subscriber message contract](https://github.com/regrets123/Smart_Ring_Subs/blob/main/docs/message-contract.md). More complete gateway examples are in [payloadExample.json](../main/models/payloadExample.json). The subscriber rejects malformed or oversized JSON, unsupported kinds, and invalid values; it treats identical record retries as duplicates.

## HTTPS read operation

| Item | Value |
| --- | --- |
| Method and path | `GET /readings` |
| Deployment URL | `https://mqtt.saxedesign.se:8080/readings` |
| Authentication | `Authorization: Bearer <token>` |
| `limit` | Optional integer from 1 to 500; default 100 |
| `offset` | Optional nonnegative integer; default 0 |
| Response | JSON object with `readings`, `limit`, and `offset` |

Each `readings` item has `receivedAt` and `message`; `message` is the stored MQTT JSON record. Results are ordered by receipt time. For example, with illustrative values:

```json
{
  "readings": [
    {
      "receivedAt": "2026-10-07T12:00:01Z",
      "message": {
        "schemaVersion": 1,
        "recordId": "00000000000000000000000000000001",
        "deviceId": "ring-01",
        "gatewayId": "gateway-01",
        "userId": "user-01",
        "kind": "heartRate",
        "observedAt": "2026-10-07T12:00:00Z",
        "data": { "bpm": 72 }
      }
    }
  ],
  "limit": 100,
  "offset": 0
}
```

The subscriber repository includes `read_readings.py`, which reads the token from an ignored local file and verifies the API certificate. From that repository, with the public API certificate in `certs/api-cert.pem` and the token in `certs/token.txt`, the request is:

```powershell
python .\read_readings.py https://mqtt.saxedesign.se:8080/readings --ca-cert .\certs\api-cert.pem
```

The API certificate must match the hostname in the URL. Do not place a real token in a command example, log, or Git commit.

## HTTP errors

| Status | Condition | JSON body |
| --- | --- | --- |
| `400` | Invalid, repeated, or unknown query parameter | `{"error":"limit must be 1-500 and offset must be nonnegative integers"}` |
| `401` | Missing or incorrect bearer token | `{"error":"unauthorized"}` |
| `404` | Unknown path after authentication | `{"error":"not found"}` |
| `500` | Database cannot be read | `{"error":"database unavailable"}` |

TLS certificate or network failures happen before an HTTP response is returned. The client reports them as request failures. The API implementation and run instructions are in the [subscriber repository](https://github.com/regrets123/Smart_Ring_Subs#readme).
