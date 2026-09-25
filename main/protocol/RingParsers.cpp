#include "protocol/RingParsers.h"

namespace gateway {

esp_err_t HeartRateParser::parse(const uint8_t*, size_t, HeartRateReading&) {
    return ESP_ERR_NOT_SUPPORTED;
}
esp_err_t Spo2Parser::parse(const uint8_t*, size_t, Spo2Reading&) {
    return ESP_ERR_NOT_SUPPORTED;
}
esp_err_t SleepParser::parse(const uint8_t*, size_t, SleepRecord&) {
    return ESP_ERR_NOT_SUPPORTED;
}
esp_err_t StepsParser::parse(const uint8_t*, size_t, StepsReading&) {
    return ESP_ERR_NOT_SUPPORTED;
}

}  // namespace gateway
