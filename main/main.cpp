#include <cctype>
#include <cstdio>
#include <cstring>
#include <cinttypes>
#include <ctime>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "esp_log.h"
#include "esp_random.h" // IWYU pragma: keep
#include "freertos/FreeRTOS.h" // IWYU pragma: keep; required before task.h
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
constexpr uint32_t kSyncIntervalSeconds = 23 * 60 * 60;
constexpr uint32_t kRetrySeconds = 10;
constexpr uint32_t kIdleCheckSeconds = 60;

gateway::WifiManager& wifi_manager() {
    static gateway::WifiManager wifi;
    return wifi;
}

gateway::MqttPublisher& mqtt_publisher() {
    static gateway::MqttPublisher mqtt;
    return mqtt;
}

uint64_t stable_record_id(const char* kind, uint32_t day_epoch) {
    uint64_t hash = 14695981039346656037ULL;
    const auto add = [&hash](const char* value) {
        for (; *value; ++value) {
            hash = (hash ^ static_cast<uint8_t>(*value)) * 1099511628211ULL;
        }
        hash = (hash ^ 0xffu) * 1099511628211ULL;
    };
    add(CONFIG_GATEWAY_DEVICE_ID);
    add(kind);
    char day[16]{};
    std::snprintf(day, sizeof(day), "%" PRIu32, day_epoch);
    add(day);
    return hash;
}

bool is_hr_history(const char* label) {
    return std::strcmp(label, "hr_today") == 0 || std::strcmp(label, "hr_yesterday") == 0;
}

