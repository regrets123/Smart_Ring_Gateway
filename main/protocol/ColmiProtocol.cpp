#include "protocol/ColmiProtocol.h"

#include <cstring>

namespace gateway {

esp_err_t ColmiProtocol::make_command(uint8_t command, const uint8_t* payload,
                                      size_t payload_length, uint8_t (&out)[16]) {
    if (payload_length > 14 || (payload_length && !payload)) return ESP_ERR_INVALID_ARG;
    std::memset(out, 0, sizeof(out));
    out[0] = command;
    if (payload_length) std::memcpy(out + 1, payload, payload_length);
    unsigned sum = 0;
    for (size_t i = 0; i < 15; ++i) sum += out[i];
    out[15] = static_cast<uint8_t>(sum & 0xff);
    return ESP_OK;
}

PacketStatus ColmiProtocol::validate_notification(const uint8_t* bytes, size_t length) {
    if (!bytes || length != 16) return PacketStatus::wrong_length;
    unsigned sum = 0;
    for (size_t i = 0; i < 15; ++i) sum += bytes[i];
    return bytes[15] == static_cast<uint8_t>(sum & 0xff)
               ? PacketStatus::valid : PacketStatus::bad_checksum;
}

esp_err_t ColmiProtocol::battery(uint8_t (&out)[16]) { return make_command(0x03, nullptr, 0, out); }

esp_err_t ColmiProtocol::hr_settings(uint8_t (&out)[16]) {
    const uint8_t payload[] = {1};
    return make_command(0x16, payload, sizeof(payload), out);
}

esp_err_t ColmiProtocol::steps(uint8_t day_offset, uint8_t (&out)[16]) {
    const uint8_t payload[] = {day_offset, 0x0f, 0, 0x5f, 1};
    return make_command(0x43, payload, sizeof(payload), out);
}

esp_err_t ColmiProtocol::hr_history(uint32_t midnight_epoch, uint8_t (&out)[16]) {
    const uint8_t payload[] = {
        static_cast<uint8_t>(midnight_epoch), static_cast<uint8_t>(midnight_epoch >> 8),
        static_cast<uint8_t>(midnight_epoch >> 16), static_cast<uint8_t>(midnight_epoch >> 24),
    };
    return make_command(0x15, payload, sizeof(payload), out);
}

esp_err_t ColmiProtocol::hrv_page(uint8_t page_index, uint8_t (&out)[16]) {
    return make_command(0x39, &page_index, 1, out);
}

esp_err_t ColmiProtocol::live_start(uint8_t kind, uint8_t (&out)[16]) {
    const uint8_t payload[] = {kind, 1};
    return make_command(0x69, payload, sizeof(payload), out);
}

esp_err_t ColmiProtocol::live_continue(uint8_t kind, uint8_t (&out)[16]) {
    const uint8_t payload[] = {kind, 3};
    return make_command(0x69, payload, sizeof(payload), out);
}

esp_err_t ColmiProtocol::live_stop(uint8_t kind, uint8_t (&out)[16]) {
    const uint8_t payload[] = {kind, 0, 0};
    return make_command(0x6a, payload, sizeof(payload), out);
}

}  // namespace gateway
