#include "probe/ProbeDate.h"

#include <cstring>

namespace gateway {
namespace {
bool leap(int year) { return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0); }

int parse_digits(const char* text, int count) {
    int value = 0;
    for (int i = 0; i < count; ++i) {
        if (text[i] < '0' || text[i] > '9') {
            return -1;
        }
        value = value * 10 + text[i] - '0';
    }
    return value;
}
} // namespace

esp_err_t ProbeDate::resolve(const char* configured_yyyy_mm_dd, uint32_t synchronized_epoch,
                             bool clock_valid, uint32_t& utc_midnight_epoch) {
    if (!configured_yyyy_mm_dd) {
        return ESP_ERR_INVALID_ARG;
    }
    if (*configured_yyyy_mm_dd == '\0') {
        if (!clock_valid) {
            return ESP_ERR_NOT_FOUND;
        }
        utc_midnight_epoch = synchronized_epoch - synchronized_epoch % 86400u;
        return ESP_OK;
    }
    if (std::strlen(configured_yyyy_mm_dd) != 10 || configured_yyyy_mm_dd[4] != '-' ||
        configured_yyyy_mm_dd[7] != '-') {
        return ESP_ERR_INVALID_ARG;
    }
    const int year = parse_digits(configured_yyyy_mm_dd, 4);
    const int month = parse_digits(configured_yyyy_mm_dd + 5, 2);
    const int day = parse_digits(configured_yyyy_mm_dd + 8, 2);
    if (year < 1970 || year > 2106 || month < 1 || month > 12) {
        return ESP_ERR_INVALID_ARG;
    }
    const int month_days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    const int max_day = month_days[month - 1] + (month == 2 && leap(year) ? 1 : 0);
    if (day < 1 || day > max_day) {
        return ESP_ERR_INVALID_ARG;
    }
    uint64_t days = 0;
    for (int y = 1970; y < year; ++y) {
        days += leap(y) ? 366 : 365;
    }
    for (int m = 1; m < month; ++m) {
        days += month_days[m - 1] + (m == 2 && leap(year) ? 1 : 0);
    }
    days += static_cast<uint64_t>(day - 1);
    const uint64_t seconds = days * 86400u;
    if (seconds > UINT32_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    utc_midnight_epoch = static_cast<uint32_t>(seconds);
    return ESP_OK;
}

} // namespace gateway
