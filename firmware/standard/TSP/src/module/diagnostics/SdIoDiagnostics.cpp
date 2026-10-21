#include "SdIoDiagnostics.h"
#include "../log/log_manager.h"
#include <Arduino.h>
#include <freertos/FreeRTOS.h>

namespace SdIoDiagnostics {
namespace {
portMUX_TYPE stateMux = portMUX_INITIALIZER_UNLOCKED;
State state;
uint32_t lastReport = 0;

void report(const State& value, uint32_t now, bool changed) {
    LOG_INFO("[DIAG] SD_ABA_%s phase=%s ms=%u phase_ms=%u active=%u",
             changed ? "PHASE" : "STATUS", phaseName(value.phase), (unsigned)now,
             (unsigned)(now - value.since), (unsigned)value.active);
    LOG_INFO("[DIAG] SD_ABA_SKIPS ms=%u history=%u pending=%u query=%u mount=%u other=%u",
             (unsigned)now, (unsigned)value.skipped[0], (unsigned)value.skipped[1],
             (unsigned)value.skipped[2], (unsigned)value.skipped[3],
             (unsigned)value.skipped[4]);
}
}

const char* phaseName(Phase p) {
    switch (p) {
        case Phase::Idle: return "idle";
        case Phase::OnA: return "on_a";
        case Phase::Draining: return "draining";
        case Phase::OffB: return "off_b";
        case Phase::OnA2: return "on_a2";
        case Phase::Complete: return "complete";
    }
    return "unknown";
}

void begin() {
    const uint32_t now = millis();
    portENTER_CRITICAL(&stateMux);
    const Phase before = state.phase;
    state.begin(now);
    const State value = state;
    lastReport = now;
    portEXIT_CRITICAL(&stateMux);
    if (before != value.phase) {
        LOG_WARNING("[DIAG] SD_ABA_START ms=%u on_a_ms=%u off_b_ms=%u on_a2_ms=%u repeat=0 resume=normal",
                    (unsigned)now, (unsigned)FIRST_ON_MS, (unsigned)OFF_MS,
                    (unsigned)SECOND_ON_MS);
        report(value, now, true);
    }
}

void poll() {
    const uint32_t now = millis();
    portENTER_CRITICAL(&stateMux);
    const Phase before = state.phase;
    state.advance(now);
    const State value = state;
    const bool changed = before != value.phase;
    const bool due = value.phase != Phase::Idle && uint32_t(now - lastReport) >= 60000U;
    if (changed || due) lastReport = now;
    portEXIT_CRITICAL(&stateMux);
    if (changed || due) report(value, now, changed);
}

Snapshot snapshot() {
    portENTER_CRITICAL(&stateMux);
    const Snapshot value{state.phase, state.since, state.active};
    portEXIT_CRITICAL(&stateMux);
    return value;
}
bool blocked() { const Phase p = snapshot().phase; return p == Phase::Draining || p == Phase::OffB; }
void noteSkip(Skip why) {
    portENTER_CRITICAL(&stateMux);
    state.skip(why);
    portEXIT_CRITICAL(&stateMux);
}

bool Guard::start(const char* operation, Skip why, uint32_t trace, int type, uint64_t timestamp) {
    if (entered_) return true;
    const uint32_t now = millis();
    portENTER_CRITICAL(&stateMux);
    const Phase before = state.phase;
    entered_ = state.enter(now, why);
    const State value = state;
    id_ = value.sequence;
    portEXIT_CRITICAL(&stateMux);
    if (before != value.phase) report(value, now, true);
    if (!entered_) {
        LOG_INFO("[DIAG] SD_IO_SKIP op=%s ms=%u phase=%s trace=%u type=%d timestamp=%llu",
                 operation, (unsigned)now, phaseName(value.phase), (unsigned)trace, type, timestamp);
        return false;
    }
    operation_ = operation;
    started_ = now;
    LOG_INFO("[DIAG] SD_IO_BEGIN id=%u op=%s start_ms=%u phase=%s trace=%u type=%d timestamp=%llu",
             (unsigned)id_, operation_, (unsigned)started_, phaseName(value.phase),
             (unsigned)trace, type, timestamp);
    return true;
}

void Guard::finish() {
    if (!entered_) return;
    const uint32_t finished = millis();
    LOG_INFO("[DIAG] SD_IO_END id=%u op=%s start_ms=%u end_ms=%u elapsed_ms=%u",
             (unsigned)id_, operation_, (unsigned)started_, (unsigned)finished,
             (unsigned)(finished - started_));
    const uint32_t now = millis();
    portENTER_CRITICAL(&stateMux);
    const Phase before = state.phase;
    state.leave(now);
    const State value = state;
    portEXIT_CRITICAL(&stateMux);
    entered_ = false;
    if (before != value.phase) report(value, now, true);
}
}
