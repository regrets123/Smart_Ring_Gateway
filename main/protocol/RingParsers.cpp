#include "protocol/RingParsers.h"
#include "protocol/BigDataProtocol.h"
#include "protocol/ColmiProtocol.h"

#include <utility>

namespace {

esp_err_t parse_live_value(const uint8_t* bytes, size_t length,
                           gateway::LiveMeasurementKind expected_kind, uint8_t& value) {
    value = 0;
    if (gateway::ColmiProtocol::validate_notification(bytes, length) !=
            gateway::PacketStatus::valid ||
        !gateway::ColmiProtocol::is_live_measurement_command(bytes[0]) ||
        gateway::ColmiProtocol::is_kind(bytes[1]) != expected_kind ||
        !gateway::ColmiProtocol::is_live_response_state(bytes[2])) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    value = bytes[3];
    return value == 0 ? ESP_ERR_NOT_FOUND : ESP_OK;
}

} // namespace

namespace gateway
{
    esp_err_t HeartRateParser::parse(const uint8_t *bytes, size_t length, HeartRateReading &reading)
    {
        reading.bpm = 0;
        uint8_t value = 0;
        const auto status = parse_live_value(bytes, length, LiveMeasurementKind::heart_rate, value);
        if (status == ESP_OK) {
            reading.bpm = value;
        }
        return status;
    }

    void HeartRateHistoryParser::reset()
    {
        packet_count_ = 0;
        next_index_ = 0;
        pending_ = {};
    }

    esp_err_t HeartRateHistoryParser::parse(const uint8_t *bytes, size_t length,
                                            HeartRateHistoryRecord &record, bool &complete)
    {
        complete = false;
        record = {};
        if (ColmiProtocol::validate_notification(bytes, length) != PacketStatus::valid ||
            bytes[0] != 0x15) {
            reset();
            return ESP_ERR_INVALID_RESPONSE;
        }

        const uint8_t index = bytes[1];
        if (index == 0xff) {
            reset();
            return ESP_ERR_NOT_FOUND;
        }
        if (index == 0) {
            reset();
            packet_count_ = bytes[2];
            if (packet_count_ < 2) {
                reset();
                return ESP_ERR_INVALID_RESPONSE;
            }
            pending_.range = bytes[3];
            pending_.samples.reserve(9 + static_cast<size_t>(packet_count_ - 2) * 13);
            next_index_ = 1;
            return ESP_OK;
        }
        if (packet_count_ == 0 || index != next_index_ || index >= packet_count_) {
            reset();
            return ESP_ERR_INVALID_RESPONSE;
        }

        if (index == 1) {
            pending_.utc_time = static_cast<uint32_t>(bytes[2]) |
                                (static_cast<uint32_t>(bytes[3]) << 8) |
                                (static_cast<uint32_t>(bytes[4]) << 16) |
                                (static_cast<uint32_t>(bytes[5]) << 24);
            pending_.samples.insert(pending_.samples.end(), bytes + 6, bytes + 15);
        } else {
            pending_.samples.insert(pending_.samples.end(), bytes + 2, bytes + 15);
        }

        if (index == packet_count_ - 1) {
            record = std::move(pending_);
            complete = true;
            reset();
        } else {
            ++next_index_;
        }
        return ESP_OK;
    }

    void HrvHistoryParser::reset()
    {
        page_count_ = 0;
        next_index_ = 0;
        days_ago_ = 0;
        pending_ = {};
    }

    esp_err_t HrvHistoryParser::parse(const uint8_t *bytes, size_t length,
                                     HrvHistoryRecord &record, bool &complete)
    {
        complete = false;
        record = {};
        if (ColmiProtocol::validate_notification(bytes, length) != PacketStatus::valid ||
            bytes[0] != 0x39) {
            reset();
            return ESP_ERR_INVALID_RESPONSE;
        }

        const uint8_t index = bytes[1];
        if (index == 0xff) {
            if (next_index_ != 0) {
                reset();
                return ESP_ERR_INVALID_RESPONSE;
            }
            if (pending_.samples.empty()) {
                reset();
                return ESP_ERR_NOT_FOUND;
            }
            record = std::move(pending_);
            complete = true;
            reset();
            return ESP_OK;
        }
        if (index == 0) {
            if (next_index_ != 0 || bytes[2] < 2 || bytes[2] > 64 || bytes[3] == 0 ||
                (pending_.interval_minutes && pending_.interval_minutes != bytes[3])) {
                reset();
                return ESP_ERR_INVALID_RESPONSE;
            }
            page_count_ = bytes[2];
            pending_.interval_minutes = bytes[3];
            next_index_ = 1;
            return ESP_OK;
        }
        if (next_index_ == 0 || index != next_index_ || index >= page_count_) {
            reset();
            return ESP_ERR_INVALID_RESPONSE;
        }

        if (index == 1) {
            // In the M7083 capture this byte advances 0, 1, 2, 3 across days.
            days_ago_ = bytes[2];
        }
        // Page 1 has 12 values after its day byte; later pages carry 13
        // values starting at byte 2. Slot positions continue across pages.
        const size_t first_byte = index == 1 ? 3 : 2;
        const size_t first_slot = index == 1 ? 0 : 12 + static_cast<size_t>(index - 2) * 13;
        const size_t slots_per_day = 1440 / pending_.interval_minutes;
        for (size_t at = first_byte; at < 15; ++at) {
            const size_t slot = first_slot + at - first_byte;
            if (slot >= slots_per_day) {
                break;
            }
            if (bytes[at] != 0 && bytes[at] != 0xff) {
                pending_.samples.push_back({days_ago_, static_cast<uint16_t>(slot), bytes[at]});
            }
        }
        next_index_ = index + 1 == page_count_ ? 0 : static_cast<uint8_t>(index + 1);
        return ESP_OK;
    }

