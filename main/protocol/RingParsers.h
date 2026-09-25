#pragma once

#include <cstddef>
#include <cstdint>

#include "esp_err.h"
#include "models/RingData.h"

namespace gateway {

// Decoders produce domain data and have no BLE or MQTT dependencies.
class HeartRateParser {
public:
    esp_err_t parse(const uint8_t* bytes, size_t length, HeartRateReading& reading);
};

class Spo2Parser {
public:
    esp_err_t parse(const uint8_t* bytes, size_t length, Spo2Reading& reading);
};

class SleepParser {
public:
    esp_err_t parse(const uint8_t* bytes, size_t length, SleepRecord& record);
};

class StepsParser {
public:
    esp_err_t parse(const uint8_t* bytes, size_t length, StepsReading& reading);
};

}  // namespace gateway
