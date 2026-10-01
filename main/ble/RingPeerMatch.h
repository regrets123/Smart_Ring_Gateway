#pragma once

#include <cstddef>
#include <cstdint>

namespace gateway {

class RingPeerMatch {
public:
    static bool matches(const uint8_t* advertisement, size_t length,
                        const char* exact_name, const char* optional_address,
                        const uint8_t (&address_le)[6]);
};

}  // namespace gateway
