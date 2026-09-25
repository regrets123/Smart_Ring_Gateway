#pragma once

#include "esp_err.h"

namespace gateway {

class WifiManager {
public:
    esp_err_t connect();
    esp_err_t disconnect();
};

}  // namespace gateway
