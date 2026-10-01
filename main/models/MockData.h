#pragma once

#include <string>

namespace gateway {

// Temporary publishing-test data; this is not a decoded COLMI measurement.
struct MockReading {
    std::string record_id;
    std::string device_id;
    std::string gateway_id;
    std::string user_id;
    std::string observed_at;
    int bpm;
};

}  // namespace gateway
