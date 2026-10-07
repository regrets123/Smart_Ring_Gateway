#pragma once

namespace gateway {

struct HeartRateReading {
    int bpm = 0;
};
struct Spo2Reading {
    int o2Perc;
};
struct SleepRecord {};
struct StepsReading {};

}  // namespace gateway
