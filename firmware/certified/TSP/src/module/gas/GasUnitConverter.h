#pragma once

#include <Arduino.h>

namespace GasUnitConverter {

static constexpr uint16_t STATUS_READY = 0x8000;
static constexpr uint16_t STATUS_HIGH_CONCENTRATION_PROTECTION = 0x0040;
static constexpr uint16_t STATUS_SENSOR_FAULT = 0x0020;
static constexpr uint16_t STATUS_OVER_RANGE = 0x0010;
static constexpr uint16_t STATUS_ALARM_MASK = 0x000F;
static constexpr uint16_t STATUS_FAULT_MASK =
    STATUS_HIGH_CONCENTRATION_PROTECTION | STATUS_SENSOR_FAULT | STATUS_OVER_RANGE;

inline bool isGasSensor(const String &sensorId)
{
    return sensorId == "w34011" || sensorId == "a05024" ||
           sensorId == "a21004" ||
           sensorId == "a21005" || sensorId == "a21026";
}

inline const char *defaultUnit(const String &sensorId)
{
    return sensorId == "a21005" ? "mg/m3" : "ug/m3";
}

inline bool isSupportedUnit(const String &unit)
{
    return unit == "ppb" || unit == "ppm" ||
           unit == "ug/m3" || unit == "mg/m3";
}

inline String normalizeUnit(const String &sensorId, const String &unit)
{
    if (!isGasSensor(sensorId)) return unit;

    String normalized = unit;
    normalized.trim();
    normalized.toLowerCase();
    return isSupportedUnit(normalized) ? normalized : String(defaultUnit(sensorId));
}

inline float massConcentrationFactor(const String &sensorId)
{
    if (sensorId == "w34011" || sensorId == "a05024") return 1.96319f; // O3
    if (sensorId == "a21004") return 1.88180f; // NO2
    if (sensorId == "a21005") return 1.14560f; // CO
    if (sensorId == "a21026") return 2.62004f; // SO2
    return 1.0f;
}

inline float convertFromPpb(const String &sensorId, float rawPpb, const String &unit)
{
    if (!isGasSensor(sensorId)) return rawPpb;

    const String normalized = normalizeUnit(sensorId, unit);
    if (normalized == "ppm") return rawPpb / 1000.0f;
    if (normalized == "ug/m3") return rawPpb * massConcentrationFactor(sensorId);
    if (normalized == "mg/m3") {
        return rawPpb * massConcentrationFactor(sensorId) / 1000.0f;
    }
    return rawPpb;
}

inline uint8_t decimalsForUnit(const String &unit)
{
    if (unit == "ppb") return 0;
    if (unit == "ppm") return 3;
    return 2;
}

inline bool isMeasurementValid(uint16_t status)
{
    return (status & STATUS_READY) != 0 && (status & STATUS_FAULT_MASK) == 0;
}

} // namespace GasUnitConverter
