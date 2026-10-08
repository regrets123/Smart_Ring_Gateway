#pragma once

#include "esp_err.h"
#include "esp_event.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "mqtt_client.h"

namespace gateway {

// Publishes JSON text and shares one SNTP state between probe dating and TLS.
class MqttPublisher {
public:
    esp_err_t sync_clock();
    esp_err_t connect();
    esp_err_t publish(const char* topic, const char* json);
    esp_err_t disconnect();

private:
    static void on_event(void* arg, esp_event_base_t base, int32_t id, void* data);
    bool sntp_initialized_ = false;
    bool clock_synced_ = false;
    esp_mqtt_client_handle_t client_ = nullptr;
    EventGroupHandle_t events_ = nullptr;
};

}  // namespace gateway
