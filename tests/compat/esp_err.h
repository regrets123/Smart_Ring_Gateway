#pragma once

// Host-only error constants for compiling the real serializer without ESP-IDF.
using esp_err_t = int;
constexpr esp_err_t ESP_OK = 0;
constexpr esp_err_t ESP_ERR_INVALID_ARG = 0x102;
constexpr esp_err_t ESP_ERR_NOT_SUPPORTED = 0x106;
