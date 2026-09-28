#pragma once
#include <stdint.h>
#include <time.h>

// One rule for RTC initialization, collection, statistics and HJ212.
// Only the legacy clock-input entry accepts a 12-digit timestamp.
namespace CalendarClock {
constexpr bool isValidTimestamp(uint64_t timestamp) {
    const unsigned second = timestamp % 100;
    const unsigned minute = (timestamp / 100) % 100;
    const unsigned hour = (timestamp / 10000) % 100;
    const unsigned day = (timestamp / 1000000) % 100;
    const unsigned month = (timestamp / 100000000) % 100;
    const unsigned year = timestamp / 10000000000ULL;
    if (timestamp < 20200101000000ULL || timestamp > 20991231235959ULL ||
        month < 1 || month > 12 || day < 1 || hour > 23 ||
        minute > 59 || second > 59) return false;
    const bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
    const unsigned days = month == 2 ? (leap ? 29 : 28) :
        (month == 4 || month == 6 || month == 9 || month == 11 ? 30 : 31);
    return day <= days;
}

constexpr bool isValidClockInput(uint64_t timestamp) {
    return isValidTimestamp(timestamp < 1000000000000ULL
        ? timestamp * 100ULL : timestamp);
}

inline uint64_t currentTimestamp() {
    time_t now = 0;
    struct tm info = {};
    time(&now);
    if (localtime_r(&now, &info) == nullptr ||
        info.tm_year + 1900 < 2020 || info.tm_year + 1900 > 2099) return 0;
    const uint64_t timestamp =
        static_cast<uint64_t>(info.tm_year + 1900) * 10000000000ULL +
        static_cast<uint64_t>(info.tm_mon + 1) * 100000000ULL +
        static_cast<uint64_t>(info.tm_mday) * 1000000ULL +
        static_cast<uint64_t>(info.tm_hour) * 10000ULL +
        static_cast<uint64_t>(info.tm_min) * 100ULL + info.tm_sec;
    return isValidTimestamp(timestamp) ? timestamp : 0;
}
}
