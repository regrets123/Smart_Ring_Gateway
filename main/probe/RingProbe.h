#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "ble/RingTransport.h"
#include "protocol/ColmiProtocol.h"

namespace gateway {

enum class ProbeResult {
    response, no_data, timeout, unsupported_channel, transport_error,
    skipped_no_date, skipped_disconnected, malformed
};

struct ProbeEntry {
    const char* label;
    ProbeResult result = ProbeResult::timeout;
    uint32_t packets = 0;
    uint32_t elapsed_ms = 0;
    size_t bytes = 0;
};

struct ProbeSummary {
    std::array<ProbeEntry, 12> entries{};
    uint32_t lost_notifications = 0;
};

class ProbeObserver {
public:
    virtual ~ProbeObserver() = default;
    virtual void tx(const char* label, RingChannel channel,
                    const uint8_t* bytes, size_t length) = 0;
    virtual void rx(const char* label, const RingNotification& notification,
                    bool matched, PacketStatus checksum) = 0;
    virtual void device_info(uint16_t uuid, esp_err_t status,
                             const uint8_t* bytes, size_t length) = 0;
    virtual void result(const ProbeEntry& entry) = 0;
    virtual void finished(const ProbeSummary& summary) = 0;
};

class RingProbe {
public:
    ProbeEntry set_time(IRingTransport& transport, uint32_t utc_epoch,
                        ProbeObserver& observer);
    ProbeSummary run(IRingTransport& transport, uint32_t today_midnight_epoch,
                     bool has_date, ProbeObserver& observer);
};

const char* probe_result_name(ProbeResult result);

}  // namespace gateway