bool is_big_data_history(const char* label) {
    return std::strcmp(label, "sleep") == 0 || std::strcmp(label, "spo2_history") == 0;
}

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
    void set_hrv_day_anchor(uint32_t probe_midnight_utc) {
        hrv_probe_midnight_utc_ = probe_midnight_utc;
    }

    void set_publisher(gateway::MqttPublisher* publisher) {
        publisher_ = publisher;
        publish_failed_ = false;
        hr_history_parser_.reset();
        hr_history_ready_ = false;
        hrv_history_parser_.reset();
        hrv_history_ready_ = false;
        hrv_history_error_ = false;
        live_hr_ready_ = false;
        live_spo2_ready_ = false;
        hrv_pages_seen_ = false;
        big_frame_.clear();
    }

    bool publish_failed() const { return publish_failed_; }

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
        const bool command_match = matched && note.channel == gateway::RingChannel::command;
        if (std::strcmp(label, "hrv") == 0 && command_match && note.length == 16 &&
            checksum == gateway::PacketStatus::valid) {
            bool complete = false;
            gateway::HrvHistoryRecord record;
            const auto status = hrv_history_parser_.parse(note.bytes, note.length, record, complete);
            if (status == ESP_OK && complete) {
                hrv_history_ = std::move(record);
                hrv_history_.probe_midnight_utc = hrv_probe_midnight_utc_;
                hrv_history_ready_ = true;
            } else if (status != ESP_OK && status != ESP_ERR_NOT_FOUND) {
                hrv_history_error_ = true;
                ESP_LOGW(kProbeTag, "HRV parse failed: %s", esp_err_to_name(status));
            }
            if (note.bytes[1] == 0xff) {
                if (hrv_pages_seen_) {
                    ESP_LOGI(kProbeTag, "HRV END marker=ff");
                } else {
                    ESP_LOGI(kProbeTag, "HRV NO_DATA marker=ff");
                }
            } else if (note.bytes[1] == 0) {
                ESP_LOGI(kProbeTag, "HRV HEADER page=0 reported_pages=%u",
                         static_cast<unsigned>(note.bytes[2]));
            } else {
                hrv_pages_seen_ = true;
                char payload_hex[14 * 3 + 1]{};
                hex_string(note.bytes + 2, 13, payload_hex, sizeof(payload_hex));
                ESP_LOGI(kProbeTag, "HRV PAGE page=%u payload_hex=%s",
                         static_cast<unsigned>(note.bytes[1]), payload_hex);
            }
        }
        if (command_match && note.length == 16 &&
            note.bytes[0] == 0x03 && checksum == gateway::PacketStatus::valid) {
            ESP_LOGI(kProbeTag, "BATTERY candidate_percent=%u candidate_charge_flag=%u",
                     note.bytes[1], note.bytes[2]);
        }

        if (command_match && std::strcmp(label, "live_hr") == 0) {
            gateway::HeartRateParser parser;
            gateway::HeartRateReading reading;
            if (parser.parse(note.bytes, note.length, reading) == ESP_OK) {
                live_hr_ = reading;
                live_hr_ready_ = true;
            }
        } else if (command_match && std::strcmp(label, "live_spo2") == 0) {
            gateway::Spo2Parser parser;
            gateway::Spo2Reading reading{};
            if (parser.parse(note.bytes, note.length, reading) == ESP_OK) {
                live_spo2_ = reading;
                live_spo2_ready_ = true;
            }
        } else if (command_match && is_hr_history(label)) {
            bool complete = false;
            const auto status =
                hr_history_parser_.parse(note.bytes, note.length, hr_history_, complete);
            hr_history_ready_ = status == ESP_OK && complete;
        } else if (matched && note.channel == gateway::RingChannel::big_data &&
                   is_big_data_history(label)) {
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
            } else if (is_hr_history(entry.label) && hr_history_ready_) {
                kind = "heartRateHistory";
                status = serializer_.serialize(hr_history_, data);
            } else if (std::strcmp(entry.label, "hrv") == 0) {
                kind = "hrvHistory";
                status = hrv_history_ready_ && !hrv_history_error_
                             ? serializer_.serialize(hrv_history_, data)
                             : ESP_ERR_INVALID_RESPONSE;
            } else if (std::strcmp(entry.label, "sleep") == 0) {
                kind = "sleep";
                status = serialize_big_data<gateway::SleepParser, gateway::SleepRecord>(data);
            } else if (std::strcmp(entry.label, "spo2_history") == 0) {
                kind = "spo2History";
                status = serialize_big_data<gateway::Spo2HistoryParser,
                                            gateway::Spo2HistoryRecord>(data);
            }
            if (status == ESP_OK && kind) {
                if (std::strcmp(entry.label, "hrv") == 0) {
                    if (!hrv_probe_midnight_utc_) {
                        publish_failed_ = true;
                    }
                    bool seen[256]{};
                    for (const auto& source : hrv_history_.samples) {
                        const uint32_t offset = source.days_ago;
                        if (seen[offset]) continue;
                        seen[offset] = true;
                        gateway::HrvHistoryRecord day;
                        day.interval_minutes = hrv_history_.interval_minutes;
                        day.probe_midnight_utc = hrv_history_.probe_midnight_utc;
                        for (const auto& sample : hrv_history_.samples) {
                            if (sample.days_ago == offset) day.samples.push_back(sample);
                        }
                        if (hrv_probe_midnight_utc_ < offset * 86400u) {
                            publish_failed_ = true;
                            continue;
                        }
                        const uint32_t epoch = hrv_probe_midnight_utc_ - offset * 86400u;
                        std::string day_json;
                        if (serializer_.serialize(day, day_json) == ESP_OK) {
                            publish(kind, day_json, epoch);
                        }
                    }
                } else if (std::strcmp(entry.label, "sleep") == 0) {
                    gateway::SleepRecord record;
                    gateway::SleepParser parser;
                    if (parser.parse(big_frame_.data(), big_frame_.size(), record) == ESP_OK) {
                        for (const auto& night : record.nights) {
                            gateway::SleepRecord one;
                            one.nights.push_back(night);
                            const uint32_t offset = static_cast<uint32_t>(night.days_ago) * 86400u;
                            if (hrv_probe_midnight_utc_ < offset) {
                                publish_failed_ = true;
                                continue;
                            }
                            const uint32_t epoch = hrv_probe_midnight_utc_ - offset;
                            std::string night_json;
                            if (serializer_.serialize(one, night_json) == ESP_OK) {
                                publish(kind, night_json, epoch);
                            }
                        }
                    }
                } else {
                    uint32_t day_epoch = 0;
                    if (is_hr_history(entry.label)) {
                        day_epoch = hr_history_.utc_time - hr_history_.utc_time % 86400u;
                    } else if (std::strcmp(entry.label, "spo2_history") == 0) {
                        gateway::Spo2HistoryRecord record;
                        gateway::Spo2HistoryParser parser;
                        if (parser.parse(big_frame_.data(), big_frame_.size(), record) == ESP_OK) {
                            const uint32_t offset = static_cast<uint32_t>(record.days_ago) * 86400u;
                            if (hrv_probe_midnight_utc_ >= offset) {
                                day_epoch = hrv_probe_midnight_utc_ - offset;
                            }
                        }
                    }
                    publish(kind, data, day_epoch);
                }
            } else if (kind) {
                publish_failed_ = true;
                ESP_LOGW(kProbeTag, "No JSON for %s: %s", entry.label,
                         esp_err_to_name(status));
            }
        }
        if (is_hr_history(entry.label)) {
            hr_history_parser_.reset();
            hr_history_ready_ = false;
        }
        if (std::strcmp(entry.label, "hrv") == 0) {
            hrv_history_parser_.reset();
            hrv_history_ready_ = false;
            hrv_history_error_ = false;
            hrv_pages_seen_ = false;
        }
        if (is_big_data_history(entry.label)) {
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
    template <typename Parser, typename Record>
    esp_err_t serialize_big_data(std::string& data) {
        Record record;
        Parser parser;
        const auto status = parser.parse(big_frame_.data(), big_frame_.size(), record);
        return status == ESP_OK ? serializer_.serialize(record, data) : status;
    }

    void publish(const char* kind, const std::string& data, uint32_t day_epoch) {
        const bool historical = std::strcmp(kind, "heartRate") != 0 &&
                                std::strcmp(kind, "spo2") != 0;
        if (historical && !day_epoch) {
            publish_failed_ = true;
            ESP_LOGW(kProbeTag, "Skipping %s publish: measurement day unavailable", kind);
            return;
        }
        const std::time_t now = std::time(nullptr);
        std::tm utc{};
        if (now < 1577836800 || !gmtime_r(&now, &utc)) {
            ESP_LOGW(kProbeTag, "Skipping %s publish: clock unavailable", kind);
            if (historical) publish_failed_ = true;
            return;
        }
        char observed_at[21]{};
        if (std::strftime(observed_at, sizeof(observed_at), "%Y-%m-%dT%H:%M:%SZ", &utc) == 0) {
            return;
        }
        char record_id[33]{};
        if (day_epoch != 0) {
            std::snprintf(record_id, sizeof(record_id), "%016" PRIx64,
                          stable_record_id(kind, day_epoch));
        } else {
            std::snprintf(record_id, sizeof(record_id), "%08x%08x%08x%08x",
                          static_cast<unsigned>(esp_random()), static_cast<unsigned>(esp_random()),
                          static_cast<unsigned>(esp_random()), static_cast<unsigned>(esp_random()));
        }
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
            if (historical) publish_failed_ = true;
            ESP_LOGW(kProbeTag, "MQTT publish %s failed: %s", kind, esp_err_to_name(status));
        } else {
            ESP_LOGI(kProbeTag, "MQTT submitted kind=%s recordId=%s", kind, record_id);
        }
    }

    char hex_[517 * 3 + 1]{};
    gateway::MqttPublisher* publisher_ = nullptr;
    bool publish_failed_ = false;
    gateway::RingJson serializer_;
    gateway::HeartRateHistoryParser hr_history_parser_;
    gateway::HeartRateHistoryRecord hr_history_;
    gateway::HrvHistoryParser hrv_history_parser_;
    gateway::HrvHistoryRecord hrv_history_;
    gateway::HeartRateReading live_hr_;
    gateway::Spo2Reading live_spo2_{};
    bool hr_history_ready_ = false;
    bool live_hr_ready_ = false;
    bool live_spo2_ready_ = false;
    bool hrv_pages_seen_ = false;
    bool hrv_history_ready_ = false;
    bool hrv_history_error_ = false;
    uint32_t hrv_probe_midnight_utc_ = 0;
    std::vector<uint8_t> big_frame_;
};

