#pragma once

#include <cstddef>
#include <cstdint>

#include "esp_err.h"

namespace gateway {

// Normal command/notification path; packet format is intentionally undecided.
class ColmiProtocol {
public:
    esp_err_t encode_command(const uint8_t* input, size_t input_length,
                             uint8_t* output, size_t output_capacity,
                             size_t& output_length);
    esp_err_t decode_notification(const uint8_t* bytes, size_t length);
};

}  // namespace gateway
