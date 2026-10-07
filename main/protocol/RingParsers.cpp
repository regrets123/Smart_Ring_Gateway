#include "protocol/RingParsers.h"
#include "protocol/ColmiProtocol.h"

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
    esp_err_t SleepParser::parse(const uint8_t *, size_t, SleepRecord &) { return ESP_ERR_NOT_SUPPORTED; }
    esp_err_t StepsParser::parse(const uint8_t *, size_t, StepsReading &)
    {
        return ESP_ERR_NOT_SUPPORTED;
    }

} // namespace gateway
