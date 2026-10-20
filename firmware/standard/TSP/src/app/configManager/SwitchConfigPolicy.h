#pragma once

#include <stdint.h>

namespace SwitchConfigPolicy {
struct Controls {
    bool heatingEnabled = true;
    bool ledEnabled = true;
    uint8_t ledRegions = 2;
};

struct BoolField {
    bool present;
    bool isBoolean;
    bool value;
};

constexpr uint8_t pack(const Controls &value) {
    return (value.heatingEnabled ? 1U : 0U) |
           (value.ledEnabled ? 2U : 0U) |
           (value.ledRegions == 4 ? 4U : 0U);
}

constexpr Controls unpack(uint8_t bits) {
    return {(bits & 1U) != 0, (bits & 2U) != 0,
            static_cast<uint8_t>((bits & 4U) != 0 ? 4 : 2)};
}

inline bool readHeating(BoolField current, BoolField legacy, Controls &value) {
    const BoolField field = current.present ? current : legacy;
    if (!field.present) return true;
    if (!field.isBoolean) return false;
    value.heatingEnabled = field.value;
    return true;
}

inline bool applyClient(Controls &value, BoolField heating, BoolField legacyHeating,
                        BoolField ledEnabled, BoolField fourRegions) {
    Controls next = value;
    if (!readHeating(heating, legacyHeating, next)) return false;
    if (ledEnabled.present) {
        if (!ledEnabled.isBoolean) return false;
        next.ledEnabled = ledEnabled.value;
    }
    if (fourRegions.present) {
        if (!fourRegions.isBoolean) return false;
        next.ledRegions = fourRegions.value ? 4 : 2;
    }
    value = next;
    return true;
}

static_assert(pack(Controls{}) == 3, "First-upgrade LED defaults: enabled, two regions");
static_assert(unpack(7).ledRegions == 4, "Four-region decoding");
} // namespace SwitchConfigPolicy
