#pragma once
#include <stdint.h>
#include <string>
#include <algorithm>
#include <cctype>

extern uint64_t testClockMs;
inline uint32_t millis() { return static_cast<uint32_t>(testClockMs); }
using TickType_t = uint32_t;
#define pdMS_TO_TICKS(value) static_cast<TickType_t>(value)
inline void vTaskDelay(TickType_t ticks) { testClockMs += ticks; }

class String {
public:
    String(const char *text = "") : text_(text) {}
    bool operator==(const char *text) const { return text_ == text; }
    void trim() {
        const size_t first = text_.find_first_not_of(" \t\r\n");
        text_ = first == std::string::npos ? "" : text_.substr(first);
        const size_t last = text_.find_last_not_of(" \t\r\n");
        if (last != std::string::npos) text_.resize(last + 1);
    }
    void toLowerCase() {
        std::transform(text_.begin(), text_.end(), text_.begin(),
                       [](unsigned char c) { return std::tolower(c); });
    }
private:
    std::string text_;
};