    esp_err_t Spo2Parser::parse(const uint8_t *bytes, size_t length, Spo2Reading &reading)
    {
        reading.o2Perc = 0;
        uint8_t value = 0;
        const auto status = parse_live_value(bytes, length, LiveMeasurementKind::spo2, value);
        if (status == ESP_OK) {
            reading.o2Perc = value;
        }
        return status;
    }

    esp_err_t Spo2HistoryParser::parse(const uint8_t *bytes, size_t length,
                                       Spo2HistoryRecord &record)
    {
        record.days.clear();
        record.raw_payload.clear();

        const esp_err_t status = BigDataProtocol::validate_frame(bytes, length, 0x2a);
        if (status != ESP_OK) {
            return status;
        }

        record.raw_payload.assign(bytes + 6, bytes + length);
        const auto &payload = record.raw_payload;
        // The M7083 sends concatenated 49-byte day records: daysAgo followed
        // by 24 hourly (max, min) pairs. Zero/zero means no reading.
        constexpr size_t kDayBytes = 1 + 24 * 2;
        if (payload.empty() || payload.size() % kDayBytes != 0) {
            return ESP_ERR_INVALID_RESPONSE;
        }

        for (size_t base = 0; base < payload.size(); base += kDayBytes) {
            Spo2HistoryDay day;
            day.days_ago = payload[base];
            for (uint8_t slot = 0; slot < 24; ++slot) {
                const uint8_t max = payload[base + 1 + 2 * slot];
                const uint8_t min = payload[base + 2 + 2 * slot];
                if (max || min) {
                    day.samples.push_back({slot, min ? min : max, max ? max : min});
                }
            }
            if (!day.samples.empty()) {
                record.days.push_back(std::move(day));
            }
        }
        return record.days.empty() ? ESP_ERR_NOT_FOUND : ESP_OK;
    }

    esp_err_t SleepParser::parse(const uint8_t *bytes, size_t length, SleepRecord &reading)
    {
        reading.nights.clear();
        reading.raw_payload.clear();
        // The caller must provide the complete frame, not one BLE fragment.
        const esp_err_t status = BigDataProtocol::validate_frame(bytes, length, 0x27);
        if (status != ESP_OK) {
            return status;
        }

        // The first six bytes are the Big Data header.
        reading.raw_payload.assign(bytes + 6, bytes + length);
        const auto &payload = reading.raw_payload;
        const size_t sleep_days = payload[0];
        if (sleep_days == 0) {
            return ESP_ERR_NOT_FOUND;
        }

        size_t offset = 1;
        for (size_t day = 0; day < sleep_days; ++day) {
            if (payload.size() - offset < 2) {
                reading.nights.clear();
                return ESP_ERR_INVALID_RESPONSE;
            }

            const uint8_t days_ago = payload[offset];
            const size_t day_bytes = payload[offset + 1];
            offset += 2;
            if (day_bytes > payload.size() - offset ||
                (day_bytes != 0 && (day_bytes < 4 || (day_bytes - 4) % 2 != 0))) {
                reading.nights.clear();
                return ESP_ERR_INVALID_RESPONSE;
            }
            if (day_bytes == 0) {
                continue;
            }

            const size_t day_end = offset + day_bytes;
            const auto read_minutes = [&payload](size_t at) {
                const uint16_t value = static_cast<uint16_t>(payload[at]) |
                                       (static_cast<uint16_t>(payload[at + 1]) << 8);
                return static_cast<int16_t>(value < 0x8000 ? static_cast<int32_t>(value)
                                                           : static_cast<int32_t>(value) - 0x10000);
            };

            SleepNight night;
            night.days_ago = days_ago;
            night.start_min = read_minutes(offset);
            night.end_min = read_minutes(offset + 2);
            for (size_t at = offset + 4; at < day_end; at += 2) {
                const uint8_t stage = payload[at];
                if (stage == static_cast<uint8_t>(SleepStage::light) ||
                    stage == static_cast<uint8_t>(SleepStage::deep) ||
                    stage == static_cast<uint8_t>(SleepStage::awake)) {
                    night.stages.push_back({static_cast<SleepStage>(stage), payload[at + 1]});
                }
            }
            if (!night.stages.empty()) {
                reading.nights.push_back(std::move(night));
            }
            offset = day_end;
        }
        return reading.nights.empty() ? ESP_ERR_NOT_FOUND : ESP_OK;
    }
    esp_err_t StepsParser::parse(const uint8_t *, size_t, StepsReading &)
    {
        return ESP_ERR_NOT_SUPPORTED;
    }

} // namespace gateway
