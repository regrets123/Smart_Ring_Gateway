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

class HeartRateHistoryParser {
public:
    // Feed one complete 0x15 notification at a time. complete is true when record is ready.
    esp_err_t parse(const uint8_t* bytes, size_t length,
                    HeartRateHistoryRecord& record, bool& complete);
    void reset();

private:
    uint8_t packet_count_ = 0;
    uint8_t next_index_ = 0;
    HeartRateHistoryRecord pending_;
};

class HrvHistoryParser {
public:
    // Feed complete 0x39 notifications in arrival order. The 0xff marker ends
    // the multi-day stream; complete is true only after that marker.
    esp_err_t parse(const uint8_t* bytes, size_t length,
                    HrvHistoryRecord& record, bool& complete);
    void reset();

private:
    uint8_t page_count_ = 0;
    uint8_t next_index_ = 0;
    uint8_t days_ago_ = 0;
    HrvHistoryRecord pending_;
};

class Spo2Parser {
public:
    esp_err_t parse(const uint8_t* bytes, size_t length, Spo2Reading& reading);
};

class Spo2HistoryParser {
public:
    // Expects a complete, reassembled 0x2A Big Data frame.
    esp_err_t parse(const uint8_t* bytes, size_t length, Spo2HistoryRecord& record);
};

class SleepParser {
public:
    // Expects a complete, reassembled 0x27 Big Data frame.
    esp_err_t parse(const uint8_t* bytes, size_t length, SleepRecord& record);
};

}  // namespace gateway
