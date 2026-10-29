#pragma once

#include <atomic>
#include <mutex>
#include <string>

#include "esp_err.h"
#include "esp_event_base.h"
#include "freertos/FreeRTOS.h" // IWYU pragma: keep; required before event_groups.h
#include "freertos/event_groups.h"
#include "mqtt_client.h"

namespace gateway {

// Publishes JSON text and shares one SNTP state between probe dating and TLS.
class MqttPublisher {
public:
    esp_err_t sync_clock();
    esp_err_t connect();
    // Returns ESP_OK only once the Pi reports the record committed to SQLite
    // (stored/updated/duplicate). ESP_ERR_INVALID_RESPONSE: the Pi rejected it.
    // ESP_ERR_TIMEOUT: no broker ack or no database receipt.
    esp_err_t publish(const char* topic, const char* json, const char* record_id);
    esp_err_t disconnect();

private:
    static void on_event(void* arg, esp_event_base_t base, int32_t id, void* data);
    void handle_receipt(const esp_mqtt_event_t& event);
    std::string receipt_topic_;
    std::mutex receipt_mutex_;
    std::string expected_record_id_;
    std::string receipt_status_;
    bool sntp_initialized_ = false;
    bool clock_synced_ = false;
    std::atomic<int> acknowledged_id_{-1};
    esp_mqtt_client_handle_t client_ = nullptr;
    EventGroupHandle_t events_ = nullptr;
};

}  // namespace gateway
