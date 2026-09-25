#pragma once

#include <cstddef>
#include <cstdint>

#include "esp_err.h"

namespace gateway {

// BLE transport only. Discovery, connection, and notifications belong here.
class RingBleClient {
public:
    esp_err_t start_scan();
    esp_err_t connect();
    esp_err_t write(const uint8_t* bytes, size_t length);
    esp_err_t subscribe();
    esp_err_t disconnect();
};

}  // namespace gateway
