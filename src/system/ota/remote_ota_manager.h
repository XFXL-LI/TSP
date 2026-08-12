#ifndef REMOTE_OTA_MANAGER_H
#define REMOTE_OTA_MANAGER_H

#include <freertos/FreeRTOS.h>

namespace RemoteOtaManager
{
// Start the persistent OTA request listener. The active worker is created only
// for an accepted upload request.
bool begin(UBaseType_t listenerPriority, UBaseType_t workerPriority);

bool isActive();

// Business code enters a guarded region only while it is actively using
// sensors, storage, serial ports, queues or outputs. A pending OTA blocks new
// guarded regions and waits for every in-flight region to finish.
void beginBusinessActivity();
void endBusinessActivity();

bool requestBusinessPause(uint32_t timeoutMs);
void resumeBusiness();
uint32_t activeBusinessCount();

class BusinessActivityGuard
{
public:
    BusinessActivityGuard()
    {
        beginBusinessActivity();
    }

    ~BusinessActivityGuard()
    {
        endBusinessActivity();
    }

    BusinessActivityGuard(const BusinessActivityGuard &) = delete;
    BusinessActivityGuard &operator=(const BusinessActivityGuard &) = delete;
};
}

#endif // REMOTE_OTA_MANAGER_H
