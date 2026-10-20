#pragma once
#include <stdint.h>

// Relative horizons must stay below INT32_MAX milliseconds. An explicit flag
// keeps a deadline that wraps to zero distinct from an unbounded legacy call.
struct ReadDeadline {
    uint32_t targetMs = 0;
    bool enabled = false;

    static constexpr ReadDeadline at(uint32_t target) {
        return {target, true};
    }

    constexpr uint32_t remainingMs(uint32_t now) const {
        const int32_t remaining = static_cast<int32_t>(targetMs - now);
        return !enabled ? UINT32_MAX
                        : remaining > 0 ? static_cast<uint32_t>(remaining) : 0;
    }

    constexpr bool fits(uint32_t now, uint32_t budgetMs) const {
        return !enabled || (remainingMs(now) > 0 &&
                            remainingMs(now) >= budgetMs);
    }
};

static_assert(ReadDeadline::at(1000).remainingMs(900) == 100, "deadline budget");
static_assert(!ReadDeadline::at(1000).fits(1000, 0), "expired deadline");
static_assert(ReadDeadline::at(0).remainingMs(UINT32_MAX) == 1, "zero wrap");
static_assert(ReadDeadline::at(20).remainingMs(UINT32_MAX - 9) == 30, "wrap");
