#pragma once
#include <stdint.h>
#include <stddef.h>

namespace TvocPolicy {
constexpr const char* ID = "a99054";
constexpr const char* LEGACY_ID = "a24035";
constexpr uint8_t SLAVE = 3;
constexpr uint16_t CONCENTRATION_REGISTER = 5;
constexpr uint16_t STATUS_REGISTER = 272;
constexpr uint16_t VERSION_REGISTER = 256;
constexpr uint32_t INTERVAL_MS = 5000;
constexpr uint32_t PUMP_MS = 70000;
// Provisional engineering window; validate the 10-second flush with real gas.
constexpr uint32_t WINDOW_START_MS = 10000;
constexpr uint8_t PLANNED_SAMPLES = (PUMP_MS - WINDOW_START_MS) / INTERVAL_MS;
constexpr uint16_t CALIBRATED = 1U << 0;
constexpr uint16_t BUSY = 1U << 2;
constexpr uint16_t ADC_FAILURE = 1U << 3;
constexpr uint16_t SATURATION = 1U << 4;
constexpr uint16_t OVER_RANGE = 1U << 5;
// Isobutylene equivalent, ideal gas at 25 C and 101.325 kPa.
// M = 56.1063 g/mol (NIST), R = 8.31446261815324 J/(mol K).
constexpr float MASS_FACTOR = 56.1063 / (8.31446261815324 * 298.15 / 101.325);

struct Sample {
    uint32_t ppb = 0;
    uint16_t flags = 0;
    uint16_t calibrationState = 0;
    uint16_t calibrationError = 0;
    bool valid = false;
    char status = 'B';
};
inline Sample decode(bool measurementOk, const uint16_t* concentration,
                     bool statusOk, const uint16_t* info) {
    Sample s;
    if (!statusOk) return s; // Never pair a measurement with cached status.
    s.flags = info[0];
    s.calibrationState = info[1];
    s.calibrationError = info[2];
    if (s.flags & BUSY) { s.status = 'C'; return s; }
    if (!(s.flags & CALIBRATED) || (s.flags & (ADC_FAILURE | SATURATION))) {
        s.status = 'D'; return s;
    }
    if (!measurementOk) return s;
    s.ppb = (uint32_t(concentration[0]) << 16) | concentration[1];
    if (s.ppb == UINT32_MAX) { s.status = 'D'; return s; }
    // A previous failed/cancelled calibration does not invalidate good old parameters.
    s.valid = true;
    s.status = (s.flags & OVER_RANGE) ? 'T' : 'N';
    return s;
}
// One call per scheduled slot. Failure consumes its slot; never replay an old value.
template<class Reader>
Sample readSample(Reader& read) {
    uint16_t concentration[2] = {};
    uint16_t info[4] = {};
    const bool measured = read(CONCENTRATION_REGISTER, 2, concentration);
    const bool statusRead = read(STATUS_REGISTER, 4, info);
    return decode(measured, concentration, statusRead, info);
}
inline bool slotAvailable(uint32_t now, uint32_t cycleStart, uint8_t slot) {
    const uint32_t elapsed = now - cycleStart;
    const uint32_t start = WINDOW_START_MS + uint32_t(slot) * INTERVAL_MS;
    return slot < PLANNED_SAMPLES && elapsed >= start &&
           elapsed < start + INTERVAL_MS && elapsed < PUMP_MS;
}
inline uint8_t priority(char status) {
    return status == 'C' ? 6 : status == 'B' ? 5 : status == 'D' ? 4 :
           status == 'T' ? 3 : 0;
}
struct Batch {
    Sample samples[PLANNED_SAMPLES] = {};
    uint8_t count = 0;
    uint8_t validCount = 0;
    uint64_t sum = 0;
    bool overRange = false;
    char failure = 'D';
    void add(const Sample& s) {
        if (count >= PLANNED_SAMPLES) return;
        samples[count++] = s;
        if (s.valid) {
            sum += s.ppb;
            ++validCount; // Includes real zero and valid isolated high readings.
            overRange = overRange || s.status == 'T';
        } else if (priority(s.status) > priority(failure)) {
            failure = s.status;
        }
    }
    bool valid() const { return validCount > 0; }
    float average() const { return valid() ? float(double(sum) / validCount) : 0.0f; }
    char status() const { return valid() ? (overRange ? 'T' : 'N') : failure; }
};
static_assert(PLANNED_SAMPLES == 12, "TVOC provisional sample count");
}
