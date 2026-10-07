#include <cstdint>
#include <cstring>

#include "ble/RingPeerMatch.h"
#include "protocol/BigDataProtocol.h"
#include "protocol/ColmiProtocol.h"
#include "probe/RingProbe.h"
#include "probe/ProbeDate.h"

#include <deque>
#include <vector>

namespace {

bool equals(const uint8_t* actual, const uint8_t* expected, size_t length) {
    return std::memcmp(actual, expected, length) == 0;
}

int test_command_packets() {
    uint8_t packet[16]{};
    const uint8_t battery[16] = {0x03, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x03};
    if (gateway::ColmiProtocol::battery(packet) != ESP_OK || !equals(packet, battery, 16)) {
        return 1;
    }

    const uint8_t settings[16] = {0x16, 0x01, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x17};
    if (gateway::ColmiProtocol::hr_settings(packet) != ESP_OK || !equals(packet, settings, 16)) {
        return 2;
    }

    const uint8_t steps[16] = {0x43, 0, 0x0f, 0, 0x5f, 0x01, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xb2};
    if (gateway::ColmiProtocol::steps(0, packet) != ESP_OK || !equals(packet, steps, 16)) {
        return 3;
    }

    const uint8_t overflow[16] = {0x43, 0xff, 0x0f, 0, 0x5f, 0x01, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xb1};
    if (gateway::ColmiProtocol::steps(0xff, packet) != ESP_OK || !equals(packet, overflow, 16)) {
        return 4;
    }

    const uint8_t hr_history[16] = {0x15, 0x78, 0x56, 0x34, 0x12, 0, 0, 0,
                                    0,    0,    0,    0,    0,    0, 0, 0x29};
    if (gateway::ColmiProtocol::hr_history(0x12345678, packet) != ESP_OK ||
        !equals(packet, hr_history, 16)) {
        return 5;
    }

    const uint8_t hrv[16] = {0x39, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x3a};
    if (gateway::ColmiProtocol::hrv_page(1, packet) != ESP_OK || !equals(packet, hrv, 16)) {
        return 6;
    }

    const uint8_t live_start[16] = {0x69, 3, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x6d};
    if (gateway::ColmiProtocol::live_start(3, packet) != ESP_OK ||
        !equals(packet, live_start, 16)) {
        return 7;
    }

    const uint8_t live_continue[16] = {0x69, 1, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x6d};
    if (gateway::ColmiProtocol::live_continue(1, packet) != ESP_OK ||
        !equals(packet, live_continue, 16)) {
        return 8;
    }

    const uint8_t live_stop[16] = {0x6a, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x6d};
    if (gateway::ColmiProtocol::live_stop(3, packet) != ESP_OK || !equals(packet, live_stop, 16)) {
        return 9;
    }
    return 0;
}

int test_command_validation() {
    const uint8_t battery_response[16] = {0x03, 0x38, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x3b};
    if (gateway::ColmiProtocol::validate_notification(battery_response, 16) !=
        gateway::PacketStatus::valid) {
        return 10;
    }
    if (gateway::ColmiProtocol::validate_notification(battery_response, 15) !=
        gateway::PacketStatus::wrong_length) {
        return 11;
    }
    uint8_t corrupt[16];
    std::memcpy(corrupt, battery_response, 16);
    corrupt[15] ^= 1;
    if (gateway::ColmiProtocol::validate_notification(corrupt, 16) !=
        gateway::PacketStatus::bad_checksum) {
        return 12;
    }
    if (gateway::ColmiProtocol::make_command(3, nullptr, 15, corrupt) != ESP_ERR_INVALID_ARG) {
        return 13;
    }
    return 0;
}

int test_big_data() {
    uint8_t request[7]{};
    const uint8_t sleep_request[7] = {0xbc, 0x27, 1, 0, 0xff, 0, 0xff};
    if (gateway::BigDataProtocol::make_read(0x27, request) != ESP_OK ||
        !equals(request, sleep_request, 7)) {
        return 20;
    }

    gateway::BigDataLengthTracker tracker;
    const uint8_t first[2] = {0xbc, 0x27};
    const uint8_t second[5] = {3, 0, 0xff, 0xff, 0x11};
    const uint8_t last[2] = {0x22, 0x33};
    if (tracker.push(first, 2) != gateway::BigDataStatus::incomplete) {
        return 21;
    }
    if (tracker.push(second, 5) != gateway::BigDataStatus::incomplete ||
        tracker.declared_length() != 3 || tracker.bytes_seen() != 7) {
        return 22;
    }
    if (tracker.push(last, 2) != gateway::BigDataStatus::complete || tracker.bytes_seen() != 9) {
        return 23;
    }

    gateway::BigDataLengthTracker too_large;
    const uint8_t large_header[6] = {0xbc, 0x2a, 0x01, 0x80, 0, 0};
    if (too_large.push(large_header, 6) != gateway::BigDataStatus::malformed) {
        return 24;
    }
    gateway::BigDataLengthTracker wrong_magic;
    const uint8_t bad_header[6] = {0xbb, 0x2a, 1, 0, 0, 0};
    if (wrong_magic.push(bad_header, 6) != gateway::BigDataStatus::malformed) {
        return 25;
    }
    return 0;
}

int test_peer_selection() {
    const uint8_t complete_name[] = {11, 0x09, 'M', '7', '0', '8', '3', '_', '7', '9', '0', '4'};
    const uint8_t short_name[] = {6, 0x08, 'M', '7', '0', '8', '3'};
    const uint8_t other_name[] = {11, 0x09, 'M', '7', '0', '8', '3', '_', '0', '0', '0', '0'};
    const uint8_t address_le[] = {0x04, 0x79, 0x31, 0x45, 0x35, 0x31};
    if (!gateway::RingPeerMatch::matches(complete_name, sizeof(complete_name), "M7083_7904", "",
                                         address_le)) {
        return 30;
    }
    if (gateway::RingPeerMatch::matches(short_name, sizeof(short_name), "M7083_7904", "",
                                        address_le)) {
        return 31;
    }
    if (gateway::RingPeerMatch::matches(other_name, sizeof(other_name), "M7083_7904", "",
                                        address_le)) {
        return 32;
    }
    if (!gateway::RingPeerMatch::matches(complete_name, sizeof(complete_name), "M7083_7904",
                                         "31:35:45:31:79:04", address_le)) {
        return 33;
    }
    if (gateway::RingPeerMatch::matches(complete_name, sizeof(complete_name), "M7083_7904",
                                        "31:35:45:31:79:05", address_le)) {
        return 34;
    }
    if (gateway::RingPeerMatch::matches(complete_name, sizeof(complete_name), "M7083_7904",
                                        "invalid", address_le)) {
        return 35;
    }
    return 0;
}

struct FakeTransport : gateway::IRingTransport {
    bool connected = true;
    bool big_data = true;
    uint32_t lost = 2;
    std::vector<std::vector<uint8_t>> writes;
    std::deque<gateway::RingNotification> pending;
    bool disconnect_on_live = false;
    bool queue_responses = true;
    bool no_data = false;
    bool bad_battery_checksum = false;
    bool malformed_big_data = false;

