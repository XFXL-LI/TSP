#include "SdIoDiagnostics.h"
#include "../log/log_manager.h"
#include <Arduino.h>
#include <freertos/FreeRTOS.h>

namespace SdIoDiagnostics {
namespace {
portMUX_TYPE stateMux = portMUX_INITIALIZER_UNLOCKED;
State state;
}

const char* phaseName(Phase p) {
    switch (p) {
        case Phase::Idle: return "idle";
        case Phase::Normal: return "normal";
    }
    return "unknown";
}

void begin() {
    const uint32_t now = millis();
    portENTER_CRITICAL(&stateMux);
    const Phase before = state.phase;
    state.begin(now);
    const State value = state;
    portEXIT_CRITICAL(&stateMux);
    if (before != value.phase) {
        LOG_INFO("[DIAG] SD_MODE mode=normal comparison=off ms=%u", (unsigned)now);
    }
}

void poll() {
    // Existing maintenance callers remain valid; no timed phase transitions.
}

Snapshot snapshot() {
    portENTER_CRITICAL(&stateMux);
    const Snapshot value{state.phase, state.since, state.active};
    portEXIT_CRITICAL(&stateMux);
    return value;
}
bool blocked() { return false; }
void noteSkip(Skip why) {
    portENTER_CRITICAL(&stateMux);
    state.skip(why);
    portEXIT_CRITICAL(&stateMux);
}

bool Guard::start(const char* operation, Skip why, uint32_t trace, int type, uint64_t timestamp) {
    if (entered_) return true;
    const uint32_t now = millis();
    portENTER_CRITICAL(&stateMux);
    entered_ = state.enter(now, why);
    const State value = state;
    id_ = value.sequence;
    portEXIT_CRITICAL(&stateMux);
    operation_ = operation;
    started_ = now;
    LOG_DEBUG("[DIAG] SD_IO_BEGIN id=%u op=%s start_ms=%u phase=%s trace=%u type=%d timestamp=%llu",
             (unsigned)id_, operation_, (unsigned)started_, phaseName(value.phase),
             (unsigned)trace, type, timestamp);
    return true;
}

void Guard::finish() {
    if (!entered_) return;
    const uint32_t finished = millis();
    LOG_DEBUG("[DIAG] SD_IO_END id=%u op=%s start_ms=%u end_ms=%u elapsed_ms=%u",
             (unsigned)id_, operation_, (unsigned)started_, (unsigned)finished,
             (unsigned)(finished - started_));
    if (uint32_t(finished - started_) >= SLOW_IO_MS) {
        LOG_WARNING("[DIAG] SD_IO_SLOW id=%u op=%s elapsed_ms=%u",
                    (unsigned)id_, operation_, (unsigned)(finished - started_));
    }
    portENTER_CRITICAL(&stateMux);
    state.leave(finished);
    portEXIT_CRITICAL(&stateMux);
    entered_ = false;
}
}
