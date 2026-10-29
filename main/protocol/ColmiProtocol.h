#pragma once

#include <cstddef>
#include <cstdint>

#include "esp_err.h"

namespace gateway {

enum class PacketStatus { valid, wrong_length, bad_checksum };
enum class LiveMeasurementKind { unknown, heart_rate, spo2 };

class ColmiProtocol {
public:
    static esp_err_t make_command(uint8_t command, const uint8_t* payload,
                                  size_t payload_length, uint8_t (&out)[16]);
    static PacketStatus validate_notification(const uint8_t* bytes, size_t length);
    static bool is_live_measurement_command(uint8_t command);
    static LiveMeasurementKind is_kind(uint8_t kind);
    static bool is_live_response_state(uint8_t state);
    static esp_err_t battery(uint8_t (&out)[16]);
    static esp_err_t set_time(uint32_t utc_epoch, uint8_t (&out)[16]);
    static esp_err_t hr_settings(uint8_t (&out)[16]);
    // Byte layout from colmi.puxtril.com/settings: action (1 read, 2 write),
    // isEnabled, interval. That page does not define the enable values or the
    // interval unit; 1 = enabled and minutes are assumed and checked by read-back.
    static esp_err_t hr_settings_write(bool enabled, uint8_t interval_minutes,
                                       uint8_t (&out)[16]);
    static bool parse_hr_settings(const uint8_t* bytes, size_t length, bool& enabled,
                                  uint8_t& interval_minutes);
    static esp_err_t hr_history(uint32_t midnight_epoch, uint8_t (&out)[16]);
    static esp_err_t hrv_page(uint8_t page_index, uint8_t (&out)[16]);
    static esp_err_t live_start(uint8_t kind, uint8_t (&out)[16]);
    static esp_err_t live_continue(uint8_t kind, uint8_t (&out)[16]);
    static esp_err_t live_stop(uint8_t kind, uint8_t (&out)[16]);
};

}  // namespace gateway
