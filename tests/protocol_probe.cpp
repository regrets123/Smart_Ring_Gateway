#include <cstdint>
#include <cstring>

#include "protocol/BigDataProtocol.h"
#include "protocol/ColmiProtocol.h"

namespace {

bool equals(const uint8_t* actual, const uint8_t* expected, size_t length) {
    return std::memcmp(actual, expected, length) == 0;
}

int test_command_packets() {
    uint8_t packet[16]{};
    const uint8_t battery[16] = {0x03, 0, 0, 0, 0, 0, 0, 0,
                                 0, 0, 0, 0, 0, 0, 0, 0x03};
    if (gateway::ColmiProtocol::battery(packet) != ESP_OK || !equals(packet, battery, 16)) return 1;

    const uint8_t settings[16] = {0x16, 0x01, 0, 0, 0, 0, 0, 0,
                                  0, 0, 0, 0, 0, 0, 0, 0x17};
    if (gateway::ColmiProtocol::hr_settings(packet) != ESP_OK ||
        !equals(packet, settings, 16)) return 2;

    const uint8_t steps[16] = {0x43, 0, 0x0f, 0, 0x5f, 0x01, 0, 0,
                               0, 0, 0, 0, 0, 0, 0, 0xb2};
    if (gateway::ColmiProtocol::steps(0, packet) != ESP_OK || !equals(packet, steps, 16)) return 3;

    const uint8_t overflow[16] = {0x43, 0xff, 0x0f, 0, 0x5f, 0x01, 0, 0,
                                  0, 0, 0, 0, 0, 0, 0, 0xb1};
    if (gateway::ColmiProtocol::steps(0xff, packet) != ESP_OK ||
        !equals(packet, overflow, 16)) return 4;

    const uint8_t hr_history[16] = {0x15, 0x78, 0x56, 0x34, 0x12, 0, 0, 0,
                                    0, 0, 0, 0, 0, 0, 0, 0x29};
    if (gateway::ColmiProtocol::hr_history(0x12345678, packet) != ESP_OK ||
        !equals(packet, hr_history, 16)) return 5;

    const uint8_t hrv[16] = {0x39, 1, 0, 0, 0, 0, 0, 0,
                             0, 0, 0, 0, 0, 0, 0, 0x3a};
    if (gateway::ColmiProtocol::hrv_page(1, packet) != ESP_OK || !equals(packet, hrv, 16)) return 6;

    const uint8_t live_start[16] = {0x69, 3, 1, 0, 0, 0, 0, 0,
                                    0, 0, 0, 0, 0, 0, 0, 0x6d};
    if (gateway::ColmiProtocol::live_start(3, packet) != ESP_OK ||
        !equals(packet, live_start, 16)) return 7;

    const uint8_t live_continue[16] = {0x69, 1, 3, 0, 0, 0, 0, 0,
                                       0, 0, 0, 0, 0, 0, 0, 0x6d};
    if (gateway::ColmiProtocol::live_continue(1, packet) != ESP_OK ||
        !equals(packet, live_continue, 16)) return 8;

    const uint8_t live_stop[16] = {0x6a, 3, 0, 0, 0, 0, 0, 0,
                                   0, 0, 0, 0, 0, 0, 0, 0x6d};
    if (gateway::ColmiProtocol::live_stop(3, packet) != ESP_OK ||
        !equals(packet, live_stop, 16)) return 9;
    return 0;
}

int test_command_validation() {
    const uint8_t battery_response[16] = {0x03, 0x38, 0, 0, 0, 0, 0, 0,
                                           0, 0, 0, 0, 0, 0, 0, 0x3b};
    if (gateway::ColmiProtocol::validate_notification(battery_response, 16) !=
        gateway::PacketStatus::valid) return 10;
    if (gateway::ColmiProtocol::validate_notification(battery_response, 15) !=
        gateway::PacketStatus::wrong_length) return 11;
    uint8_t corrupt[16];
    std::memcpy(corrupt, battery_response, 16);
    corrupt[15] ^= 1;
    if (gateway::ColmiProtocol::validate_notification(corrupt, 16) !=
        gateway::PacketStatus::bad_checksum) return 12;
    if (gateway::ColmiProtocol::make_command(3, nullptr, 15, corrupt) !=
        ESP_ERR_INVALID_ARG) return 13;
    return 0;
}

int test_big_data() {
    uint8_t request[7]{};
    const uint8_t sleep_request[7] = {0xbc, 0x27, 1, 0, 0xff, 0, 0xff};
    if (gateway::BigDataProtocol::make_read(0x27, request) != ESP_OK ||
        !equals(request, sleep_request, 7)) return 20;

    gateway::BigDataLengthTracker tracker;
    const uint8_t first[2] = {0xbc, 0x27};
    const uint8_t second[5] = {3, 0, 0xff, 0xff, 0x11};
    const uint8_t last[2] = {0x22, 0x33};
    if (tracker.push(first, 2) != gateway::BigDataStatus::incomplete) return 21;
    if (tracker.push(second, 5) != gateway::BigDataStatus::incomplete ||
        tracker.declared_length() != 3 || tracker.bytes_seen() != 7) return 22;
    if (tracker.push(last, 2) != gateway::BigDataStatus::complete ||
        tracker.bytes_seen() != 9) return 23;

    gateway::BigDataLengthTracker too_large;
    const uint8_t large_header[6] = {0xbc, 0x2a, 0x01, 0x80, 0, 0};
    if (too_large.push(large_header, 6) != gateway::BigDataStatus::malformed) return 24;
    gateway::BigDataLengthTracker wrong_magic;
    const uint8_t bad_header[6] = {0xbb, 0x2a, 1, 0, 0, 0};
    if (wrong_magic.push(bad_header, 6) != gateway::BigDataStatus::malformed) return 25;
    return 0;
}

}  // namespace

int main() {
    if (const int result = test_command_packets()) return result;
    if (const int result = test_command_validation()) return result;
    return test_big_data();
}
