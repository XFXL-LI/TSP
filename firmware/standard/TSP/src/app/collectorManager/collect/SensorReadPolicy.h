#ifndef SENSOR_READ_POLICY_H
#define SENSOR_READ_POLICY_H

#include <stdint.h>

namespace SensorReadPolicy
{
constexpr uint8_t MAX_ATTEMPTS = 3;
constexpr uint32_t RESPONSE_TIMEOUT_MS = 500;
constexpr uint32_t QUERY_GAP_MS = 300;
}

#endif
