#pragma once
#ifndef TEMP_MANAGER_H
#define TEMP_MANAGER_H

#include <Arduino.h>
#include "../../module/sht30/sht30.h"

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
    bool isHeatingActive() const { return _isHeating; }

private:
    TempManager();
    ~TempManager() = default;
    TempManager(const TempManager&) = delete;
    TempManager& operator=(const TempManager&) = delete;

    SHT30 _sht30;
    
    // 控制参数
    float _targetTempUp;
    float _targetHumiUp;
    float _targetTempLow;
    float _targetHumiLow;

    // 实时状态值
    float _currentTemp;
    float _currentHumi;
    bool _isHeating;
    bool _isFanRuning;

    void executeControl();
};

#endif // TEMP_MANAGER_H