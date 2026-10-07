#pragma once

#include <cstddef>
#include <cstdint>

#include "esp_err.h"

namespace gateway {

enum class PacketStatus { valid, wrong_length, bad_checksum };

class ColmiProtocol {
public:
    static esp_err_t make_command(uint8_t command, const uint8_t* payload,
                                  size_t payload_length, uint8_t (&out)[16]);
    static PacketStatus validate_notification(const uint8_t* bytes, size_t length);
    static esp_err_t battery(uint8_t (&out)[16]);
    static esp_err_t set_time(uint32_t utc_epoch, uint8_t (&out)[16]);
    static esp_err_t hr_settings(uint8_t (&out)[16]);
    static esp_err_t steps(uint8_t day_offset, uint8_t (&out)[16]);
    static esp_err_t hr_history(uint32_t midnight_epoch, uint8_t (&out)[16]);
    static esp_err_t hrv_page(uint8_t page_index, uint8_t (&out)[16]);
    static esp_err_t live_start(uint8_t kind, uint8_t (&out)[16]);
    static esp_err_t live_continue(uint8_t kind, uint8_t (&out)[16]);
    static esp_err_t live_stop(uint8_t kind, uint8_t (&out)[16]);
};

}  // namespace gateway
