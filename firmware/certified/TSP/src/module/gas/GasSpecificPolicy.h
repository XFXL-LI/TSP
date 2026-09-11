#pragma once
#ifndef GAS_SPECIFIC_POLICY_H
#define GAS_SPECIFIC_POLICY_H

#include <stdint.h>
#include <string.h>

namespace GasSpecificPolicy {

constexpr uint16_t CERTIFIED_MAX_PPB = 500;

inline bool hasCertifiedLimit(const char *sensorId)
{
    if (sensorId == nullptr) return false;
    return strcmp(sensorId, "w34011") == 0 ||
           strcmp(sensorId, "a21004") == 0 ||
           strcmp(sensorId, "a21026") == 0;
}

inline uint16_t applyPpbLimit(const char *sensorId, uint16_t valuePpb)
{
    return hasCertifiedLimit(sensorId) && valuePpb > CERTIFIED_MAX_PPB
        ? CERTIFIED_MAX_PPB : valuePpb;
}

} // namespace GasSpecificPolicy

#endif
