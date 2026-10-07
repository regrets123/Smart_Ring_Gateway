#include <cctype>
#include <cstdio>
#include <cstring>
#include <ctime>

#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "sdkconfig.h"

#include "models/MockData.h"
#include "ble/RingBleClient.h"
#include "network/MqttPublisher.h"
#include "network/WifiManager.h"
#include "probe/ProbeDate.h"
#include "probe/RingProbe.h"
#include "serialization/RingJson.h"

#if CONFIG_GATEWAY_BLE_PROBE_ENABLED
namespace {
constexpr const char* kProbeTag = "M7083_PROBE";

bool parse_utc_timestamp(const char* value, uint32_t& epoch) {
    if (!value || std::strlen(value) != 20 || value[4] != '-' || value[7] != '-' ||
        value[10] != 'T' || value[13] != ':' || value[16] != ':' || value[19] != 'Z')
        return false;
    for (size_t i = 0; i < 20; ++i) {
        if (i == 4 || i == 7 || i == 10 || i == 13 || i == 16 || i == 19) continue;
        if (!std::isdigit(static_cast<unsigned char>(value[i]))) return false;
    }
    char date[11]{};
    std::memcpy(date, value, 10);
    uint32_t midnight = 0;
    if (gateway::ProbeDate::resolve(date, 0, false, midnight) != ESP_OK) return false;
    const auto two_digits = [value](size_t index) {
        return (value[index] - '0') * 10 + value[index + 1] - '0';
    };
    const int hour = two_digits(11);
    const int minute = two_digits(14);
    const int second = two_digits(17);
    if (hour > 23 || minute > 59 || second > 59) return false;
    epoch = midnight + static_cast<uint32_t>(hour * 3600 + minute * 60 + second);
    return true;
}

const char* channel_name(gateway::RingChannel channel) {
    return channel == gateway::RingChannel::command ? "command" : "big_data";
}

const char* checksum_name(gateway::PacketStatus status) {
    switch (status) {
    case gateway::PacketStatus::valid: return "valid";
    case gateway::PacketStatus::wrong_length: return "wrong_length";
    case gateway::PacketStatus::bad_checksum: return "bad_checksum";
    }
    return "unknown";
}

void hex_string(const uint8_t* bytes, size_t length, char* output, size_t capacity) {
    if (capacity == 0) return;
    size_t cursor = 0;
    for (size_t i = 0; i < length && cursor + 4 < capacity; ++i) {
        const int written = std::snprintf(output + cursor, capacity - cursor,
                                          i ? " %02x" : "%02x", bytes[i]);
        if (written < 0) break;
        cursor += static_cast<size_t>(written);
    }
    output[cursor] = '\0';
}

class MonitorObserver final : public gateway::ProbeObserver {
public:
    void tx(const char* label, gateway::RingChannel channel,
            const uint8_t* bytes, size_t length) override {
        hex_string(bytes, length, hex_, sizeof(hex_));
        ESP_LOGI(kProbeTag, "PROBE TX %s ch=%s len=%u hex=%s", label,
                 channel_name(channel), static_cast<unsigned>(length), hex_);
    }

    void rx(const char* label, const gateway::RingNotification& note,
            bool matched, gateway::PacketStatus checksum) override {
        hex_string(note.bytes, note.length, hex_, sizeof(hex_));
        ESP_LOGI(kProbeTag, "PROBE RX %s ch=%s len=%u matched=%u cmd=%02x checksum=%s hex=%s",
                 label, channel_name(note.channel), static_cast<unsigned>(note.length),
                 matched ? 1u : 0u, note.length ? note.bytes[0] : 0u,
                 note.channel == gateway::RingChannel::command ? checksum_name(checksum) : "n/a",
                 hex_);
        if (matched && note.channel == gateway::RingChannel::command &&
            note.length == 16 && note.bytes[0] == 0x03 &&
            checksum == gateway::PacketStatus::valid)
            ESP_LOGI(kProbeTag, "BATTERY candidate_percent=%u candidate_charge_flag=%u",
                     note.bytes[1], note.bytes[2]);
    }

    void device_info(uint16_t uuid, esp_err_t status,
                     const uint8_t* bytes, size_t length) override {
        hex_string(bytes, length, hex_, sizeof(hex_));
        char printable[257]{};
        const size_t count = length < sizeof(printable) - 1 ? length : sizeof(printable) - 1;
        for (size_t i = 0; i < count; ++i)
            printable[i] = std::isprint(static_cast<unsigned char>(bytes[i])) ? bytes[i] : '.';
        ESP_LOGI(kProbeTag, "DEVICE_INFO uuid=%04x status=%s len=%u hex=%s text=%s",
                 uuid, esp_err_to_name(status), static_cast<unsigned>(length), hex_, printable);
    }

