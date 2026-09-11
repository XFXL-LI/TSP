#include "SHT30.h"
#include "../log/log_manager.h"
#include "ClosedCube_SHT31D.h"

ClosedCube_SHT31D sht3xd;

SHT30::SHT30(uint8_t address) : _address(address), _isInitialized(false) {}

bool SHT30::begin(int sdaPin, int sclPin, uint32_t frequency) {
    _isInitialized = Wire.begin(sdaPin, sclPin, frequency);
    sht3xd.begin(0x44);
    if (sht3xd.periodicStart(SHT3XD_REPEATABILITY_HIGH, SHT3XD_FREQUENCY_10HZ) != SHT3XD_NO_ERROR)
		LOG_ERROR("Cannot start periodic mode");
    return _isInitialized;
}

bool SHT30::readTemperatureHumidity(float &temperature, float &humidity) {
    if (!_isInitialized) {
        return false;
    }

    SHT31D result = sht3xd.periodicFetchData();
    if (result.error == SHT3XD_NO_ERROR) {
        temperature = result.t;
        humidity = result.rh;
        return true;
    } else {
        LOG_ERROR("Failed to read from SHT30 sensor, error code: %d", result.error);
        return false;
    }
}