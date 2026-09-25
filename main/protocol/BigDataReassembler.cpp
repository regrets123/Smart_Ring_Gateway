#include "protocol/BigDataReassembler.h"

namespace gateway {

esp_err_t BigDataReassembler::push_chunk(const uint8_t*, size_t) {
    return ESP_ERR_NOT_SUPPORTED;
}

void BigDataReassembler::reset() {}

}  // namespace gateway
