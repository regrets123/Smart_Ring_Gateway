#pragma once

#include <cstddef>
#include <cstdint>

#include "esp_err.h"

namespace gateway {

// Separate path for fragmented history transfers. Buffer limits are TBD.
class BigDataReassembler {
public:
    esp_err_t push_chunk(const uint8_t* bytes, size_t length);
    void reset();
};

}  // namespace gateway
