#include "protocol/ColmiProtocol.h"

namespace gateway {

esp_err_t ColmiProtocol::encode_command(const uint8_t*, size_t, uint8_t*, size_t,
                                         size_t& output_length) {
    output_length = 0;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t ColmiProtocol::decode_notification(const uint8_t*, size_t) {
    return ESP_ERR_NOT_SUPPORTED;
}

}  // namespace gateway
