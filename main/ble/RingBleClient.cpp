#include "ble/RingBleClient.h"

namespace gateway {

esp_err_t RingBleClient::start_scan() { return ESP_ERR_NOT_SUPPORTED; }
esp_err_t RingBleClient::connect() { return ESP_ERR_NOT_SUPPORTED; }
esp_err_t RingBleClient::write(const uint8_t*, size_t) { return ESP_ERR_NOT_SUPPORTED; }
esp_err_t RingBleClient::subscribe() { return ESP_ERR_NOT_SUPPORTED; }
esp_err_t RingBleClient::disconnect() { return ESP_ERR_NOT_SUPPORTED; }

}  // namespace gateway
