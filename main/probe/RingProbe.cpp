#include "probe/RingProbe.h"

#include <algorithm>
#include <chrono>
#include <cstring>

#include "protocol/BigDataProtocol.h"

namespace gateway {
namespace {
using Clock = std::chrono::steady_clock;
constexpr uint32_t kPollMs = 1000;
constexpr uint32_t kCommandQuietMs = 1200;
constexpr uint32_t kCommandHardMs = 6000;
constexpr uint32_t kBigHardMs = 15000;
constexpr uint32_t kLiveMs = 30000;

uint32_t elapsed(Clock::time_point start) {
    return static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count());
}

ProbeResult unavailable(IRingTransport& transport, RingChannel channel) {
    if (!transport.is_connected()) {
        return ProbeResult::skipped_disconnected;
    }
    return transport.has_channel(channel) ? ProbeResult::response
                                          : ProbeResult::unsupported_channel;
}

void note_rx(ProbeObserver& observer, const char* label, const RingNotification& note,
             bool matched) {
    const auto check = note.channel == RingChannel::command
                           ? ColmiProtocol::validate_notification(note.bytes, note.length)
                           : PacketStatus::wrong_length;
    observer.rx(label, note, matched, check);
}

ProbeEntry command(IRingTransport& transport, ProbeObserver& observer, const char* label,
                   const uint8_t (&request)[16], bool multi, bool live = false,
                   uint8_t* first_valid = nullptr, bool hrv_stream = false) {
    ProbeEntry entry{label};
    entry.result = unavailable(transport, RingChannel::command);
    if (entry.result != ProbeResult::response) {
        return entry;
    }
    const auto start = Clock::now();
    observer.tx(label, RingChannel::command, request, sizeof(request));
    if (transport.write(RingChannel::command, request, sizeof(request)) != ESP_OK) {
        entry.result = ProbeResult::transport_error;
        entry.elapsed_ms = elapsed(start);
        return entry;
    }
    const uint8_t command_id = request[0];
    uint32_t waited = 0;
    uint32_t quiet = 0;
    bool got_valid = false;
    bool got_malformed = false;
    bool got_no_data = false;
    bool sent_continue = false;
    uint8_t hr_packets = 0;
    const uint32_t limit = live ? kLiveMs : kCommandHardMs;
    while (waited < limit && elapsed(start) < limit) {
        if (!transport.is_connected()) {
            entry.result = ProbeResult::skipped_disconnected;
            break;
        }
        RingNotification note;
        const uint32_t slice = std::min(kPollMs, limit - waited);
        const auto rc = transport.receive(note, slice);
        if (rc == ESP_ERR_TIMEOUT) {
            waited += slice;
            quiet += slice;
            if (!live && ((got_valid && (!multi || quiet >= kCommandQuietMs)) ||
                          (got_malformed && quiet >= kCommandQuietMs))) {
                break;
            }
            if (live && !sent_continue && !got_valid && waited >= 5000) {
                uint8_t continuation[16]{};
                ColmiProtocol::live_continue(request[1], continuation);
                observer.tx(label, RingChannel::command, continuation, sizeof(continuation));
                if (transport.write(RingChannel::command, continuation, sizeof(continuation)) !=
                    ESP_OK) {
                    entry.result = ProbeResult::transport_error;
                    break;
                }
                sent_continue = true;
            }
            continue;
        }
        if (rc != ESP_OK) {
            entry.result = transport.is_connected() ? ProbeResult::transport_error
                                                    : ProbeResult::skipped_disconnected;
            break;
        }
        const bool same =
            note.channel == RingChannel::command && note.length > 0 &&
            note.bytes[0] == command_id &&
            (command_id != 0x39 ||
             (note.length > 1 && (hrv_stream || note.bytes[1] == request[1] ||
                                  note.bytes[1] == 0xff)));
        const auto check = ColmiProtocol::validate_notification(note.bytes, note.length);
        note_rx(observer, label, note, same);
        if (!same) {
            continue;
        }
        quiet = 0;
        if (check == PacketStatus::valid) {
            if (first_valid && !got_valid) {
                std::memcpy(first_valid, note.bytes, 16);
            }
            ++entry.packets;
            entry.bytes += note.length;
            got_valid = true;
            if (!live && note.bytes[1] == 0xff &&
                (command_id == 0x43 || command_id == 0x15 || command_id == 0x39)) {
                got_no_data = command_id != 0x39 || entry.packets == 1;
                break;
            }
            if (command_id == 0x15 && note.bytes[1] == 0) {
                hr_packets = note.bytes[2];
            }
            if (multi && command_id == 0x43 && note.bytes[1] != 0xf0 && note.bytes[6] > 0 &&
                note.bytes[5] == note.bytes[6] - 1) {
                break;
            }
            if (multi && command_id == 0x15 && hr_packets > 0 && note.bytes[1] == hr_packets - 1) {
                break;
            }
            if (!multi && !live) {
                break;
            }
        } else {
            got_malformed = true;
        }
    }
    if (live && transport.is_connected()) {
        uint8_t stop[16]{};
        ColmiProtocol::live_stop(request[1], stop);
        observer.tx(label, RingChannel::command, stop, sizeof(stop));
        if (transport.write(RingChannel::command, stop, sizeof(stop)) != ESP_OK) {
            entry.result = ProbeResult::transport_error;
        }
    }
    if (entry.result == ProbeResult::response) {
        entry.result = got_no_data     ? ProbeResult::no_data
                       : got_valid     ? ProbeResult::response
                       : got_malformed ? ProbeResult::malformed
                                       : ProbeResult::timeout;
    }
    entry.elapsed_ms = elapsed(start);
    return entry;
}

ProbeEntry big_data(IRingTransport& transport, ProbeObserver& observer, const char* label,
                    uint8_t id) {
    ProbeEntry entry{label};
    entry.result = unavailable(transport, RingChannel::big_data);
    if (entry.result != ProbeResult::response) {
        return entry;
    }
    uint8_t request[7]{};
    BigDataProtocol::make_read(id, request);
    const auto start = Clock::now();
    observer.tx(label, RingChannel::big_data, request, sizeof(request));
    if (transport.write(RingChannel::big_data, request, sizeof(request)) != ESP_OK) {
        entry.result = ProbeResult::transport_error;
        return entry;
    }
    BigDataLengthTracker tracker;
    uint32_t waited = 0;
    while (waited < kBigHardMs && elapsed(start) < kBigHardMs) {
        if (!transport.is_connected()) {
            entry.result = ProbeResult::skipped_disconnected;
            break;
        }
        RingNotification note;
        const uint32_t slice = std::min(kPollMs, kBigHardMs - waited);
        const auto rc = transport.receive(note, slice);
        if (rc == ESP_ERR_TIMEOUT) {
            waited += slice;
            continue;
        }
        if (rc != ESP_OK) {
            entry.result = transport.is_connected() ? ProbeResult::transport_error
                                                    : ProbeResult::skipped_disconnected;
            break;
        }
        const bool same = note.channel == RingChannel::big_data &&
                          (entry.packets > 0 || (note.length > 0 && note.bytes[0] == 0xbc &&
                                                 (note.length == 1 || note.bytes[1] == id)));
        note_rx(observer, label, note, same);
        if (!same) {
            continue;
        }
        ++entry.packets;
        entry.bytes += note.length;
        const auto state = tracker.push(note.bytes, note.length);
        if (state == BigDataStatus::malformed ||
            (tracker.bytes_seen() >= 6 && tracker.data_id() != id)) {
            entry.result = ProbeResult::malformed;
            break;
        }
        if (state == BigDataStatus::complete) {
            entry.result =
                tracker.declared_length() == 0 ? ProbeResult::no_data : ProbeResult::response;
            break;
        }
    }
    if (entry.result == ProbeResult::response && entry.packets == 0) {
        entry.result = ProbeResult::timeout;
    }
    if (entry.result == ProbeResult::response &&
        tracker.bytes_seen() < 6 + tracker.declared_length()) {
        entry.result = ProbeResult::timeout;
    }
    entry.elapsed_ms = elapsed(start);
    return entry;
}

ProbeEntry device_info(IRingTransport& transport, ProbeObserver& observer) {
    ProbeEntry entry{"device_info"};
    if (!transport.is_connected()) {
        entry.result = ProbeResult::skipped_disconnected;
        return entry;
    }
    const auto start = Clock::now();
    constexpr uint16_t uuids[] = {0x2a24, 0x2a25, 0x2a26, 0x2a27, 0x2a29};
    for (const auto uuid : uuids) {
        if (!transport.is_connected()) {
            entry.result = ProbeResult::skipped_disconnected;
            break;
        }
        uint8_t bytes[256]{};
        size_t length = 0;
        const auto rc = transport.read_device_info(uuid, bytes, sizeof(bytes), length);
        observer.device_info(uuid, rc, bytes, rc == ESP_OK ? length : 0);
        if (rc == ESP_OK) {
            ++entry.packets;
            entry.bytes += length;
        } else if (rc != ESP_ERR_NOT_FOUND) {
            entry.result = ProbeResult::transport_error;
        }
    }
    if (entry.result == ProbeResult::timeout) {
        entry.result = entry.packets ? ProbeResult::response : ProbeResult::no_data;
    }
    entry.elapsed_ms = elapsed(start);
    return entry;
}

} // namespace

