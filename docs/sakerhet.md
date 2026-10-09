# Security analysis

This system carries personal health readings from the M7083 ring through an ESP32-C3 gateway, an MQTT broker, a Raspberry Pi subscriber, SQLite, and an HTTPS API. The measures below describe the current implementation and deployment; they do not make the readings anonymous.

## Implemented measures

| Measure | Where and purpose |
| --- | --- |
| MQTT over TLS | The gateway uses `mqtts://` and an embedded public CA certificate to verify the broker. It waits for clock synchronization before TLS. The subscriber also uses TLS and a CA certificate for its broker connection. |
| Broker credentials | The gateway and subscriber can authenticate with separate MQTT usernames and passwords. Broker account permissions should restrict each account to its required topic. |
| HTTPS and bearer token | The API uses HTTPS for network-facing access and requires exactly one `Authorization: Bearer <token>` header. The server requires a token of at least 32 characters and compares it without a timing-dependent string comparison. |
| Input validation | The subscriber rejects malformed, oversized, or unsupported JSON and checks field types and ranges before database storage. The gateway checks ring packet lengths and checksums before decoding. |
| Restricted exposure | In this deployment, the Pi exposes broker port `8883` and API port `8080` to reach the services. The API defaults to loopback and requires a certificate and key when bound to a network-facing address. |

## Risks and remaining limitations

1. **BLE interception or spoofing.** The ring's GATT link is not encrypted by this firmware. Someone within BLE range may be able to observe or imitate traffic. The gateway selects the ring by advertised name and optionally its BLE address, but those identifiers are not strong authentication. The ring firmware is outside this project's control, so this risk remains.
2. **Loss of readings during an outage.** MQTT QoS 1 and broker acknowledgements reduce transport uncertainty but do not prove that the subscriber committed a reading to SQLite. The gateway has no durable offline queue or database-level acknowledgement. It retries incomplete history syncs, and some history can be fetched again from the ring, but live readings may be lost. A durable queue or end-to-end receipt would improve this.
3. **Unauthorized access to health data.** HTTPS and a bearer token protect the API in transit and gate access. If the token is copied or the Pi is compromised, the API can expose stored readings. Access to port `8080`, token storage, and token rotation remain important. The API has no per-user authorization.
4. **False identity claims in valid messages.** The subscriber validates message structure but does not verify that a `deviceId`, `gatewayId`, and `userId` belong together. A publisher with broker access to the topic could submit plausible records under another identity. Restricting broker publish permissions helps, but the relationship is not yet enforced by the subscriber.
5. **Certificate and clock dependencies.** TLS can fail if a certificate expires, the configured hostname does not match, or the gateway clock cannot synchronize. The gateway logs clock and broker errors and waits before scanning. Certificates and the clock source still need operational maintenance.

## Sensitive configuration

The gateway's Wi-Fi SSID and password, MQTT credentials, and IDs are entered through ESP-IDF menuconfig. The local `sdkconfig` is ignored by Git; do not copy real credentials into tracked defaults or documentation. `main/certs/mqtt_ca.pem` is ignored in this repository. It contains a public CA certificate, not a private key.

The subscriber keeps its MQTT credentials in environment variables, its API token in an ignored local token file, and the API certificate and private key in its ignored `certs/` directory. SQLite files are also ignored. Only the public API certificate and the token are needed by the read client; the API private key stays on the Pi. Keep real token values out of logs, screenshots, commits, and shared command histories. The [subscriber README](https://github.com/regrets123/Smart_Ring_Subs#readme) describes its configuration.

These controls meet the project's basic secure-communication requirement. The unencrypted BLE hop, missing per-user authorization, and lack of durable delivery remain explicit limitations.
