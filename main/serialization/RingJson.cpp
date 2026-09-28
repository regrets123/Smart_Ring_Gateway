#include "serialization/RingJson.h"

#include <nlohmann/json.hpp>

namespace gateway {

namespace {
bool valid_id(const std::string& id) {
    if (id.empty() || id.size() > 64) return false;
    for (unsigned char ch : id) {
        if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
              (ch >= '0' && ch <= '9') || ch == '-' || ch == '_')) return false;
    }
    return true;
}
}  // namespace

esp_err_t RingJson::serialize(const MockReading& reading, std::string& json) {
    json.clear();
    if (!valid_id(reading.device_id) || !valid_id(reading.gateway_id) ||
        !valid_id(reading.user_id)) return ESP_ERR_INVALID_ARG;

    const nlohmann::json payload = {
        {"deviceId", reading.device_id},
        {"gatewayId", reading.gateway_id},
        {"userId", reading.user_id},
        {"reading", reading.reading},
    };
    json = payload.dump();
    return ESP_OK;
}

// Real ring conversions remain undefined until the BLE upload format is agreed.
esp_err_t RingJson::serialize(const HeartRateReading&, std::string& json) {
    json.clear();
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t RingJson::serialize(const Spo2Reading&, std::string& json) {
    json.clear();
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t RingJson::serialize(const SleepRecord&, std::string& json) {
    json.clear();
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t RingJson::serialize(const StepsReading&, std::string& json) {
    json.clear();
    return ESP_ERR_NOT_SUPPORTED;
}

}  // namespace gateway
