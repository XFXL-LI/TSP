#include "DeviceRuntimeStatus.h"
#include "CalendarClock.h"
#include <freertos/FreeRTOS.h>

namespace {
portMUX_TYPE statusMux = portMUX_INITIALIZER_UNLOCKED;
DeviceStatusSnapshot status;
bool startupComplete = false;
bool rtcChecked = false;

void updateInit() {
    if (status.init != 2) status.init = startupComplete && rtcChecked ? 1 : 0;
}
}

namespace DeviceRuntimeStatus {
void startupChecksComplete() {
    portENTER_CRITICAL(&statusMux);
    startupComplete = true;
    updateInit();
    portEXIT_CRITICAL(&statusMux);
}

void rtcCheckComplete(bool valid) {
    portENTER_CRITICAL(&statusMux);
    rtcChecked = true;
    if (valid && status.time == 0) status.time = 1;
    updateInit();
    portEXIT_CRITICAL(&statusMux);
}

void criticalInitFailed() {
    portENTER_CRITICAL(&statusMux);
    status.init = 2;
    portEXIT_CRITICAL(&statusMux);
}

void sdMountStarted() {
    portENTER_CRITICAL(&statusMux);
    status.sd = 1;
    status.hasSdError = false;
    portEXIT_CRITICAL(&statusMux);
}

void sdMountFinished(int32_t error) {
    portENTER_CRITICAL(&statusMux);
    status.sd = error == 0 ? 2 : 3;
    status.sdError = error;
    status.hasSdError = error != 0;
    portEXIT_CRITICAL(&statusMux);
}

void measurementWriteFinished(bool ok) {
    portENTER_CRITICAL(&statusMux);
    status.write = ok ? 1 : 2;
    portEXIT_CRITICAL(&statusMux);
}

void networkTimeConfirmed() {
    portENTER_CRITICAL(&statusMux);
    status.time = 2;
    portEXIT_CRITICAL(&statusMux);
}

DeviceStatusSnapshot read() {
    portENTER_CRITICAL(&statusMux);
    DeviceStatusSnapshot copy = status;
    portEXIT_CRITICAL(&statusMux);
    // Read only the software clock, never the RTC bus or a DTU.
    if (CalendarClock::currentTimestamp() == 0) copy.time = 0;
    return copy;
}
}
