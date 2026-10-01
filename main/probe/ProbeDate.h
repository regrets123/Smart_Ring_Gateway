#pragma once

#include <cstdint>

#include "esp_err.h"

namespace gateway {

class ProbeDate {
public:
    static esp_err_t resolve(const char* configured_yyyy_mm_dd,
                             uint32_t synchronized_epoch, bool clock_valid,
                             uint32_t& utc_midnight_epoch);
};

}  // namespace gateway
