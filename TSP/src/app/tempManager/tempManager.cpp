#include "TempManager.h"
#include "../../module/log/log_manager.h"
#include "../../module/Serial/SerialManager.h"

TempManager::TempManager() : 
    _targetTempUp(35.0f), 
    _targetTempLow(15.5f),
    _targetHumiUp(70.0f), 
    _targetHumiLow(40.5f),
    _currentTemp(0.0f),
    _currentHumi(0.0f),
    _isHeating(false),
    _isFanRuning(false) {}

void TempManager::begin() {
    pinMode(TEMP_HEAT_PIN, OUTPUT);
    pinMode(FAN_PIN, OUTPUT);
    digitalWrite(TEMP_HEAT_PIN, LOW);
    digitalWrite(FAN_PIN, LOW);
    if (!_sht30.begin(TEMP_I2C_SDA, TEMP_I2C_SCL, 30000)) {
        LOG_ERROR("TempManager - Failed to initialize SHT30 sensor");
    }
}

void TempManager::setTargetTemp(int tempUp, int tempLow) {
    _targetTempUp = (float)tempUp;
    _targetTempLow = (float)tempLow;
}
void TempManager::setTargetHumi(int humiUp, int humiLow) {
    _targetHumiUp = (float)humiUp;
    _targetHumiLow = (float)humiLow;
}

void TempManager::executeControl() {
    if (_currentTemp < _targetTempLow || _currentHumi > _targetHumiUp) {
        if (!_isHeating) {
            digitalWrite(TEMP_HEAT_PIN, HIGH);
            _isHeating = true;
        }
    } 
    else if (_currentTemp >= _targetTempLow && _currentHumi <= _targetHumiUp) {
        if (_isHeating) {
            digitalWrite(TEMP_HEAT_PIN, LOW);
            _isHeating = false;
        }
    }
    if (_currentTemp > 25.0) {
        if (!_isFanRuning){
            digitalWrite(FAN_PIN, HIGH);
            _isFanRuning = true;
        }
    } else if (_currentTemp < 25.0) {
        if (_isFanRuning){
            digitalWrite(FAN_PIN, LOW);
            _isFanRuning = false;
        }
    }
}

void TempManager::poll() {
    float t = 0.0f;
    float h = 0.0f;
    LOG_DEBUG("TempManager - Polling SHT30 sensor...");
    if (_sht30.readTemperatureHumidity(t, h)) {
        _currentTemp = t;
        _currentHumi = h;
        LOG_DEBUG("TempManager - Current Temp: %.2f C, Humidity: %.2f %%", _currentTemp, _currentHumi);
        executeControl();
    } else {
        digitalWrite(TEMP_HEAT_PIN, LOW);
        _isHeating = false;
    }
}