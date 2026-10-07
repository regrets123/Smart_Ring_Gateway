#pragma once

#include <cstdint>
#include <vector>

namespace gateway {

struct HeartRateReading {
    int bpm = 0;
};

struct HeartRateHistoryRecord {
    uint32_t utc_time = 0;
    uint8_t range = 0;
    // Sample bytes in packet order; trailing padding is retained.
    std::vector<uint8_t> samples;
};
struct Spo2Reading {
    int o2Perc;
};

struct Spo2HistorySample {
    uint8_t min = 0;
    uint8_t max = 0;
};

struct Spo2HistoryRecord {
    uint8_t unknown = 0;
    uint8_t days_ago = 0;
    std::vector<Spo2HistorySample> samples;
    std::vector<uint8_t> raw_payload;
};

enum class SleepStage : uint8_t {
    unknown = 0,
    light = 2,
    deep = 3,
    awake = 5,
};

struct SleepStageSpan {
    SleepStage stage = SleepStage::unknown;
    uint8_t duration_min = 0;
};

struct SleepNight {
    uint8_t days_ago = 0;
    // Signed minute offsets from midnight of the day identified by days_ago.
    int16_t start_min = 0;
    int16_t end_min = 0;
    std::vector<SleepStageSpan> stages;
};

struct SleepRecord {
    // One Big Data response can contain several nights.
    std::vector<SleepNight> nights;
    // Retained while the M7083 field layout is checked against real captures.
    std::vector<uint8_t> raw_payload;
};
struct StepsReading {
    int steps;
};

}  // namespace gateway
