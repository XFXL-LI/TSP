#pragma once

#include <Arduino.h>
#include "../../inc/sys_init.h"
#include "../../module/QyledLib/QyledLib.h"
#include "../../module/Serial/SerialManager.h"

class LedManager {
public:
    static LedManager &getInstance();

    void begin();
    void updateDisplay(const AllProcessedDataPacket *packet, int updateTime, COLLECTMAP &collectMap);

private:
    LedManager();
    ~LedManager();

    LedManager(const LedManager &) = delete;
    LedManager &operator=(const LedManager &) = delete;

    float getValue(const AllProcessedDataPacket *packet, const String &sensorId) const;

    void sendPage(const AllProcessedDataPacket *packet, uint8_t step);
    void sendLine(uint8_t index, const char *content);
    void sendLineLimit(uint8_t index, const char *content);
    void sendPacket(const uint8_t *data, uint16_t len);
    uint16_t packTo485(uint8_t index, const char *content, uint8_t *buffer, uint16_t bufferLen);
    uint16_t packTo485Limit(uint8_t index, const char *content, uint8_t *buffer, uint16_t bufferLen);
    bool buildDisplayTextBySensorId(const String &sensorId, float value, char *buffer, size_t size);

    void buildWindSpeedText(float value, char *buffer, size_t size);
    void buildWindDirectionText(float value, char *buffer, size_t size);
    void buildTemperatureText(float value, char *buffer, size_t size);
    void buildHumidityText(float value, char *buffer, size_t size);
    void buildPressureText(float value, char *buffer, size_t size);
    void buildNoiseText(float value, char *buffer, size_t size);

    uint8_t _runLedStep;
};
