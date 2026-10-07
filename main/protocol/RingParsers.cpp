#include "protocol/RingParsers.h"
#include "protocol/BigDataProtocol.h"
#include "protocol/ColmiProtocol.h"

#include <utility>

namespace gateway
{
    esp_err_t HeartRateParser::parse(const uint8_t *bytes, size_t length, HeartRateReading &reading)
    {
        reading.bpm = 0;
        if (ColmiProtocol::validate_notification(bytes, length) != PacketStatus::valid ||
            !ColmiProtocol::is_live_measurement_command(bytes[0]) ||
            ColmiProtocol::is_kind(bytes[1]) != LiveMeasurementKind::heart_rate ||
            !ColmiProtocol::is_live_response_state(bytes[2]))
        {
            return ESP_ERR_INVALID_RESPONSE;
        }

        // M7083 live HR packets put BPM in byte 3; zero means no reading yet.
        if (bytes[3] == 0)
        {
            return ESP_ERR_NOT_FOUND;
        }
        reading.bpm = bytes[3];
        return ESP_OK;
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

    esp_err_t Spo2Parser::parse(const uint8_t *bytes, size_t length, Spo2Reading &reading)
    {
        reading.o2Perc = 0;
        if (ColmiProtocol::validate_notification(bytes, length) != PacketStatus::valid ||
            !ColmiProtocol::is_live_measurement_command(bytes[0]) ||
            ColmiProtocol::is_kind(bytes[1]) != LiveMeasurementKind::spo2 ||
            !ColmiProtocol::is_live_response_state(bytes[2]))
        {
            return ESP_ERR_INVALID_RESPONSE;
        }
        if (bytes[3] == 0)
        {
            return ESP_ERR_NOT_FOUND;
        }
        reading.o2Perc = bytes[3];
        return ESP_OK;
    }

    esp_err_t Spo2HistoryParser::parse(const uint8_t *bytes, size_t length,
                                       Spo2HistoryRecord &record)
    {
        record.unknown = 0;
        record.days_ago = 0;
        record.samples.clear();
        record.raw_payload.clear();

        const esp_err_t status = BigDataProtocol::validate_frame(bytes, length, 0x2a);
        if (status != ESP_OK) {
            return status;
        }

        record.raw_payload.assign(bytes + 6, bytes + length);
        const auto &payload = record.raw_payload;
        if (payload.size() < 2 || (payload.size() - 2) % 2 != 0) {
            return ESP_ERR_INVALID_RESPONSE;
        }

        record.unknown = payload[0];
        record.days_ago = payload[1];
        for (size_t at = 2; at < payload.size(); at += 2) {
            record.samples.push_back({payload[at], payload[at + 1]});
        }
        return record.samples.empty() ? ESP_ERR_NOT_FOUND : ESP_OK;
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
