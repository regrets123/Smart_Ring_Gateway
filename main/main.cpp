#include <cctype>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "sdkconfig.h"

#include "ble/RingBleClient.h"
#include "network/WifiManager.h"
#include "network/MqttPublisher.h"
#include "probe/ProbeDate.h"
#include "probe/RingProbe.h"
#include "protocol/RingParsers.h"
#include "serialization/RingJson.h"

namespace {
constexpr const char* kProbeTag = "M7083_PROBE";

bool parse_utc_timestamp(const char* value, uint32_t& epoch) {
    if (!value || std::strlen(value) != 20 || value[4] != '-' || value[7] != '-' ||
        value[10] != 'T' || value[13] != ':' || value[16] != ':' || value[19] != 'Z') {
        return false;
    }
    for (size_t i = 0; i < 20; ++i) {
        if (i == 4 || i == 7 || i == 10 || i == 13 || i == 16 || i == 19) {
            continue;
        }
        if (!std::isdigit(static_cast<unsigned char>(value[i]))) {
            return false;
        }
    }
    char date[11]{};
    std::memcpy(date, value, 10);
    uint32_t midnight = 0;
    if (gateway::ProbeDate::resolve(date, 0, false, midnight) != ESP_OK) {
        return false;
    }
    const auto two_digits = [value](size_t index) {
        return (value[index] - '0') * 10 + value[index + 1] - '0';
    };
    const int hour = two_digits(11);
    const int minute = two_digits(14);
    const int second = two_digits(17);
    if (hour > 23 || minute > 59 || second > 59) {
        return false;
    }
    epoch = midnight + static_cast<uint32_t>(hour * 3600 + minute * 60 + second);
    return true;
}

const char* channel_name(gateway::RingChannel channel) {
    return channel == gateway::RingChannel::command ? "command" : "big_data";
}

const char* checksum_name(gateway::PacketStatus status) {
    switch (status) {
    case gateway::PacketStatus::valid:
        return "valid";
    case gateway::PacketStatus::wrong_length:
        return "wrong_length";
    case gateway::PacketStatus::bad_checksum:
        return "bad_checksum";
    }
    return "unknown";
}

void hex_string(const uint8_t* bytes, size_t length, char* output, size_t capacity) {
    if (capacity == 0) {
        return;
    }
    size_t cursor = 0;
    for (size_t i = 0; i < length && cursor + 4 < capacity; ++i) {
        const int written =
            std::snprintf(output + cursor, capacity - cursor, i ? " %02x" : "%02x", bytes[i]);
        if (written < 0) {
            break;
        }
        cursor += static_cast<size_t>(written);
    }
    output[cursor] = '\0';
}

class MonitorObserver final : public gateway::ProbeObserver {
  public:
    void set_publisher(gateway::MqttPublisher* publisher) {
        publisher_ = publisher;
        hr_history_parser_.reset();
        hr_history_ready_ = false;
        live_hr_ready_ = false;
        live_spo2_ready_ = false;
        big_frame_.clear();
    }

    void tx(const char* label, gateway::RingChannel channel, const uint8_t* bytes,
            size_t length) override {
        hex_string(bytes, length, hex_, sizeof(hex_));
        ESP_LOGI(kProbeTag, "PROBE TX %s ch=%s len=%u hex=%s", label, channel_name(channel),
                 static_cast<unsigned>(length), hex_);
        if (std::strcmp(label, "hrv") == 0 && channel == gateway::RingChannel::command &&
            length == 16 && bytes[0] == 0x39) {
            ESP_LOGI(kProbeTag, "HRV REQUEST page=%u", static_cast<unsigned>(bytes[1]));
        }
    }

