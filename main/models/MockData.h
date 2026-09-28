#pragma once

#include <string>

namespace gateway {

// Temporary publishing-test data; this is not a decoded COLMI measurement.
struct MockReading {
    std::string device_id;
    std::string gateway_id;
    std::string user_id;
    int reading;
};

}  // namespace gateway
