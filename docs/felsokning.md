# Troubleshooting and fault records

This guide uses the ESP32 serial monitor, Raspberry Pi subscriber logs, and the HTTPS client output to follow one reading from the ring to the API. The assignment also requires two **deliberately introduced, analyzed, fixed, and verified** faults. The records below document an incorrect API token and an MQTT topic mismatch.

## Follow a reading through the system

1. In the ESP32 serial monitor, look for a Wi-Fi IP address, `Clock synchronized`, `Broker connected`, BLE `PROBE RESULT` lines, and `PROBE SUMMARY`. `lost_notifications` and packet counts show whether BLE reception completed.
2. Find `MQTT submitted kind=... recordId=...` followed by `Broker acknowledged message ...`. An acknowledgement confirms broker delivery only.
3. In the subscriber log, look for `Subscribed to gateway/readings` and `Message stored`, `Message updated`, `Message duplicate`, or `Rejected message`. A database error is logged separately and leaves the MQTT message unacknowledged.
4. Call `GET /readings` as described in [api.md](api.md), then look for the same `recordId` in the returned `message`. This verifies storage and API access together.

| Symptom | Check first |
| --- | --- |
| No Wi-Fi IP address | SSID/password in local menuconfig, Wi-Fi availability, serial `Wi-Fi disconnected` reason code. |
| Clock unavailable | Wi-Fi and access to the SNTP server; TLS startup waits for a usable clock. |
| MQTT unavailable or publish timeout | Broker URI, hostname, port `8883`, CA certificate, credentials, topic, and broker logs. A publish waits up to 10 seconds for acknowledgement. |
| Ring not found or incomplete probe | Ring advertisement, exact name/address filter, phone app connection, BLE packet/checksum logs, and `lost_notifications`. An incomplete history sync retries after 10 seconds. |
| Broker acknowledged but no API record | Subscriber subscription and storage logs, topic match, payload validation, and database access. Broker acknowledgement is not a database commit. |
| HTTPS client fails | API process, port `8080`, certificate hostname and trust, token file, then HTTP status if a response arrives. |

## Fault 1: Incorrect API token file

**Status:** The incorrect-token response and successful recovery with the intended token have been observed.

| Required observation | Record |
| --- | --- |
| Introduced fault and cause | The client was run with `--token-file .\certs\wrong-token.txt`, supplying an incorrect bearer token. |
| Observed symptom | The client printed `HTTP 401: {"error":"unauthorized"}`. |
| How it was identified | The request using `wrong-token.txt` returned HTTP `401`. Repeating the request without that override returned readings, isolating the problem to the selected token file. |
| Tools and logs | PowerShell output from `read_readings.py`. The token value itself was not printed. |
| Action taken | The client was run again without the `--token-file` override, so it used its default `certs/token.txt`. |
| Verification | The request using the intended token succeeded and `read_readings.py` printed a large JSON response of stored readings. The full response is omitted because it is roughly 1,000 lines long. |

## Fault 2: Gateway and subscriber topic mismatch

**Status:** Encountered, corrected, and verified during development.

| Required observation | Record |
| --- | --- |
| Introduced fault | The gateway published to `gateway-01/readings`, while the subscriber listened on `gateway-01/mock/readings`. |
| Observed symptom | Messages from the gateway did not arrive at the original subscriber. |
| How it was identified | A second subscriber using the gateway's topic received the messages. This showed that the broker was delivering them and led to the discovery that the original Python process had not been restarted after its topic setting changed. |
| Tools and logs | The configured topic values and a second MQTT subscriber that received the gateway's publications. |
| Root cause | The topic names differed. After the setting was corrected, the original subscriber service was not properly restarted, so the running process continued using its old topic. |
| Action taken | The topic setting was corrected and the original subscriber service was restarted. A second subscriber on the gateway's topic helped identify why the first change appeared ineffective. |
| Verification | The second subscriber received messages on `gateway-01/readings`. After the original service restarted, the next gateway publication reached the original subscriber. |

Both fault records now include observed symptoms, causes, corrective actions, and recovery evidence.
