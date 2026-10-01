#pragma once

#include <cstddef>
#include <cstdint>

#include "esp_err.h"

namespace gateway {

enum class RingChannel { command, big_data };

struct RingNotification {
    RingChannel channel = RingChannel::command;
    size_t length = 0;
    uint8_t bytes[517]{};
};

class IRingTransport {
public:
    virtual ~IRingTransport() = default;
    virtual bool has_channel(RingChannel channel) const = 0;
    virtual esp_err_t write(RingChannel channel, const uint8_t* bytes, size_t length) = 0;
    virtual esp_err_t receive(RingNotification& notification, uint32_t timeout_ms) = 0;
    virtual esp_err_t read_device_info(uint16_t characteristic_uuid, uint8_t* out,
                                       size_t capacity, size_t& length) = 0;
    virtual uint32_t lost_notifications() const = 0;
    virtual bool is_connected() const = 0;
    virtual esp_err_t disconnect() = 0;
};

}  // namespace gateway
