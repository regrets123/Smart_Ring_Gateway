#include <cctype>
#include <cstdio>
#include <ctime>

#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
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
    const auto date_status = gateway::ProbeDate::resolve(CONFIG_GATEWAY_PROBE_DATE,
                                                         synchronized_epoch, clock_valid, midnight);
    if (date_status == ESP_OK)
        ESP_LOGI(kProbeTag, "Probe date UTC midnight epoch=%u source=%s",
                 static_cast<unsigned>(midnight),
                 CONFIG_GATEWAY_PROBE_DATE[0] ? "menuconfig" : "SNTP");
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
