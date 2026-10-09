#include "serialization/RingJson.h"

#include <cstdio>
#include <utility>

#include <nlohmann/json.hpp>

namespace gateway {
namespace {

std::string clock_time(int16_t minutes) {
    const int minute_of_day = (static_cast<int>(minutes) % 1440 + 1440) % 1440;
    char formatted[6]{};
    std::snprintf(formatted, sizeof(formatted), "%02d:%02d",
                  minute_of_day / 60, minute_of_day % 60);
    return formatted;
}

} // namespace

esp_err_t RingJson::serialize(const HeartRateReading& reading, std::string& json) {
    json.clear();
    if (reading.bpm <= 0 || reading.bpm > 255) {
        return ESP_ERR_INVALID_ARG;
    }

    const nlohmann::json payload = {{"bpm", reading.bpm}};
    json = payload.dump();
    return ESP_OK;
}

esp_err_t RingJson::serialize(const HeartRateHistoryRecord& record, std::string& json) {
    json.clear();
    if (record.samples.empty()) {
        return ESP_ERR_INVALID_ARG;
    }

    const nlohmann::json payload = {{"utc_time", record.utc_time},
                                    {"range", record.range},
                                    {"samples", record.samples}};
    json = payload.dump();
    return ESP_OK;
}

esp_err_t RingJson::serialize(const HrvHistoryRecord& record, std::string& json) {
    json.clear();
    if (record.interval_minutes == 0 || record.samples.empty()) {
        return ESP_ERR_INVALID_ARG;
    }

    nlohmann::json samples = nlohmann::json::array();
    for (const auto& sample : record.samples) {
        samples.push_back({{"days_ago", sample.days_ago},
                           {"slot", sample.slot},
                           {"value_ms", sample.value_ms}});
    }
    nlohmann::json payload = {{"metric", "hrv_composite_ms"},
                              {"interval_minutes", record.interval_minutes},
                              {"samples", std::move(samples)}};
    if (record.probe_midnight_utc != 0) {
        payload["probe_midnight_utc"] = record.probe_midnight_utc;
    }
    json = payload.dump();
    return ESP_OK;
}

esp_err_t RingJson::serialize(const Spo2Reading& reading, std::string& json) {
    json.clear();
    if (reading.o2Perc <= 0 || reading.o2Perc > 100) {
        return ESP_ERR_INVALID_ARG;
    }

    const nlohmann::json payload = {{"o2Perc", reading.o2Perc}};
    json = payload.dump();
    return ESP_OK;
}

esp_err_t RingJson::serialize(const Spo2HistoryRecord& record, std::string& json) {
    json.clear();
    if (record.days.size() != 1 || record.days.front().samples.empty()) {
        return ESP_ERR_INVALID_ARG;
    }

    const auto& day = record.days.front();
    nlohmann::json samples = nlohmann::json::array();
    for (const auto& sample : day.samples) {
        samples.push_back({{"slot", sample.slot}, {"min", sample.min}, {"max", sample.max}});
    }

    const nlohmann::json payload = {{"days_ago", day.days_ago},
                                    {"samples", std::move(samples)}};
    json = payload.dump();
    return ESP_OK;
}

esp_err_t RingJson::serialize(const SleepRecord& record, std::string& json) {
    json.clear();
    if (record.nights.empty()) {
        return ESP_ERR_INVALID_ARG;
    }

    nlohmann::json nights = nlohmann::json::array();
    for (const auto& night : record.nights) {
        nlohmann::json stages = nlohmann::json::array();
        for (const auto& span : night.stages) {
            const char* name = nullptr;
            switch (span.stage) {
            case SleepStage::light:
                name = "light";
                break;
            case SleepStage::deep:
                name = "deep";
                break;
            case SleepStage::rem:
                name = "rem";
                break;
            case SleepStage::awake:
                name = "awake";
                break;
            default:
                json.clear();
                return ESP_ERR_INVALID_ARG;
            }
            stages.push_back({{"stage", name}, {"duration_min", span.duration_min}});
        }
        if (stages.empty()) {
            json.clear();
            return ESP_ERR_INVALID_ARG;
        }
        nights.push_back({{"days_ago", night.days_ago},
                          {"start_time", clock_time(night.start_min)},
                          {"end_time", clock_time(night.end_min)},
                          {"stages", std::move(stages)}});
    }

    json = nlohmann::json{{"nights", std::move(nights)}}.dump();
    return ESP_OK;
}

} // namespace gateway
