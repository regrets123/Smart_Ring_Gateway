#pragma once

#include <cstddef>
#include <cstdint>

#include "esp_err.h"

namespace gateway {

enum class BigDataStatus { incomplete, complete, malformed };

class BigDataProtocol {
public:
    static esp_err_t make_read(uint8_t data_id, uint8_t (&out)[7]);
    // Check a complete frame's marker, length, ID, and nonempty payload; CRC is not checked.
    static esp_err_t validate_frame(const uint8_t* bytes, size_t length,
                                    uint8_t expected_data_id);
};

class BigDataLengthTracker {
public:
    BigDataStatus push(const uint8_t* bytes, size_t length);
    size_t declared_length() const { return declared_length_; }
    size_t bytes_seen() const { return bytes_seen_; }
    uint8_t data_id() const { return header_[1]; }

private:
    uint8_t header_[6]{};
    size_t bytes_seen_ = 0;
    size_t declared_length_ = 0;
    BigDataStatus status_ = BigDataStatus::incomplete;
};

}  // namespace gateway
