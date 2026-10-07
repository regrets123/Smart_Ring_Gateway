#include "ble/RingPeerMatch.h"

#include <cstring>

namespace gateway {
namespace {

int hex_digit(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

bool address_matches(const char* text, const uint8_t (&address_le)[6]) {
    if (!text || !*text) {
        return true;
    }
    if (std::strlen(text) != 17) {
        return false;
    }
    for (size_t i = 0; i < 6; ++i) {
        const size_t offset = i * 3;
        const int hi = hex_digit(text[offset]);
        const int lo = hex_digit(text[offset + 1]);
        if (hi < 0 || lo < 0 || (i < 5 && text[offset + 2] != ':') ||
            static_cast<uint8_t>((hi << 4) | lo) != address_le[5 - i]) {
            return false;
        }
    }
    return true;
}

} // namespace

bool RingPeerMatch::matches(const uint8_t* advertisement, size_t length, const char* exact_name,
                            const char* optional_address, const uint8_t (&address_le)[6]) {
    if (!advertisement || !exact_name || !*exact_name ||
        !address_matches(optional_address, address_le)) {
        return false;
    }
    const size_t expected_length = std::strlen(exact_name);
    for (size_t pos = 0; pos < length;) {
        const size_t field_length = advertisement[pos];
        if (!field_length) {
            break;
        }
        if (field_length > length - pos - 1) {
            return false;
        }
        if (field_length >= 1 && advertisement[pos + 1] == 0x09 &&
            field_length - 1 == expected_length &&
            std::memcmp(advertisement + pos + 2, exact_name, expected_length) == 0) {
            return true;
        }
        pos += field_length + 1;
    }
    return false;
}

} // namespace gateway
