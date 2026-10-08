#include <cstddef>
#include <cstdint>
#include <string>

#include <nlohmann/json.hpp>

#include "protocol/RingParsers.h"
#include "serialization/RingJson.h"

namespace {

uint8_t nibble(char c) {
    return static_cast<uint8_t>(c <= '9' ? c - '0' : c - 'a' + 10);
}

void decode(const char* hex, uint8_t (&packet)[16]) {
    for (size_t i = 0; i < 16; ++i) {
        packet[i] = static_cast<uint8_t>((nibble(hex[2 * i]) << 4) | nibble(hex[2 * i + 1]));
    }
}

int captured_history() {
    // M7083 capture: four daily records, 30-minute slots, then 0xff terminator.
    const char* packets[] = {
        "3900051e00000000000000000000005c",
        "3901002b002700220021002d002a0026",
        "39022d00310000000000000000000099",
        "3903000000000000000000000000003c",
        "3904000000000000000000000000003d",
        "3900051e00000000000000000000005c",
        "3901010000000000000000000000003b",
        "390200000000000000002d00270024b3",
        "39030030002600210022003000300035",
        "390420002e002e002800250000000006",
        "3900051e00000000000000000000005c",
        "390102250028002d00220029002e002f",
        "39022d0032002a00300029002100306e",
        "3903003100000000000000000000006d",
        "3904000000000000000000000000003d",
        "3900051e00000000000000000000005c",
        "3901030000000000000000000000003d",
        "3902000000000000000000000000003b",
        "3903000000000000001f0031002f00bb",
        "3904290020002a002e001f00000000fd",
        "39ff0000000000000000000000000038",
    };
    gateway::HrvHistoryParser parser;
    gateway::HrvHistoryRecord record;
    bool complete = false;
    for (size_t i = 0; i < sizeof(packets) / sizeof(packets[0]); ++i) {
        uint8_t packet[16]{};
        decode(packets[i], packet);
        if (parser.parse(packet, sizeof(packet), record, complete) != ESP_OK ||
            complete != (i + 1 == sizeof(packets) / sizeof(packets[0]))) {
            return 1;
        }
    }
    if (record.interval_minutes != 30 || record.samples.size() != 44 ||
        record.samples[0].days_ago != 0 || record.samples[0].slot != 0 ||
        record.samples[0].value_ms != 43 || record.samples[7].slot != 14 ||
        record.samples[7].value_ms != 49 || record.samples[8].days_ago != 1 ||
        record.samples[8].slot != 20 || record.samples.back().days_ago != 3 ||
        record.samples.back().slot != 46 || record.samples.back().value_ms != 31) {
        return 2;
    }
    gateway::RingJson serializer;
    std::string output;
    record.probe_midnight_utc = 1791417600u;
    if (serializer.serialize(record, output) != ESP_OK) {
        return 3;
    }
    const auto json = nlohmann::json::parse(output);
    if (json["metric"] != "hrv_composite_ms" || json["interval_minutes"] != 30 ||
        json["probe_midnight_utc"] != 1791417600u ||
        json["samples"].size() != 44 || json["samples"][0]["value_ms"] != 43 ||
        json["samples"].back()["days_ago"] != 3) {
        return 4;
    }
    return 0;
}

int invalid_history() {
    gateway::HrvHistoryParser parser;
    gateway::HrvHistoryRecord record;
    bool complete = false;
    uint8_t packet[16]{};
    decode("39ff0000000000000000000000000038", packet);
    if (parser.parse(packet, sizeof(packet), record, complete) != ESP_ERR_NOT_FOUND || complete) {
        return 5;
    }
    decode("3900051e00000000000000000000005c", packet);
    if (parser.parse(packet, sizeof(packet), record, complete) != ESP_OK) {
        return 6;
    }
    decode("39022d00310000000000000000000099", packet);
    if (parser.parse(packet, sizeof(packet), record, complete) != ESP_ERR_INVALID_RESPONSE) {
        return 7;
    }
    decode("3901002b002700220021002d002a0026", packet);
    if (parser.parse(packet, sizeof(packet), record, complete) != ESP_ERR_INVALID_RESPONSE) {
        return 8;
    }
    return 0;
}

} // namespace

int main() {
    if (const int result = captured_history()) {
        return result;
    }
    return invalid_history();
}
