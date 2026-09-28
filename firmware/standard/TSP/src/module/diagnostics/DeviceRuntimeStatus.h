#pragma once
#include <stdint.h>

struct DeviceStatusSnapshot {
    uint8_t init = 0;
    uint8_t sd = 0;
    uint8_t write = 0;
    uint8_t time = 0;
    int32_t sdError = 0;
    bool hasSdError = false;
};

// Producers update existing state transitions; queries only copy these fields.
// No heap allocation, device access, new task, or dependency on LCD presence.
namespace DeviceRuntimeStatus {
void startupChecksComplete();
void rtcCheckComplete(bool valid);
void criticalInitFailed();
void sdMountStarted();
void sdMountFinished(int32_t error);
void measurementWriteFinished(bool ok);
void networkTimeConfirmed();
DeviceStatusSnapshot read();
}
