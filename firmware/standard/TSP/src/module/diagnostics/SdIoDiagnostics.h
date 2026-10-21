#pragma once
#include "SdIoDiagnosticPolicy.h"

namespace SdIoDiagnostics {
struct Snapshot {
    Phase phase;
    uint32_t since, active;
};
void begin();
void poll();
bool blocked();
Snapshot snapshot();
const char* phaseName(Phase phase);
void noteSkip(Skip reason);

// Use once per top-level SD operation, after acquiring its filesystem mutex.
// Helpers inside that operation (mkdir, verify, cleanup) share this admission.
class Guard {
public:
    Guard() = default;
    explicit Guard(const char* operation, Skip reason = Skip::Other) { start(operation, reason); }
    ~Guard() { finish(); }
    bool start(const char* operation, Skip reason, uint32_t trace = 0,
               int type = -1, uint64_t timestamp = 0);
    void finish();
    bool entered() const { return entered_; }
    Guard(const Guard&) = delete;
    Guard& operator=(const Guard&) = delete;
private:
    const char* operation_ = nullptr;
    uint32_t id_ = 0, started_ = 0;
    bool entered_ = false;
};
}
