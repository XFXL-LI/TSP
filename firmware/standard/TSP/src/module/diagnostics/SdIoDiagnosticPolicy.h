#pragma once
#include <stdint.h>

// Passive operation accounting. Normal firmware never schedules an SD pause.
namespace SdIoDiagnostics {
constexpr uint32_t SLOW_IO_MS = 3000U;
enum class Phase : uint8_t { Idle, Normal };
enum class Skip : uint8_t { History, Pending, Query, Mount, Other, Count };
struct State {
    Phase phase = Phase::Idle;
    uint32_t since = 0, active = 0, sequence = 0;
    uint32_t skipped[static_cast<unsigned>(Skip::Count)] = {};
    bool blocked() const { return false; }
    void begin(uint32_t now) {
        if (phase != Phase::Idle) return;
        phase = Phase::Normal; since = now;
    }
    bool enter(uint32_t, Skip) {
        ++active; ++sequence; return true;
    }
    void leave(uint32_t) { if (active) --active; }
    void skip(Skip reason) { ++skipped[static_cast<unsigned>(reason)]; }
};
}
