#include "serialization/RingJson.h"

#include <nlohmann/json.hpp>

namespace gateway {

esp_err_t RingJson::serialize(const HeartRateReading& reading, std::string& json) {
    json.clear();
    if (reading.bpm <= 0 || reading.bpm > 255) {
        return ESP_ERR_INVALID_ARG;
    }

    const nlohmann::json payload = {{"bpm", reading.bpm}};
    json = payload.dump();
    return ESP_OK;
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

} // namespace gateway