    void rx(const char* label, const gateway::RingNotification& note, bool matched,
            gateway::PacketStatus checksum) override {
        hex_string(note.bytes, note.length, hex_, sizeof(hex_));
        ESP_LOGI(kProbeTag, "PROBE RX %s ch=%s len=%u matched=%u cmd=%02x checksum=%s hex=%s",
                 label, channel_name(note.channel), static_cast<unsigned>(note.length),
                 matched ? 1u : 0u, note.length ? note.bytes[0] : 0u,
                 note.channel == gateway::RingChannel::command ? checksum_name(checksum) : "n/a",
                 hex_);
        if (std::strcmp(label, "hrv") == 0 && matched &&
            note.channel == gateway::RingChannel::command && note.length == 16 &&
            checksum == gateway::PacketStatus::valid) {
            if (note.bytes[1] == 0xff) {
                ESP_LOGI(kProbeTag, "HRV NO_DATA marker=ff");
            } else if (note.bytes[1] == 0) {
                ESP_LOGI(kProbeTag, "HRV HEADER page=0 reported_pages=%u",
                         static_cast<unsigned>(note.bytes[2]));
            } else {
                char payload_hex[14 * 3 + 1]{};
                hex_string(note.bytes + 2, 13, payload_hex, sizeof(payload_hex));
                ESP_LOGI(kProbeTag, "HRV PAGE page=%u payload_hex=%s",
                         static_cast<unsigned>(note.bytes[1]), payload_hex);
            }
        }
        if (matched && note.channel == gateway::RingChannel::command && note.length == 16 &&
            note.bytes[0] == 0x03 && checksum == gateway::PacketStatus::valid) {
            ESP_LOGI(kProbeTag, "BATTERY candidate_percent=%u candidate_charge_flag=%u",
                     note.bytes[1], note.bytes[2]);
        }

        if (matched && note.channel == gateway::RingChannel::command &&
            std::strcmp(label, "live_hr") == 0) {
            gateway::HeartRateParser parser;
            gateway::HeartRateReading reading;
            if (parser.parse(note.bytes, note.length, reading) == ESP_OK) {
                live_hr_ = reading;
                live_hr_ready_ = true;
            }
        } else if (matched && note.channel == gateway::RingChannel::command &&
                   std::strcmp(label, "live_spo2") == 0) {
            gateway::Spo2Parser parser;
            gateway::Spo2Reading reading{};
            if (parser.parse(note.bytes, note.length, reading) == ESP_OK) {
                live_spo2_ = reading;
                live_spo2_ready_ = true;
            }
        } else if (matched && note.channel == gateway::RingChannel::command &&
                   (std::strcmp(label, "hr_today") == 0 ||
                    std::strcmp(label, "hr_yesterday") == 0)) {
            bool complete = false;
            const auto status =
                hr_history_parser_.parse(note.bytes, note.length, hr_history_, complete);
            hr_history_ready_ = status == ESP_OK && complete;
        } else if (matched && note.channel == gateway::RingChannel::big_data &&
                   (std::strcmp(label, "sleep") == 0 ||
                    std::strcmp(label, "spo2_history") == 0)) {
            big_frame_.insert(big_frame_.end(), note.bytes, note.bytes + note.length);
        }
    }

    void device_info(uint16_t uuid, esp_err_t status, const uint8_t* bytes,
                     size_t length) override {
        hex_string(bytes, length, hex_, sizeof(hex_));
        char printable[257]{};
        const size_t count = length < sizeof(printable) - 1 ? length : sizeof(printable) - 1;
        for (size_t i = 0; i < count; ++i) {
            printable[i] = std::isprint(static_cast<unsigned char>(bytes[i])) ? bytes[i] : '.';
        }
        ESP_LOGI(kProbeTag, "DEVICE_INFO uuid=%04x status=%s len=%u hex=%s text=%s", uuid,
                 esp_err_to_name(status), static_cast<unsigned>(length), hex_, printable);
    }

    void result(const gateway::ProbeEntry& entry) override {
        ESP_LOGI(kProbeTag, "PROBE RESULT %s status=%s packets=%u bytes=%u elapsed_ms=%u",
                 entry.label, gateway::probe_result_name(entry.result),
                 static_cast<unsigned>(entry.packets), static_cast<unsigned>(entry.bytes),
                 static_cast<unsigned>(entry.elapsed_ms));
        if (entry.result == gateway::ProbeResult::response && publisher_) {
            std::string data;
            const char* kind = nullptr;
            esp_err_t status = ESP_ERR_NOT_SUPPORTED;
            if (std::strcmp(entry.label, "live_hr") == 0 && live_hr_ready_) {
                kind = "heartRate";
                status = serializer_.serialize(live_hr_, data);
            } else if (std::strcmp(entry.label, "live_spo2") == 0 && live_spo2_ready_) {
                kind = "spo2";
                status = serializer_.serialize(live_spo2_, data);
            } else if ((std::strcmp(entry.label, "hr_today") == 0 ||
                        std::strcmp(entry.label, "hr_yesterday") == 0) && hr_history_ready_) {
                kind = "heartRateHistory";
                status = serializer_.serialize(hr_history_, data);
            } else if (std::strcmp(entry.label, "sleep") == 0) {
                gateway::SleepRecord record;
                gateway::SleepParser parser;
                status = parser.parse(big_frame_.data(), big_frame_.size(), record);
                if (status == ESP_OK) {
                    kind = "sleep";
                    status = serializer_.serialize(record, data);
                }
            } else if (std::strcmp(entry.label, "spo2_history") == 0) {
                gateway::Spo2HistoryRecord record;
                gateway::Spo2HistoryParser parser;
                status = parser.parse(big_frame_.data(), big_frame_.size(), record);
                if (status == ESP_OK) {
                    kind = "spo2History";
                    status = serializer_.serialize(record, data);
                }
            }
            if (status == ESP_OK && kind) {
                publish(kind, data);
            } else if (kind || std::strcmp(entry.label, "sleep") == 0 ||
                       std::strcmp(entry.label, "spo2_history") == 0) {
                ESP_LOGW(kProbeTag, "No JSON for %s: %s", entry.label,
                         esp_err_to_name(status));
            }
        }
        if (std::strcmp(entry.label, "hr_today") == 0 ||
            std::strcmp(entry.label, "hr_yesterday") == 0) {
            hr_history_parser_.reset();
            hr_history_ready_ = false;
        }
        if (std::strcmp(entry.label, "sleep") == 0 ||
            std::strcmp(entry.label, "spo2_history") == 0) {
            big_frame_.clear();
        }
    }

