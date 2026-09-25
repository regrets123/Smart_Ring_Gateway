#pragma once

#include "esp_err.h"

namespace gateway {

// TLS policy, broker configuration, and JSON schema are still to be defined.
class MqttPublisher {
public:
    esp_err_t connect();
    esp_err_t publish(const char* topic, const char* json);
    esp_err_t disconnect();
};

}  // namespace gateway
