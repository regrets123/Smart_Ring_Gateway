#include <nlohmann/json.hpp>
#include "serialization/RingJson.h"

int main() {
    gateway::RingJson serializer;
    std::string payload;
    const gateway::MockReading mock{
        "example-stable-record-id-001", "ring-01", "gateway-01", "user-01",
        "2026-10-01T12:00:00Z",         72};
    if (serializer.serialize(mock, payload) != ESP_OK) {
        return 1;
    }
    const auto data = nlohmann::json::parse(payload, nullptr, false);
    const nlohmann::json expected = {
        {"schemaVersion", 1},
        {"recordId", "example-stable-record-id-001"},
        {"deviceId", "ring-01"},
        {"gatewayId", "gateway-01"},
        {"userId", "user-01"},
        {"kind", "heartRate"},
        {"observedAt", "2026-10-01T12:00:00Z"},
        {"data", {{"bpm", 72}}},
    };
    if (data != expected) {
        return 2;
    }
    payload = "stale";
    if (serializer.serialize(gateway::MockReading{"", "ring-01", "gateway-01", "user-01",
                                                  "2026-10-01T12:00:00Z", 72},
                             payload) != ESP_ERR_INVALID_ARG ||
        !payload.empty()) {
        return 3;
    }
    payload = "stale";
    if (serializer.serialize(gateway::HeartRateReading{}, payload) != ESP_ERR_NOT_SUPPORTED ||
        !payload.empty()) {
        return 4;
    }
    return 0;
}
