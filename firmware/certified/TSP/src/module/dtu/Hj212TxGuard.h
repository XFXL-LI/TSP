#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// Only synchronous GPIO transmission is boosted, never ACK waits or SD I/O.
// Keep interrupts enabled. OTA's existing business pause still applies.
class Hj212TxGuard {
public:
    static constexpr UBaseType_t PRIORITY = 11;

    explicit Hj212TxGuard(bool enabled) : enabled_(enabled) {
        if (!enabled_) return;
        TaskStatus_t status = {};
        vTaskGetInfo(nullptr, &status, pdFALSE, eInvalid);
        base_ = status.uxBasePriority;
        if (base_ < PRIORITY) {
            vTaskPrioritySet(nullptr, PRIORITY);
            raised_ = true;
        }
    }
    ~Hj212TxGuard() {
        if (raised_) vTaskPrioritySet(nullptr, base_);
    }
    UBaseType_t originalPriority() const { return base_; }
    Hj212TxGuard(const Hj212TxGuard&) = delete;
    Hj212TxGuard& operator=(const Hj212TxGuard&) = delete;

private:
    bool enabled_ = false;
    bool raised_ = false;
    UBaseType_t base_ = 0;
};