    bool has_channel(gateway::RingChannel c) const override {
        return connected && (c == gateway::RingChannel::command || big_data);
    }
    esp_err_t write(gateway::RingChannel c, const uint8_t* p, size_t n) override {
        writes.emplace_back(p, p + n);
        if (!queue_responses) {
            return ESP_OK;
        }
        auto enqueue = [&](gateway::RingChannel channel, const uint8_t* b, size_t len) {
            gateway::RingNotification note;
            note.channel = channel;
            note.length = len;
            std::memcpy(note.bytes, b, len);
            pending.push_back(note);
        };
        if (c == gateway::RingChannel::big_data) {
            const uint8_t header[] = {0xbc,
                                      p[1],
                                      static_cast<uint8_t>(malformed_big_data ? 1 : 3),
                                      static_cast<uint8_t>(malformed_big_data ? 0x80 : 0),
                                      0xff,
                                      0xff};
            const uint8_t tail[] = {1, 2, 3};
            enqueue(c, header, 1);
            enqueue(c, header + 1, sizeof(header) - 1);
            enqueue(c, tail, sizeof(tail));
        } else if (p[0] != 0x6a) {
            uint8_t unrelated[16]{};
            gateway::ColmiProtocol::battery(unrelated);
            if (p[0] == 0x16) {
                enqueue(c, unrelated, 16);
            }
            uint8_t packet[16]{};
            if (no_data && (p[0] == 0x43 || p[0] == 0x15 || p[0] == 0x39)) {
                const uint8_t marker[] = {0xff};
                gateway::ColmiProtocol::make_command(p[0], marker, 1, packet);
            } else if (p[0] == 0x39 && p[1] == 0) {
                const uint8_t header_payload[] = {0, 2};
                gateway::ColmiProtocol::make_command(p[0], header_payload, 2, packet);
            } else {
                gateway::ColmiProtocol::make_command(p[0], p + 1, 1, packet);
            }
            if (p[0] == 0x03 && bad_battery_checksum) {
                packet[15] ^= 1;
            }
            enqueue(c, packet, 16);
            if (p[0] == 0x43 || p[0] == 0x15) {
                enqueue(c, packet, 16);
            }
            if (p[0] == 0x69 && disconnect_on_live) {
                connected = false;
            }
        }
        return ESP_OK;
    }
    esp_err_t receive(gateway::RingNotification& n, uint32_t) override {
        if (!connected) {
            return ESP_ERR_INVALID_STATE;
        }
        if (pending.empty()) {
            return ESP_ERR_TIMEOUT;
        }
        n = pending.front();
        pending.pop_front();
        return ESP_OK;
    }
    esp_err_t read_device_info(uint16_t, uint8_t* out, size_t, size_t& n) override {
        out[0] = 'X';
        n = 1;
        return ESP_OK;
    }
    uint32_t lost_notifications() const override { return lost; }
    bool is_connected() const override { return connected; }
    esp_err_t disconnect() override {
        connected = false;
        return ESP_OK;
    }
};

struct FakeObserver : gateway::ProbeObserver {
    int tx_count = 0, rx_count = 0, unmatched = 0, info_count = 0, results = 0;
    void tx(const char*, gateway::RingChannel, const uint8_t*, size_t) override { ++tx_count; }
    void rx(const char*, const gateway::RingNotification&, bool matched,
            gateway::PacketStatus) override {
        ++rx_count;
        if (!matched) {
            ++unmatched;
        }
    }
    void device_info(uint16_t, esp_err_t, const uint8_t*, size_t) override { ++info_count; }
    void result(const gateway::ProbeEntry&) override { ++results; }
    void finished(const gateway::ProbeSummary&) override {}
};

int test_session() {
    FakeTransport transport;
    FakeObserver observer;
    gateway::RingProbe probe;
    const auto summary = probe.run(transport, 0x69000000, true, observer);
    if (summary.entries[0].result != gateway::ProbeResult::response ||
        summary.entries[3].packets != 2 || observer.unmatched < 1) {
        return 40;
    }
    if (summary.entries[8].result != gateway::ProbeResult::response ||
        summary.entries[8].bytes != 9 || summary.lost_notifications != 2 ||
        summary.entries[7].packets != 3) {
        return 41;
    }
    if (observer.info_count != 5 || observer.results != 12) {
        return 42;
    }
    if (transport.writes.size() < 12) {
        return 43;
    }

    FakeTransport missing;
    missing.big_data = false;
    missing.queue_responses = false;
    FakeObserver second;
    const auto partial = probe.run(missing, 0, false, second);
    if (partial.entries[5].result != gateway::ProbeResult::skipped_no_date ||
        partial.entries[8].result != gateway::ProbeResult::unsupported_channel ||
        partial.entries[9].result != gateway::ProbeResult::unsupported_channel ||
        partial.entries[0].result != gateway::ProbeResult::timeout) {
        return 44;
    }

    FakeTransport drop;
    drop.disconnect_on_live = true;
    FakeObserver third;
    const auto lost = probe.run(drop, 0, false, third);
    if (lost.entries[10].result != gateway::ProbeResult::skipped_disconnected ||
        lost.entries[11].result != gateway::ProbeResult::skipped_disconnected) {
        return 45;
    }
    FakeTransport empty;
    empty.no_data = true;
    FakeObserver fourth;
    const auto none = probe.run(empty, 0x69000000, true, fourth);
    if (none.entries[3].result != gateway::ProbeResult::no_data ||
        none.entries[5].result != gateway::ProbeResult::no_data ||
        none.entries[7].result != gateway::ProbeResult::no_data) {
        return 46;
    }
    FakeTransport malformed;
    malformed.bad_battery_checksum = true;
    malformed.malformed_big_data = true;
    FakeObserver fifth;
    const auto bad = probe.run(malformed, 0, false, fifth);
    if (bad.entries[0].result != gateway::ProbeResult::malformed ||
        bad.entries[8].result != gateway::ProbeResult::malformed) {
        return 47;
    }
    return 0;
}

int test_date() {
    uint32_t midnight = 0;
    if (gateway::ProbeDate::resolve("2024-02-29", 0, false, midnight) != ESP_OK ||
        midnight != 1709164800u) {
        return 50;
    }
    if (gateway::ProbeDate::resolve("2023-02-29", 0, false, midnight) != ESP_ERR_INVALID_ARG) {
        return 51;
    }
    if (gateway::ProbeDate::resolve("2024-13-01", 0, false, midnight) != ESP_ERR_INVALID_ARG) {
        return 52;
    }
    if (gateway::ProbeDate::resolve("", 0, false, midnight) != ESP_ERR_NOT_FOUND) {
        return 53;
    }
    if (gateway::ProbeDate::resolve("", 1709251199u, true, midnight) != ESP_OK ||
        midnight != 1709164800u) {
        return 54;
    }
    return 0;
}

} // namespace

int main() {
    if (const int result = test_command_packets()) {
        return result;
    }
    if (const int result = test_command_validation()) {
        return result;
    }
    if (const int result = test_big_data()) {
        return result;
    }
    if (const int result = test_peer_selection()) {
        return result;
    }
    if (const int result = test_session()) {
        return result;
    }
    return test_date();
}
