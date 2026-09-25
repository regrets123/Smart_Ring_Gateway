#include "network/MqttPublisher.h"

namespace gateway {

esp_err_t MqttPublisher::connect() { return ESP_ERR_NOT_SUPPORTED; }
esp_err_t MqttPublisher::publish(const char*, const char*) {
    return ESP_ERR_NOT_SUPPORTED;
}
esp_err_t MqttPublisher::disconnect() { return ESP_ERR_NOT_SUPPORTED; }

}  // namespace gateway