    void finished(const gateway::ProbeSummary& summary) override {
        ESP_LOGI(kProbeTag, "PROBE SUMMARY BEGIN");
        for (const auto& entry : summary.entries) {
            ESP_LOGI(kProbeTag, "PROBE SUMMARY %s=%s packets=%u bytes=%u", entry.label,
                     gateway::probe_result_name(entry.result), static_cast<unsigned>(entry.packets),
                     static_cast<unsigned>(entry.bytes));
        }
        ESP_LOGI(kProbeTag, "PROBE SUMMARY lost_notifications=%u",
                 static_cast<unsigned>(summary.lost_notifications));
        ESP_LOGI(kProbeTag, "PROBE SUMMARY END");
    }

  private:
    void publish(const char* kind, const std::string& data) {
        // TODO: After end-to-end validation, use a stable ID and measurement time for each ring reading so resyncs do not create duplicate rows.
        const std::time_t now = std::time(nullptr);
        std::tm utc{};
        if (now < 1577836800 || !gmtime_r(&now, &utc)) {
            ESP_LOGW(kProbeTag, "Skipping %s publish: clock unavailable", kind);
            return;
        }
        char observed_at[21]{};
        if (std::strftime(observed_at, sizeof(observed_at), "%Y-%m-%dT%H:%M:%SZ", &utc) == 0) {
            return;
        }
        char record_id[33]{};
        std::snprintf(record_id, sizeof(record_id), "%08x%08x%08x%08x",
                      static_cast<unsigned>(esp_random()), static_cast<unsigned>(esp_random()),
                      static_cast<unsigned>(esp_random()), static_cast<unsigned>(esp_random()));
        const nlohmann::json message = {{"schemaVersion", 1},
                                        {"recordId", record_id},
                                        {"deviceId", CONFIG_GATEWAY_DEVICE_ID},
                                        {"gatewayId", CONFIG_GATEWAY_GATEWAY_ID},
                                        {"userId", CONFIG_GATEWAY_USER_ID},
                                        {"kind", kind},
                                        {"observedAt", observed_at},
                                        {"data", nlohmann::json::parse(data)}};
        const std::string json = message.dump();
        const auto status = publisher_->publish(CONFIG_GATEWAY_MQTT_TOPIC, json.c_str());
        if (status != ESP_OK) {
            ESP_LOGW(kProbeTag, "MQTT publish %s failed: %s", kind, esp_err_to_name(status));
        } else {
            ESP_LOGI(kProbeTag, "MQTT submitted kind=%s recordId=%s", kind, record_id);
        }
    }