bool run_probe() {
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
    auto& wifi = wifi_manager();
    auto& mqtt = mqtt_publisher();
    const bool wifi_connected = CONFIG_GATEWAY_WIFI_SSID[0] != '\0' && wifi.connect() == ESP_OK;
    if (!wifi_connected) {
        ESP_LOGW(kProbeTag, "Wi-Fi unavailable; retrying before ring sync");
        return false;
    }
    if (wifi_connected) {
        clock_valid = mqtt.sync_clock() == ESP_OK;
        if (clock_valid) {
            synchronized_epoch = static_cast<uint32_t>(std::time(nullptr));
        }
    }
    bool mqtt_connected = false;
    if (wifi_connected && CONFIG_GATEWAY_MQTT_URI[0] != '\0') {
        const auto status = mqtt.connect();
        mqtt_connected = status == ESP_OK;
        if (!mqtt_connected) {
            ESP_LOGW(kProbeTag, "MQTT unavailable: %s", esp_err_to_name(status));
        }
    }
    if (!mqtt_connected) return false;
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
        transport.disconnect();
        return false;
    }
    const auto subscribed = transport.subscribe();
    ESP_LOGI(kProbeTag, "GATT subscribed=%s command=%u big_data=%u", esp_err_to_name(subscribed),
             transport.has_channel(gateway::RingChannel::command),
             transport.has_channel(gateway::RingChannel::big_data));
    static MonitorObserver observer;
    observer.set_publisher(mqtt_connected ? &mqtt : nullptr);
    observer.set_hrv_day_anchor(date_status == ESP_OK ? midnight : 0);
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
    const auto summary = runner.run(transport, midnight, date_status == ESP_OK, observer);
    transport.disconnect();
    bool history_ok = date_status == ESP_OK && !observer.publish_failed() &&
                      summary.lost_notifications == 0;
    bool history_seen = false;
    for (const auto& entry : summary.entries) {
        if (is_hr_history(entry.label) || std::strcmp(entry.label, "hrv") == 0 ||
            is_big_data_history(entry.label)) {
            if (entry.result == gateway::ProbeResult::response ||
                entry.result == gateway::ProbeResult::no_data) history_seen = true;
            else history_ok = false;
        }
    }
    return history_ok && history_seen;
}
} // namespace

