#pragma once

#include <cstddef>
#include <cstdint>

#include "esp_err.h"

namespace gateway {

// BLE transport only. Discovery, connection, and notifications belong here.
class RingBleClient {
public:
    // Notification bytes are borrowed and valid only during the callback.
    // Context lets the caller route bytes to the selected device's protocol state.
    using NotificationHandler = void (*)(const uint8_t* bytes, size_t length,
                                         void* context);

    esp_err_t start_scan();
    // Select a registered device by its logical ID. Mapping that ID to a BLE
    // peer will be implemented after discovery; it is not a BLE address string.
    esp_err_t connect(const char* device_id);
    esp_err_t write(const uint8_t* bytes, size_t length);
    esp_err_t subscribe(NotificationHandler handler, void* context);
    esp_err_t disconnect();
};

}  // namespace gateway
