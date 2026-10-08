#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "protocol/ColmiProtocol.h"
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

void append_hex(const char* hex, std::vector<uint8_t>& bytes) {
    for (size_t i = 0; hex[i]; i += 2) {
        bytes.push_back(static_cast<uint8_t>((nibble(hex[i]) << 4) | nibble(hex[i + 1])));
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

int live_readings() {
    uint8_t packet[16]{};
    const uint8_t hr_payload[] = {1, 0, 79};
    gateway::ColmiProtocol::make_command(0x69, hr_payload, sizeof(hr_payload), packet);
    gateway::HeartRateParser hr_parser;
    gateway::Spo2Parser spo2_parser;
    gateway::HeartRateReading hr;
    gateway::Spo2Reading spo2{};
    if (hr_parser.parse(packet, sizeof(packet), hr) != ESP_OK || hr.bpm != 79 ||
        spo2_parser.parse(packet, sizeof(packet), spo2) != ESP_ERR_INVALID_RESPONSE ||
        spo2.o2Perc != 0) {
        return 9;
    }
    const uint8_t spo2_payload[] = {3, 0, 98};
    gateway::ColmiProtocol::make_command(0x69, spo2_payload, sizeof(spo2_payload), packet);
    if (spo2_parser.parse(packet, sizeof(packet), spo2) != ESP_OK || spo2.o2Perc != 98 ||
        hr_parser.parse(packet, sizeof(packet), hr) != ESP_ERR_INVALID_RESPONSE || hr.bpm != 0) {
        return 10;
    }
    const uint8_t empty_payload[] = {3, 0, 0};
    gateway::ColmiProtocol::make_command(0x69, empty_payload, sizeof(empty_payload), packet);
    if (spo2_parser.parse(packet, sizeof(packet), spo2) != ESP_ERR_NOT_FOUND || spo2.o2Perc != 0) {
        return 11;
    }
    packet[15] ^= 1;
    if (spo2_parser.parse(packet, sizeof(packet), spo2) != ESP_ERR_INVALID_RESPONSE) {
        return 12;
    }
    return 0;
}

int captured_spo2_history() {
    // M7083 capture: three 49-byte day records in one 147-byte payload.
    const char* fragments[] = {
        "bc2a9300562b0263636363636363636363636363",
        "6363636363636363636363636300000000000000",
        "0000000000000000000000000000000100000000",
        "0000000000000000000000000000000063636363",
        "6363636363636363636363636363636363636363",
        "6363636300636363636363636363636363636363",
        "6363636363636363630000000000000000000000",
        "00000000000000000000000000",
    };
    std::vector<uint8_t> frame;
    for (const char* fragment : fragments) {
        append_hex(fragment, frame);
    }
    gateway::Spo2HistoryParser parser;
    gateway::Spo2HistoryRecord record;
    if (frame.size() != 153 || parser.parse(frame.data(), frame.size(), record) != ESP_OK ||
        record.days.size() != 3 || record.days[0].days_ago != 2 ||
        record.days[1].days_ago != 1 || record.days[2].days_ago != 0 ||
        record.days[0].samples.size() != 13 || record.days[1].samples.size() != 14 ||
        record.days[2].samples.size() != 12 || record.days[0].samples[0].slot != 0 ||
        record.days[0].samples[0].min != 99 || record.days[0].samples[0].max != 99) {
        return 13;
    }
    gateway::Spo2HistoryRecord one;
    one.days.push_back(record.days[0]);
    gateway::RingJson serializer;
    std::string output;
    if (serializer.serialize(one, output) != ESP_OK) return 14;
    const auto json = nlohmann::json::parse(output);
    if (json["days_ago"] != 2 || json["samples"].size() != 13 ||
        json["samples"][0]["slot"] != 0 || json["samples"][0]["min"] != 99 ||
        json["samples"][0]["max"] != 99) {
        return 15;
    }
    frame[2] = 148;
    frame.push_back(0);
    if (parser.parse(frame.data(), frame.size(), record) != ESP_ERR_INVALID_RESPONSE) return 16;
    return 0;
}

int captured_sleep_history() {
    const char* fragments[] = {
        "bc276700ca5b0202365805a601020b0312040a05",
        "0402220311040d0220040b02010502021d031804",
        "1b02310316041c032b02320508020b0503021f04",
        "0c0204002cfd0411010225031604110224040c02",
        "20031d04160233030e0506021305040208050b02",
        "220413031e040d0214",
    };
    std::vector<uint8_t> frame;
    for (const char* fragment : fragments) append_hex(fragment, frame);
    gateway::SleepParser parser;
    gateway::SleepRecord record;
    if (frame.size() != 109 || parser.parse(frame.data(), frame.size(), record) != ESP_OK ||
        record.nights.size() != 2 || record.nights[0].days_ago != 2 ||
        record.nights[0].start_min != 1368 || record.nights[0].end_min != 422 ||
        record.nights[0].stages.size() != 25) return 17;
    int total = 0;
    int rem = 0;
    for (const auto& span : record.nights[0].stages) {
        total += span.duration_min;
        if (span.stage == gateway::SleepStage::rem) rem += span.duration_min;
    }
    if (total != 494 || rem != 101) return 18;
    gateway::SleepRecord one;
    one.nights.push_back(record.nights[0]);
    gateway::RingJson serializer;
    std::string output;
    if (serializer.serialize(one, output) != ESP_OK) return 19;
    const auto json = nlohmann::json::parse(output);
    if (json["nights"][0]["start_time"] != "22:48" ||
        json["nights"][0]["end_time"] != "07:02" ||
        json["nights"][0].contains("start_min") ||
        json["nights"][0].contains("end_min") ||
        json["nights"][0]["stages"].size() != 25 ||
        json["nights"][0]["stages"][2]["stage"] != "rem" ||
        json["nights"][0]["stages"][2]["duration_min"] != 10) return 20;
    gateway::SleepRecord negative_start;
    gateway::SleepNight previous_evening;
    previous_evening.days_ago = 1;
    previous_evening.start_min = -60;
    previous_evening.end_min = 420;
    previous_evening.stages.push_back({gateway::SleepStage::light, 60});
    negative_start.nights.push_back(previous_evening);
    if (serializer.serialize(negative_start, output) != ESP_OK) return 21;
    const auto negative_json = nlohmann::json::parse(output);
    if (negative_json["nights"][0]["start_time"] != "23:00" ||
        negative_json["nights"][0]["end_time"] != "07:00") return 22;
    return 0;
}

} // namespace

int main() {
    if (const int result = captured_history()) {
        return result;
    }
    if (const int result = invalid_history()) {
        return result;
    }
    if (const int result = captured_spo2_history()) {
        return result;
    }
    if (const int result = captured_sleep_history()) {
        return result;
    }
    return live_readings();
}
