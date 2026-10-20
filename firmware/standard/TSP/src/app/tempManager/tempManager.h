#pragma once
#ifndef TEMP_MANAGER_H
#define TEMP_MANAGER_H

#include <Arduino.h>
#include "../../module/sht30/sht30.h"
#include "HeatingControlPolicy.h"
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define TEMP_HEAT_PIN 42 // 加热
#define TEMP_I2C_SDA 8
#define TEMP_I2C_SCL 18
#define FAN_PIN 14

class TempManager {
public:
    // 获取单例实例
    static TempManager& getInstance() {
        static TempManager instance;
        return instance;
    }
    void begin();
    void poll();
    void setTargetTemp(int tempUp, int tempLow);
    void setTargetHumi(int humiUp, int humiLow);
    void setHeatingEnabled(bool enabled);
    void bindControlTask(TaskHandle_t task) { _controlTask.store(task); }
    void applyHeatingControl();
    bool isHeatingActive() const { return _isHeating.load(); }

private:
    TempManager();
    ~TempManager() = default;
    TempManager(const TempManager&) = delete;
    TempManager& operator=(const TempManager&) = delete;

    SHT30 _sht30;
    

    unsigned long _lastActivity = 0;
    // 控制参数
    float _targetTempUp;
    float _targetHumiUp;
    float _targetTempLow;
    float _targetHumiLow;

    // 实时状态值
    float _currentTemp;
    float _currentHumi;
    std::atomic<bool> _isHeating;
    bool _isFanRuning;
    bool _heatingEnabled = false;
    bool _outputReady = false;
    bool _sampleValid = false;
    portMUX_TYPE _heatMux = portMUX_INITIALIZER_UNLOCKED;
    std::atomic<TaskHandle_t> _controlTask{nullptr};

    void executeControl();
};

#endif // TEMP_MANAGER_H