extern "C" void app_main(void) {
    ESP_ERROR_CHECK(nvs_flash_init());
    for (;;) {
        const std::time_t now = std::time(nullptr);
        uint32_t last_sync = 0;
        nvs_handle_t handle;
        if (nvs_open("m7083_probe", NVS_READWRITE, &handle) == ESP_OK) {
            nvs_get_u32(handle, "last_sync", &last_sync);
            nvs_close(handle);
        }
        if (now >= 1577836800 && last_sync <= static_cast<uint64_t>(now) &&
            static_cast<uint64_t>(now) - last_sync < kSyncIntervalSeconds) {
            if (CONFIG_GATEWAY_WIFI_SSID[0] && wifi_manager().connect() == ESP_OK &&
                CONFIG_GATEWAY_MQTT_URI[0]) {
                mqtt_publisher().connect();
            }
            vTaskDelay(pdMS_TO_TICKS(kIdleCheckSeconds * 1000));
            continue;
        }
        if (run_probe()) {
            const uint32_t completed_at = static_cast<uint32_t>(std::time(nullptr));
            if (nvs_open("m7083_probe", NVS_READWRITE, &handle) == ESP_OK) {
                if (nvs_set_u32(handle, "last_sync", completed_at) == ESP_OK) {
                    nvs_commit(handle);
                }
                nvs_close(handle);
            }
            ESP_LOGI(kProbeTag, "History sync completed; waiting 23 hours");
        } else {
            ESP_LOGW(kProbeTag, "History sync incomplete; retrying when ring is nearby");
            vTaskDelay(pdMS_TO_TICKS(kRetrySeconds * 1000));
        }
    }
}
