#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// Declare before acquiring SERIAL_HJ212. If a wire guard must preserve an
// inherited floor, restore the original base only AFTER releasing that mutex.
// This scope never boosts ACK waits and never allocates RTOS objects or memory.
class Hj212PriorityScope {
public:
    Hj212PriorityScope() : base_(currentBasePriority()) {}
    ~Hj212PriorityScope() { restore(); }
    static UBaseType_t currentBasePriority() {
        TaskStatus_t status = {};
        vTaskGetInfo(nullptr, &status, pdFALSE, eInvalid);
        return status.uxBasePriority;
    }
    UBaseType_t originalPriority() const { return base_; }
    void deferRestoration() { deferred_ = true; }
    void restore() {
        if (deferred_) {
            vTaskPrioritySet(nullptr, base_);
            deferred_ = false;
        }
    }
    Hj212PriorityScope(const Hj212PriorityScope&) = delete;
    Hj212PriorityScope& operator=(const Hj212PriorityScope&) = delete;
private:
    UBaseType_t base_;
    bool deferred_ = false;
};

// Only synchronous GPIO transmission is boosted, never ACK waits or SD I/O.
// Keep interrupts enabled. OTA's existing business pause still applies.
class Hj212TxGuard {
public:
    static constexpr UBaseType_t PRIORITY = 11;

    explicit Hj212TxGuard(bool enabled, Hj212PriorityScope* scope = nullptr) {
        if (!enabled) { ready_ = true; return; }
        base_ = Hj212PriorityScope::currentBasePriority();
        const UBaseType_t effective = uxTaskPriorityGet(nullptr);
        restore_ = base_;
        if (effective >= PRIORITY) { ready_ = true; return; }
        if (effective != base_) {
            // ESP-IDF 5.5.2 vTaskPrioritySet only changes the base while
            // inherited. First align the base with the measured effective
            // priority, then raise. Never force a guessed/lower effective level.
            // Without the caller's post-unlock restore scope, refuse to write.
            if (!scope) return;
            scope->deferRestoration();
            restore_ = effective > base_ ? effective : base_;
            vTaskPrioritySet(nullptr, effective);
        }
        vTaskPrioritySet(nullptr, base_ > PRIORITY ? base_ : PRIORITY);
        raised_ = true;
        // A concurrent inheritance change can invalidate the two-step snapshot.
        // The caller must not write if the measured level is still insufficient.
        ready_ = uxTaskPriorityGet(nullptr) >= PRIORITY;
    }
    ~Hj212TxGuard() {
        if (raised_) vTaskPrioritySet(nullptr, restore_);
    }
    bool ready() const { return ready_; }
    UBaseType_t originalPriority() const { return base_; }
    Hj212TxGuard(const Hj212TxGuard&) = delete;
    Hj212TxGuard& operator=(const Hj212TxGuard&) = delete;

private:
    bool raised_ = false, ready_ = false;
    UBaseType_t base_ = 0, restore_ = 0;
};
