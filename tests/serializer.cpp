#include <climits>
#include <nlohmann/json.hpp>
#include "serialization/RingJson.h"

int main() {
    gateway::RingJson serializer;
    std::string payload;
    for (int value : {42, 0, -1, INT_MIN, INT_MAX}) {
        const gateway::MockReading mock{"ring-01", "gateway-01", "user-01", value};
        if (serializer.serialize(mock, payload) != ESP_OK) return 1;
        const auto data = nlohmann::json::parse(payload, nullptr, false);
        if (data.is_discarded() || data.size() != 4 ||
            data["deviceId"] != "ring-01" || data["gatewayId"] != "gateway-01" ||
            data["userId"] != "user-01" || data["reading"] != value) return 2;
    }
    payload = "stale";
    if (serializer.serialize(gateway::MockReading{"", "gateway-01", "user-01", 42}, payload)
        != ESP_ERR_INVALID_ARG || !payload.empty()) return 3;
    payload = "stale";
    if (serializer.serialize(gateway::HeartRateReading{}, payload)
        != ESP_ERR_NOT_SUPPORTED || !payload.empty()) return 4;
    return 0;
}
