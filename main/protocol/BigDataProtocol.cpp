#include "protocol/BigDataProtocol.h"

#include <cstring>

namespace gateway {

esp_err_t BigDataProtocol::make_read(uint8_t data_id, uint8_t (&out)[7]) {
    const uint8_t request[] = {0xbc, data_id, 1, 0, 0xff, 0, 0xff};
    std::memcpy(out, request, sizeof(request));
    return ESP_OK;
}

BigDataStatus BigDataLengthTracker::push(const uint8_t* bytes, size_t length) {
    if (status_ != BigDataStatus::incomplete) return status_;
    if (!bytes && length) return status_ = BigDataStatus::malformed;
    for (size_t i = 0; i < length; ++i) {
        if (bytes_seen_ < sizeof(header_)) header_[bytes_seen_] = bytes[i];
        ++bytes_seen_;
        if (bytes_seen_ == sizeof(header_)) {
            declared_length_ = static_cast<size_t>(header_[2]) |
                               (static_cast<size_t>(header_[3]) << 8);
            if (header_[0] != 0xbc || declared_length_ > 32768)
                return status_ = BigDataStatus::malformed;
        }
        if (bytes_seen_ >= sizeof(header_) &&
            bytes_seen_ > sizeof(header_) + declared_length_)
            return status_ = BigDataStatus::malformed;
    }
    if (bytes_seen_ >= sizeof(header_) &&
        bytes_seen_ == sizeof(header_) + declared_length_)
        status_ = BigDataStatus::complete;
    return status_;
}

}  // namespace gateway
