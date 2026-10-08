#pragma once

#include <string>

#include "esp_err.h"
#include "models/RingData.h"

namespace gateway {

// Converts decoded ring data into JSON text for publishing.
// Produces measurement data objects; the publisher supplies identity and event metadata.
// Unsupported readings clear the output and return ESP_ERR_NOT_SUPPORTED.
// Callers must check the result before publishing.
class RingJson {
public:
    esp_err_t serialize(const HeartRateReading& reading, std::string& json);
    esp_err_t serialize(const HeartRateHistoryRecord& record, std::string& json);
    esp_err_t serialize(const HrvHistoryRecord& record, std::string& json);
    esp_err_t serialize(const Spo2Reading& reading, std::string& json);
    esp_err_t serialize(const Spo2HistoryRecord& record, std::string& json);
    esp_err_t serialize(const SleepRecord& record, std::string& json);
    esp_err_t serialize(const StepsReading& reading, std::string& json);
};

}  // namespace gateway
