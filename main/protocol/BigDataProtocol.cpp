#include "protocol/BigDataProtocol.h"

#include <cstring>

namespace gateway {

esp_err_t BigDataProtocol::make_read(uint8_t data_id, uint8_t (&out)[7]) {
    const uint8_t request[] = {0xbc, data_id, 1, 0, 0xff, 0, 0xff};
    std::memcpy(out, request, sizeof(request));
    return ESP_OK;
}

esp_err_t BigDataProtocol::validate_frame(const uint8_t* bytes, size_t length,
                                           uint8_t expected_data_id) {
    if (!bytes) {
        return ESP_ERR_INVALID_ARG;
    }

    BigDataLengthTracker frame;
    if (frame.push(bytes, length) != BigDataStatus::complete ||
        frame.data_id() != expected_data_id) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    if (frame.declared_length() == 0) {
        return ESP_ERR_NOT_FOUND;
    }
    // TODO: Once several days of nonempty frames are stored in SQL, confirm
    // CRC-16/Modbus for header bytes 4-5; allow the 0xFFFF empty-data sentinel.
    // Then reject frames whose CRC does not match the payload.
    return ESP_OK;
}

BigDataStatus BigDataLengthTracker::push(const uint8_t* bytes, size_t length) {
    if (status_ != BigDataStatus::incomplete) {
        return status_;
    }
    if (!bytes && length) {
        return status_ = BigDataStatus::malformed;
    }
    for (size_t i = 0; i < length; ++i) {
        if (bytes_seen_ < sizeof(header_)) {
            header_[bytes_seen_] = bytes[i];
        }
        ++bytes_seen_;
        if (bytes_seen_ == sizeof(header_)) {
            declared_length_ =
                static_cast<size_t>(header_[2]) | (static_cast<size_t>(header_[3]) << 8);
            if (header_[0] != 0xbc || declared_length_ > 32768) {
                return status_ = BigDataStatus::malformed;
            }
        }
        if (bytes_seen_ >= sizeof(header_) && bytes_seen_ > sizeof(header_) + declared_length_) {
            return status_ = BigDataStatus::malformed;
        }
    }
    if (bytes_seen_ >= sizeof(header_) && bytes_seen_ == sizeof(header_) + declared_length_) {
        status_ = BigDataStatus::complete;
    }
    return status_;
}

} // namespace gateway