    char hex_[517 * 3 + 1]{};
    gateway::MqttPublisher* publisher_ = nullptr;
    gateway::RingJson serializer_;
    gateway::HeartRateHistoryParser hr_history_parser_;
    gateway::HeartRateHistoryRecord hr_history_;
    gateway::HeartRateReading live_hr_;
    gateway::Spo2Reading live_spo2_{};
    bool hr_history_ready_ = false;
    bool live_hr_ready_ = false;
    bool live_spo2_ready_ = false;
    std::vector<uint8_t> big_frame_;
};

void run_probe() {
    ESP_ERROR_CHECK(nvs_flash_init());
    uint32_t configured_time = 0;
    const bool time_requested = CONFIG_GATEWAY_PROBE_SET_TIME_UTC[0] != '\0';
    const bool configured_time_valid =
        time_requested && parse_utc_timestamp(CONFIG_GATEWAY_PROBE_SET_TIME_UTC, configured_time);
    if (time_requested && !configured_time_valid) {
        ESP_LOGE(kProbeTag, "Invalid one-shot UTC timestamp; expected YYYY-MM-DDTHH:MM:SSZ");
    }
    uint32_t midnight = 0;
    bool clock_valid = false;
    uint32_t synchronized_epoch = 0;
    static gateway::WifiManager wifi;
    const bool wifi_connected = CONFIG_GATEWAY_WIFI_SSID[0] != '\0' && wifi.connect() == ESP_OK;
    if (!wifi_connected) {
        ESP_LOGW(kProbeTag, "Wi-Fi unavailable; ring values will not be published");
    }
    if (wifi_connected) {
        esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
        if (esp_netif_sntp_init(&config) == ESP_OK) {
            clock_valid = esp_netif_sntp_sync_wait(pdMS_TO_TICKS(15000)) == ESP_OK;
            if (clock_valid) {
                synchronized_epoch = static_cast<uint32_t>(std::time(nullptr));
            }
            esp_netif_sntp_deinit();
        }
    }
    static gateway::MqttPublisher mqtt;
    bool mqtt_connected = false;
    if (wifi_connected && CONFIG_GATEWAY_MQTT_URI[0] != '\0') {
        const auto status = mqtt.connect();
        mqtt_connected = status == ESP_OK;
        if (!mqtt_connected) {
            ESP_LOGW(kProbeTag, "MQTT unavailable: %s", esp_err_to_name(status));
        }
    }
    if (mqtt_connected && !clock_valid) {
        synchronized_epoch = static_cast<uint32_t>(std::time(nullptr));
        clock_valid = synchronized_epoch >= 1577836800;
    }
    const auto date_status = gateway::ProbeDate::resolve(
        CONFIG_GATEWAY_PROBE_DATE, clock_valid ? synchronized_epoch : configured_time,
        clock_valid || configured_time_valid, midnight);
    if (date_status == ESP_OK) {
        ESP_LOGI(kProbeTag, "Probe date UTC midnight epoch=%u source=%s",
                 static_cast<unsigned>(midnight),
                 CONFIG_GATEWAY_PROBE_DATE[0] ? "menuconfig"
                 : clock_valid                ? "SNTP"
                                              : "configured UTC timestamp");
    } else {
        ESP_LOGW(kProbeTag, "Probe date unavailable (%s); HR history will be skipped",
                 esp_err_to_name(date_status));
    }

    static gateway::RingBleClient transport;
    ESP_LOGI(kProbeTag, "Scanning exact_name=%s address_filter=%s", CONFIG_GATEWAY_PROBE_NAME,
             CONFIG_GATEWAY_PROBE_ADDRESS[0] ? CONFIG_GATEWAY_PROBE_ADDRESS : "none");
    const auto opened =
        transport.open(CONFIG_GATEWAY_PROBE_NAME, CONFIG_GATEWAY_PROBE_ADDRESS, 60000);
    if (opened != ESP_OK) {
        ESP_LOGE(kProbeTag, "BLE open failed: %s", esp_err_to_name(opened));
        return;
    }
    const auto subscribed = transport.subscribe();
    ESP_LOGI(kProbeTag, "GATT subscribed=%s command=%u big_data=%u", esp_err_to_name(subscribed),
             transport.has_channel(gateway::RingChannel::command),
             transport.has_channel(gateway::RingChannel::big_data));
    static MonitorObserver observer;
    observer.set_publisher(mqtt_connected ? &mqtt : nullptr);
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
                    previous_result <= static_cast<uint8_t>(gateway::ProbeResult::malformed)) {
                    ESP_LOGI(kProbeTag, "One-shot ring time already attempted: %s; skipping",
                             gateway::probe_result_name(
                                 static_cast<gateway::ProbeResult>(previous_result)));
                } else {
                    ESP_LOGI(kProbeTag,
                             "One-shot ring time already attempted; result unavailable; skipping");
                }
            } else if (read_marker != ESP_OK && read_marker != ESP_ERR_NVS_NOT_FOUND) {
                ESP_LOGE(kProbeTag, "Cannot read one-shot marker: %s",
                         esp_err_to_name(read_marker));
            } else {
                const auto write_marker = nvs_set_u32(handle, "time_tag", configured_time);
                const auto saved = write_marker == ESP_OK ? nvs_commit(handle) : write_marker;
                if (saved != ESP_OK) {
                    ESP_LOGE(kProbeTag, "Cannot save one-shot marker: %s", esp_err_to_name(saved));
                } else {
                    const uint32_t send_time =
                        clock_valid ? static_cast<uint32_t>(std::time(nullptr)) : configured_time;
                    ESP_LOGI(kProbeTag, "One-shot ring time UTC epoch=%u source=%s",
                             static_cast<unsigned>(send_time),
                             clock_valid ? "SNTP" : "configured timestamp");
                    const auto result = runner.set_time(transport, send_time, observer);
                    const auto saved_result =
                        nvs_set_u8(handle, "time_result", static_cast<uint8_t>(result.result));
                    if (saved_result == ESP_OK) {
                        nvs_commit(handle);
                    }
                }
            }
            nvs_close(handle);
        }
    }
    runner.run(transport, midnight, date_status == ESP_OK, observer);
    transport.disconnect();
    ESP_LOGI(kProbeTag, "Probe session finished; reset to run again");
}
} // namespace

extern "C" void app_main(void) {
    run_probe();
}
