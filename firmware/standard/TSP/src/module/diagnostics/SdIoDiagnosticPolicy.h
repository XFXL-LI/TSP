#pragma once
#include <stdint.h>

// One run per boot. Timing is monotonic and never changes persisted settings.
namespace SdIoDiagnostics {
constexpr uint32_t FIRST_ON_MS = 30UL * 60UL * 1000UL;
constexpr uint32_t OFF_MS = 60UL * 60UL * 1000UL;
constexpr uint32_t SECOND_ON_MS = 30UL * 60UL * 1000UL;
enum class Phase : uint8_t { Idle, OnA, Draining, OffB, OnA2, Complete };
enum class Skip : uint8_t { History, Pending, Query, Mount, Other, Count };
struct State {
    Phase phase = Phase::Idle;
    uint32_t since = 0, started = 0, active = 0, sequence = 0;
    uint32_t skipped[static_cast<unsigned>(Skip::Count)] = {};
    bool blocked() const { return phase == Phase::Draining || phase == Phase::OffB; }
    void begin(uint32_t now) {
        if (phase != Phase::Idle) return;
        phase = Phase::OnA; started = since = now;
    }
    void advance(uint32_t now) {
        if (phase == Phase::OnA && uint32_t(now - since) >= FIRST_ON_MS) {
            phase = Phase::Draining; since = now;
        }
        if (phase == Phase::Draining && active == 0) {
            phase = Phase::OffB; since = now;
        } else if (phase == Phase::OffB && uint32_t(now - since) >= OFF_MS) {
            phase = Phase::OnA2; since = now;
        } else if (phase == Phase::OnA2 && uint32_t(now - since) >= SECOND_ON_MS) {
            phase = Phase::Complete; since = now;
        }
    }
    bool enter(uint32_t now, Skip reason) {
        advance(now);
        if (blocked()) { skip(reason); return false; }
        ++active; ++sequence; return true;
    }
    void leave(uint32_t now) { if (active) --active; advance(now); }
    void skip(Skip reason) { ++skipped[static_cast<unsigned>(reason)]; }
};
}
