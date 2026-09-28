#pragma once

#include <atomic>

#include "esp_err.h"
#include "esp_event.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

namespace gateway {

class WifiManager {
public:
    esp_err_t connect();
    esp_err_t disconnect();

private:
    static void on_event(void* arg, esp_event_base_t base, int32_t id, void* data);
    EventGroupHandle_t events_ = nullptr;
    esp_event_handler_instance_t wifi_handler_ = nullptr;
    esp_event_handler_instance_t ip_handler_ = nullptr;
    bool initialized_ = false;
    std::atomic<int> retries_{0};
};

}  // namespace gateway
