#pragma once

#include "GasUnitConverter.h"
#include "../../inc/sys_init.h"

namespace GasStatusPolicy {

inline DataStatus classify(uint16_t status)
{
    if ((status & GasUnitConverter::STATUS_READY) == 0U ||
        (status & GasUnitConverter::STATUS_SENSOR_FAULT) != 0U) {
        return DataStatus::SENSOR_FAULT;
    }
    if ((status & (GasUnitConverter::STATUS_HIGH_CONCENTRATION_PROTECTION |
                   GasUnitConverter::STATUS_OVER_RANGE)) != 0U) {
        return DataStatus::OVER_RANGE;
    }
    return DataStatus::NORMAL;
}

} // namespace GasStatusPolicy