    void result(const gateway::ProbeEntry& entry) override {
        ESP_LOGI(kProbeTag, "PROBE RESULT %s status=%s packets=%u bytes=%u elapsed_ms=%u",
                 entry.label, gateway::probe_result_name(entry.result),
                 static_cast<unsigned>(entry.packets), static_cast<unsigned>(entry.bytes),
                 static_cast<unsigned>(entry.elapsed_ms));
    }

    void finished(const gateway::ProbeSummary& summary) override {
        ESP_LOGI(kProbeTag, "PROBE SUMMARY BEGIN");
        for (const auto& entry : summary.entries)
            ESP_LOGI(kProbeTag, "PROBE SUMMARY %s=%s packets=%u bytes=%u",
                     entry.label, gateway::probe_result_name(entry.result),
                     static_cast<unsigned>(entry.packets), static_cast<unsigned>(entry.bytes));
        ESP_LOGI(kProbeTag, "PROBE SUMMARY lost_notifications=%u",
                 static_cast<unsigned>(summary.lost_notifications));
        ESP_LOGI(kProbeTag, "PROBE SUMMARY END");
    }

private:
    char hex_[517 * 3 + 1]{};
};

void run_probe() {
    ESP_ERROR_CHECK(nvs_flash_init());
    uint32_t configured_time = 0;
    const bool time_requested = CONFIG_GATEWAY_PROBE_SET_TIME_UTC[0] != '\0';
    const bool configured_time_valid = time_requested &&
        parse_utc_timestamp(CONFIG_GATEWAY_PROBE_SET_TIME_UTC, configured_time);
    if (time_requested && !configured_time_valid)
        ESP_LOGE(kProbeTag, "Invalid one-shot UTC timestamp; expected YYYY-MM-DDTHH:MM:SSZ");
    uint32_t midnight = 0;
    bool clock_valid = false;
    uint32_t synchronized_epoch = 0;
    if (CONFIG_GATEWAY_PROBE_DATE[0] == '\0' && CONFIG_GATEWAY_WIFI_SSID[0] != '\0') {
        gateway::WifiManager wifi;
        if (wifi.connect() == ESP_OK) {
            esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
            if (esp_netif_sntp_init(&config) == ESP_OK) {
                clock_valid = esp_netif_sntp_sync_wait(pdMS_TO_TICKS(15000)) == ESP_OK;
                if (clock_valid) synchronized_epoch = static_cast<uint32_t>(std::time(nullptr));
                esp_netif_sntp_deinit();
            }
            wifi.disconnect();
        }
    }
    const auto date_status = gateway::ProbeDate::resolve(
        CONFIG_GATEWAY_PROBE_DATE,
        clock_valid ? synchronized_epoch : configured_time,
        clock_valid || configured_time_valid, midnight);
    if (date_status == ESP_OK)
        ESP_LOGI(kProbeTag, "Probe date UTC midnight epoch=%u source=%s",
                 static_cast<unsigned>(midnight),
                 CONFIG_GATEWAY_PROBE_DATE[0] ? "menuconfig" :
                 clock_valid ? "SNTP" : "configured UTC timestamp");
    else
        ESP_LOGW(kProbeTag, "Probe date unavailable (%s); HR history will be skipped",
                 esp_err_to_name(date_status));

    static gateway::RingBleClient transport;
    ESP_LOGI(kProbeTag, "Scanning exact_name=%s address_filter=%s", CONFIG_GATEWAY_PROBE_NAME,
             CONFIG_GATEWAY_PROBE_ADDRESS[0] ? CONFIG_GATEWAY_PROBE_ADDRESS : "none");
    const auto opened = transport.open(CONFIG_GATEWAY_PROBE_NAME,
                                       CONFIG_GATEWAY_PROBE_ADDRESS, 60000);
    if (opened != ESP_OK) {
        ESP_LOGE(kProbeTag, "BLE open failed: %s", esp_err_to_name(opened));
        return;
    }
    const auto subscribed = transport.subscribe();
    ESP_LOGI(kProbeTag, "GATT subscribed=%s command=%u big_data=%u",
             esp_err_to_name(subscribed), transport.has_channel(gateway::RingChannel::command),
             transport.has_channel(gateway::RingChannel::big_data));
    static MonitorObserver observer;
    gateway::RingProbe runner;
    if (configured_time_valid) {
        nvs_handle_t handle;
        const auto opened_nvs = nvs_open("m7083_probe", NVS_READWRITE, &handle);
        if (opened_nvs != ESP_OK) {
            ESP_LOGE(kProbeTag, "One-shot time marker unavailable: %s",
                     esp_err_to_name(opened_nvs));
        } else {
            uint32_t previous = 0;
            const auto read_marker = nvs_get_u32(handle, "time_tag", &previous);
            if (read_marker == ESP_OK && previous == configured_time) {
                uint8_t previous_result = 0;
                if (nvs_get_u8(handle, "time_result", &previous_result) == ESP_OK &&
                    previous_result <= static_cast<uint8_t>(gateway::ProbeResult::malformed))
                    ESP_LOGI(kProbeTag, "One-shot ring time already attempted: %s; skipping",
                             gateway::probe_result_name(
                                 static_cast<gateway::ProbeResult>(previous_result)));
                else
                    ESP_LOGI(kProbeTag, "One-shot ring time already attempted; result unavailable; skipping");
            } else if (read_marker != ESP_OK && read_marker != ESP_ERR_NVS_NOT_FOUND) {
                ESP_LOGE(kProbeTag, "Cannot read one-shot marker: %s",
                         esp_err_to_name(read_marker));
            } else {
                const auto write_marker = nvs_set_u32(handle, "time_tag", configured_time);
                const auto saved = write_marker == ESP_OK ? nvs_commit(handle) : write_marker;
                if (saved != ESP_OK) {
                    ESP_LOGE(kProbeTag, "Cannot save one-shot marker: %s",
                             esp_err_to_name(saved));
                } else {
                    const uint32_t send_time = clock_valid
                        ? static_cast<uint32_t>(std::time(nullptr)) : configured_time;
                    ESP_LOGI(kProbeTag, "One-shot ring time UTC epoch=%u source=%s",
                             static_cast<unsigned>(send_time),
                             clock_valid ? "SNTP" : "configured timestamp");
                    const auto result = runner.set_time(transport, send_time, observer);
                    const auto saved_result = nvs_set_u8(
                        handle, "time_result", static_cast<uint8_t>(result.result));
                    if (saved_result == ESP_OK) nvs_commit(handle);
                }
            }
            nvs_close(handle);
        }
    }
    runner.run(transport, midnight, date_status == ESP_OK, observer);
    transport.disconnect();
    ESP_LOGI(kProbeTag, "Probe session finished; reset to run again");
}
}  // namespace
#endif