const char* probe_result_name(ProbeResult result) {
    switch (result) {
    case ProbeResult::response:
        return "RESPONSE";
    case ProbeResult::no_data:
        return "NO_DATA";
    case ProbeResult::timeout:
        return "TIMEOUT";
    case ProbeResult::unsupported_channel:
        return "UNSUPPORTED_CHANNEL";
    case ProbeResult::transport_error:
        return "TRANSPORT_ERROR";
    case ProbeResult::skipped_no_date:
        return "SKIPPED_NO_DATE";
    case ProbeResult::skipped_disconnected:
        return "SKIPPED_DISCONNECTED";
    case ProbeResult::malformed:
        return "MALFORMED";
    }
    return "UNKNOWN";
}

ProbeEntry RingProbe::set_time(IRingTransport& transport, uint32_t utc_epoch,
                               ProbeObserver& observer) {
    uint8_t request[16]{};
    ProbeEntry entry{"set_time"};
    if (ColmiProtocol::set_time(utc_epoch, request) == ESP_OK) {
        entry = command(transport, observer, "set_time", request, false);
    } else {
        entry.result = ProbeResult::malformed;
    }
    observer.result(entry);
    return entry;
}

ProbeSummary RingProbe::run(IRingTransport& transport, uint32_t today_midnight_epoch, bool has_date,
                            ProbeObserver& observer) {
    ProbeSummary summary;
    const char* labels[] = {"battery",         "device_info",  "hr_settings",  "steps_today",
                            "steps_yesterday", "hr_today",     "hr_yesterday", "hrv",
                            "sleep",           "spo2_history", "live_hr",      "live_spo2"};
    for (size_t i = 0; i < summary.entries.size(); ++i) {
        summary.entries[i].label = labels[i];
    }
    auto record = [&](size_t index, ProbeEntry entry) {
        summary.entries[index] = entry;
        observer.result(entry);
    };
    for (size_t i = 0; i < summary.entries.size(); ++i) {
        if (!transport.is_connected()) {
            summary.entries[i].result = ProbeResult::skipped_disconnected;
            observer.result(summary.entries[i]);
            continue;
        }
        uint8_t request[16]{};
        switch (i) {
        case 0:
            ColmiProtocol::battery(request);
            record(i, command(transport, observer, labels[i], request, false));
            break;
        case 1:
            record(i, device_info(transport, observer));
            break;
        case 2:
            ColmiProtocol::hr_settings(request);
            record(i, command(transport, observer, labels[i], request, false));
            break;
        case 3:
        case 4:
            ColmiProtocol::steps(static_cast<uint8_t>(i - 3), request);
            record(i, command(transport, observer, labels[i], request, true));
            break;
        case 5:
        case 6:
            if (!has_date || (i == 6 && today_midnight_epoch < 86400)) {
                summary.entries[i].result = ProbeResult::skipped_no_date;
                observer.result(summary.entries[i]);
            } else {
                ColmiProtocol::hr_history(today_midnight_epoch - (i == 6 ? 86400 : 0), request);
                record(i, command(transport, observer, labels[i], request, true));
            }
            break;
        case 7:
            ColmiProtocol::hrv_page(0, request);
            {
                uint8_t header[16]{};
                auto hrv = command(transport, observer, labels[i], request, false, false, header);
                if (hrv.result == ProbeResult::response) {
                    const uint8_t pages = header[2];
                    if (header[1] == 0xff || pages == 0) {
                        hrv.result = ProbeResult::no_data;
                    } else if (pages > 64) {
                        hrv.result = ProbeResult::malformed;
                    } else {
                        for (uint8_t page = 1; page <= pages; ++page) {
                            ColmiProtocol::hrv_page(page, request);
                            // The last HRV request may stream several records, each with its
                            // own page indices, before the 0xff end marker.
                            auto part = command(transport, observer, labels[i], request,
                                                page == pages, false, nullptr, page == pages);
                            hrv.packets += part.packets;
                            hrv.bytes += part.bytes;
                            hrv.elapsed_ms += part.elapsed_ms;
                            if (part.result == ProbeResult::no_data && hrv.packets > part.packets) {
                                break;
                            }
                            if (part.result != ProbeResult::response) {
                                hrv.result = part.result;
                                break;
                            }
                        }
                    }
                }
                record(i, hrv);
            }
            break;
        case 8:
        case 9:
            record(i, big_data(transport, observer, labels[i], i == 8 ? 0x27 : 0x2a));
            break;
        case 10:
        case 11:
            ColmiProtocol::live_start(i == 10 ? 1 : 3, request);
            record(i, command(transport, observer, labels[i], request, true, true));
            break;
        }
    }
    summary.lost_notifications = transport.lost_notifications();
    observer.finished(summary);
    return summary;
}

} // namespace gateway
