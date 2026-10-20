#include "TempManager.h"
#include "../../module/log/log_manager.h"
#include "../../module/Serial/SerialManager.h"
#include "../../inc/sys_init.h"

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
    taskENTER_CRITICAL(&_heatMux);
    _outputReady = true;
    _isHeating.store(false);
    taskEXIT_CRITICAL(&_heatMux);
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

void TempManager::setHeatingEnabled(bool enabled) {
    taskENTER_CRITICAL(&_heatMux);
    const bool changed = _heatingEnabled != enabled;
    _heatingEnabled = enabled;
    if (!enabled) {
        if (_outputReady) digitalWrite(TEMP_HEAT_PIN, LOW);
        _isHeating.store(false);
    }
    taskEXIT_CRITICAL(&_heatMux);
    const TaskHandle_t task = _controlTask.load();
    if (changed && task != nullptr) xTaskNotifyGive(task);
    LOG_INFO("[DIAG] HEATING_CONFIG enabled=%d", enabled ? 1 : 0);
}

void TempManager::applyHeatingControl() {
    // Serialize the final GPIO decision with a cross-core switch-off request.
    taskENTER_CRITICAL(&_heatMux);
    const auto action = HeatingControlPolicy::decide(
        _heatingEnabled, _sampleValid, _currentTemp, _currentHumi,
        _targetTempLow, _targetHumiUp);
    if (action == HeatingControlPolicy::Action::Start) {
        if (!_isHeating.load()) {
            digitalWrite(TEMP_HEAT_PIN, HIGH);
            _lastActivity = millis();
            _isHeating.store(true);
        }
    } 
    else if (action == HeatingControlPolicy::Action::Stop) {
        if (_isHeating.load()) {
            digitalWrite(TEMP_HEAT_PIN, LOW);
            _isHeating.store(false);
        }
    }
    if ((millis() - _lastActivity) > 6* 60 * 60 * 1000) {
        if (_isHeating.load()) {
            digitalWrite(TEMP_HEAT_PIN, LOW);
            _isHeating.store(false);
        }
    }
    taskEXIT_CRITICAL(&_heatMux);
}

void TempManager::executeControl() {
    applyHeatingControl();
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
        _sampleValid = true;
        _currentTemp = t;
        _currentHumi = h;
        if (systemInfo.mutex == NULL)
        {
            LOG_ERROR("Failed to create mutex for CSQ info");
        }
        else if (xSemaphoreTake(systemInfo.mutex, pdMS_TO_TICKS(3000)) == pdTRUE)
        {
            systemInfo.temp = t;
            systemInfo.mete = h;
            xSemaphoreGive(systemInfo.mutex);
        }
        LOG_DEBUG("TempManager - Current Temp: %.2f C, Humidity: %.2f %%", _currentTemp, _currentHumi);
        executeControl();
    } else {
        _sampleValid = false;
        applyHeatingControl();
    }
}