extern "C" void app_main(void) {
#if CONFIG_GATEWAY_MOCK_ENABLED
    constexpr const char* tag = "mock_gateway";
    if (CONFIG_GATEWAY_WIFI_SSID[0] == '\0' || CONFIG_GATEWAY_MQTT_URI[0] == '\0' ||
        CONFIG_GATEWAY_MQTT_TOPIC[0] == '\0') {
        ESP_LOGW(tag, "Set Wi-Fi, broker and topic under menuconfig -> Gateway MQTT publishing");
        return;
    }
    // Do not erase existing NVS automatically if initialization fails.
    ESP_ERROR_CHECK(nvs_flash_init());
    static gateway::WifiManager wifi;
    static gateway::MqttPublisher mqtt;
    gateway::RingJson serializer;
    const gateway::MockReading mock{"example-stable-record-id-001",
                                   CONFIG_GATEWAY_DEVICE_ID,
                                   CONFIG_GATEWAY_GATEWAY_ID,
                                   CONFIG_GATEWAY_USER_ID,
                                   "2026-10-01T12:00:00Z",
                                   CONFIG_GATEWAY_MOCK_BPM};
    std::string payload;
    ESP_ERROR_CHECK(serializer.serialize(mock, payload));
    ESP_LOGI(tag, "Mock source -> JSON: %s", payload.c_str());
    ESP_LOGI(tag, "Publish topic: %s", CONFIG_GATEWAY_MQTT_TOPIC);
    for (;;) {
        esp_err_t result = wifi.connect();
        if (result == ESP_OK) result = mqtt.connect();
        if (result == ESP_OK) result = mqtt.publish(CONFIG_GATEWAY_MQTT_TOPIC, payload.c_str());
        if (result != ESP_OK) ESP_LOGW(tag, "Retry next interval: %s", esp_err_to_name(result));
        vTaskDelay(pdMS_TO_TICKS(CONFIG_GATEWAY_MOCK_INTERVAL_SECONDS * 1000));
    }
#elif CONFIG_GATEWAY_BLE_PROBE_ENABLED
    run_probe();
#endif
}
