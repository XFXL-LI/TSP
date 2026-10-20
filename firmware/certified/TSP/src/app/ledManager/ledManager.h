#pragma once

#include <Arduino.h>
#include "../../inc/sys_init.h"
#include "../../module/QyledLib/QyledLib.h"
#include "../../module/Serial/SerialManager.h"
#include "LedDisplayPolicy.h"

class LedManager {
public:
    static LedManager &getInstance();

    void begin();
    void updateCache(const AllProcessedDataPacket *packet, const COLLECTMAP &collectMap);
    void configure(bool enabled, uint8_t regions, uint32_t now);
    void poll(uint32_t now);
    uint32_t waitMs(uint32_t now) const;

private:
    LedManager();
    ~LedManager();

    LedManager(const LedManager &) = delete;
    LedManager &operator=(const LedManager &) = delete;

    bool sendLine(uint8_t index, const char *content);
    bool sendLineLimit(uint8_t index, const char *content);
    bool sendPacket(const uint8_t *data, uint16_t len);
    void queueSlot(uint8_t slot, const LedDisplayPolicy::Item *item);
    void clearSlots(uint8_t first, uint8_t last);
    void sendPending(uint32_t now);
    uint16_t packTo485(uint8_t index, const char *content, uint8_t *buffer, uint16_t bufferLen);
    uint16_t packTo485Limit(uint8_t index, const char *content, uint8_t *buffer, uint16_t bufferLen);
    bool buildDisplayTextBySensorId(const String &sensorId, float value,
                                    const String &unit,
                                    char *buffer, size_t size);

    void buildWindSpeedText(float value, char *buffer, size_t size);
    void buildWindDirectionText(float value, char *buffer, size_t size);
    void buildTemperatureText(float value, char *buffer, size_t size);
    void buildHumidityText(float value, char *buffer, size_t size);
    void buildPressureText(float value, char *buffer, size_t size);
    void buildNoiseText(float value, char *buffer, size_t size);

    LedDisplayPolicy::Cache _cache;
    LedDisplayPolicy::Item _lastSent[4];
    LedDisplayPolicy::Item _pending[4];
    uint8_t _sentMask = 0;
    uint8_t _pendingMask = 0;
    uint8_t _regions = 2;
    bool _enabled = true;
    bool _configured = false;
    bool _clearInactive = true;
    bool _pagePending = false;
    bool _frameGapActive = false;
    uint32_t _nextPageAt = 0;
    uint32_t _nextFrameAt = 0;
};
