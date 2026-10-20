#include "ledManager.h"

#include <cstring>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "../../module/log/log_manager.h"
#include "../../module/gas/GasUnitConverter.h"

LedManager::LedManager() {}

LedManager::~LedManager() {}

LedManager &LedManager::getInstance()
{
    static LedManager instance;
    return instance;
}

void LedManager::begin()
{
    LOG_INFO("LED manager initialized on 485 transport");
}

void LedManager::updateCache(const AllProcessedDataPacket *packet, const COLLECTMAP &collectMap)
{
    if (packet == nullptr)
    {
        LOG_WARNING("LED update skipped because packet is null");
        return;
    }
    _cache.beginUpdate();
    for (const char *id : LedDisplayPolicy::SENSOR_ORDER)
    {
        const String sensorId(id);
        const auto entry = packet->processed_data_map.find(sensorId);
        if (entry == packet->processed_data_map.end()) continue;
        const ProcessedDataPacket &processedData = entry->second;
        if (!processedData.is_valid)
        {
            continue;
        }
        char content[64] = {0};
        float alarmLimit = 0.0f;
        float displayValue = processedData.value;
        String displayUnit;
        auto it = collectMap.find(sensorId);
        if (it != collectMap.end())
        {
            alarmLimit = (float)it->second.alarmLimit;
            displayUnit = it->second.unit;
        }
        if (GasUnitConverter::isGasSensor(sensorId))
        {
            displayUnit = GasUnitConverter::normalizeUnit(sensorId, displayUnit);
            displayValue = GasUnitConverter::convertFromPpb(sensorId,
                                                            processedData.value,
                                                            displayUnit);
        }
        if (!buildDisplayTextBySensorId(sensorId, displayValue, displayUnit,
                                        content, sizeof(content)))
        {
            continue;
        }
        _cache.append(content, displayValue > alarmLimit);
    }
    _cache.finishUpdate(_regions);
    LOG_DEBUG("[DIAG] LED_CACHE items=%u bytes=%u", _cache.count(),
              static_cast<unsigned>(sizeof(*this)));
}

void LedManager::configure(bool enabled, uint8_t regions, uint32_t now)
{
    regions = regions == 4 ? 4 : 2;
    if (_configured && enabled == _enabled && regions == _regions) return;
    const bool modeChanged = regions != _regions;
    _configured = true;
    _enabled = enabled;
    _regions = regions;
    _cache.resetPage();
    // Cancel unsent content from the old mode, but preserve the frame deadline.
    _pendingMask = 0;
    _pagePending = false;
    _nextPageAt = now;
    _clearInactive = regions == 2;
    if (!enabled) {
        clearSlots(0, 4);
    } else if (modeChanged && regions == 2) {
        clearSlots(2, 4);
    }
    LOG_INFO("[DIAG] LED_CONFIG enabled=%d regions=%u frame_gap_ms=%u",
             enabled ? 1 : 0, regions, LedDisplayPolicy::FRAME_INTERVAL_MS);
}

void LedManager::queueSlot(uint8_t slot, const LedDisplayPolicy::Item *item)
{
    LedDisplayPolicy::Item blank;
    LedDisplayPolicy::setItem(blank, " ", false);
    const auto &value = item != nullptr ? *item : blank;
    if ((_sentMask & (1U << slot)) != 0 &&
        _lastSent[slot].alarm == value.alarm &&
        std::strcmp(_lastSent[slot].text, value.text) == 0) {
        _pendingMask &= static_cast<uint8_t>(~(1U << slot));
        return;
    }
    _pending[slot] = value;
    _pendingMask |= 1U << slot;
}

void LedManager::clearSlots(uint8_t first, uint8_t last)
{
    for (uint8_t slot = first; slot < last; ++slot) queueSlot(slot, nullptr);
}

void LedManager::sendPending(uint32_t now)
{
    if (_frameGapActive) {
        if (!LedDisplayPolicy::due(now, _nextFrameAt)) return;
        _frameGapActive = false; // Expire the deadline even during long idle periods.
    }
    if (_pendingMask == 0) return;
    uint8_t slot = 0;
    while ((_pendingMask & (1U << slot)) == 0) ++slot;
    const auto &value = _pending[slot];
    const uint32_t startedAt = millis();
    const bool sent = value.alarm ? sendLineLimit(41 + slot, value.text)
                                  : sendLine(41 + slot, value.text);
    const uint32_t finishedAt = millis();
    // Start the quiet gap after synchronous UART transmission, not before it.
    // Failed/partial writes retain their target with a bounded retry rate.
    _nextFrameAt = finishedAt + (sent ? LedDisplayPolicy::FRAME_INTERVAL_MS
                                     : LedDisplayPolicy::PAGE_INTERVAL_MS);
    _frameGapActive = true;
    if (sent) {
        _lastSent[slot] = value;
        _sentMask |= 1U << slot;
        _pendingMask &= static_cast<uint8_t>(~(1U << slot));
    }
    LOG_DEBUG("[DIAG] LED_TX region=%u ok=%d start_ms=%u end_ms=%u",
              41 + slot, sent ? 1 : 0, static_cast<unsigned>(startedAt),
              static_cast<unsigned>(finishedAt));
}

void LedManager::poll(uint32_t now)
{
    if (!_enabled) {
        clearSlots(0, 4);
    } else if (!_pagePending && _cache.count() != 0 &&
               LedDisplayPolicy::due(now, _nextPageAt)) {
        if (_clearInactive) {
            clearSlots(2, 4);
            _clearInactive = false; // Pending targets retain failed clears.
        }
        for (uint8_t slot = 0; slot < _regions; ++slot) {
            queueSlot(slot, _cache.slot(slot, _regions));
        }
        _pagePending = true;
        _nextPageAt = now + LedDisplayPolicy::PAGE_INTERVAL_MS;
    }
    // One frame per call. The existing LED task waits outside its business
    // guard and outside the serial mutex before polling the next frame.
    sendPending(now);
    if (_pagePending && _pendingMask == 0) {
        _cache.advance(_regions);
        _pagePending = false;
    }
}

uint32_t LedManager::waitMs(uint32_t now) const
{
    if (_pendingMask != 0) {
        return !_frameGapActive || LedDisplayPolicy::due(now, _nextFrameAt)
            ? 0 : _nextFrameAt - now;
    }
    if (!_enabled || _cache.count() == 0) return LedDisplayPolicy::PAGE_INTERVAL_MS;
    return LedDisplayPolicy::due(now, _nextPageAt) ? 0 : _nextPageAt - now;
}

bool LedManager::buildDisplayTextBySensorId(const String &sensorId, float value,
                                            const String &unit,
                                            char *buffer, size_t size)
{
    if (buffer == nullptr || size == 0)
    {
        return false;
    }
    memset(buffer, 0, size);
    if (sensorId == "a34001")
    {
        snprintf(buffer, size, "TSP:%.1fug/m3", value);
        return true;
    }
    if (sensorId == "a34004")
    {
        snprintf(buffer, size, "PM2.5:%.1fug/m3", value);
        return true;
    }
    if (sensorId == "a34005")
    {
        snprintf(buffer, size, "PM1:%.1fug/m3", value);
        return true;
    }
    if (sensorId == "a34002")
    {
        snprintf(buffer, size, "PM10:%.1fug/m3", value);
        return true;
    }
    if (sensorId == "a01007")
    {
        buildWindSpeedText(value, buffer, size);
        return true;
    }
    if (sensorId == "a01008")
    {
        buildWindDirectionText(value, buffer, size);
        return true;
    }
    if (sensorId == "a01001")
    {
        buildTemperatureText(value, buffer, size);
        return true;
    }
    if (sensorId == "a01002")
    {
        buildHumidityText(value, buffer, size);
        return true;
    }
    if (sensorId == "a01006")
    {
        buildPressureText(value, buffer, size);
        return true;
    }
    if (sensorId == "L90")
    {
        buildNoiseText(value, buffer, size);
        return true;
    }
    if (GasUnitConverter::isGasSensor(sensorId))
    {
        const char *label = sensorId == "w34011" ? "O3" :
                            sensorId == "a21004" ? "NO2" :
                            sensorId == "a21005" ? "CO" : "SO2";
        const String normalizedUnit = GasUnitConverter::normalizeUnit(sensorId, unit);
        const uint8_t decimals = GasUnitConverter::decimalsForUnit(normalizedUnit);
        snprintf(buffer, size, "%s:%.*f%s", label, decimals, value,
                 normalizedUnit.c_str());
        return true;
    }

    LOG_WARNING("Unsupported LED sensor ID: %s", sensorId.c_str());
    return false;
}

bool LedManager::sendLine(uint8_t index, const char *content)
{
    if (content == nullptr)
    {
        return false;
    }

    uint8_t packet[512] = {0};
    uint16_t packetLen = packTo485(index, content, packet, sizeof(packet));
    if (packetLen == 0)
    {
        LOG_WARNING("Failed to build 485 LED packet for %s", content);
        return false;
    }
    return sendPacket(packet, packetLen);
}
bool LedManager::sendLineLimit(uint8_t index, const char *content)
{
    if (content == nullptr)
    {
        return false;
    }

    uint8_t packet[512] = {0};
    uint16_t packetLen = packTo485Limit(index, content, packet, sizeof(packet));
    if (packetLen == 0)
    {
        LOG_WARNING("Failed to build 485 LED packet for %s", content);
        return false;
    }

    return sendPacket(packet, packetLen);
}

bool LedManager::sendPacket(const uint8_t *data, uint16_t len)
{
    if (data == nullptr || len == 0)
    {
        return false;
    }

    auto &serialManager = SerialManager::getInstance();
    size_t written = serialManager.write(SERIAL_LED, data, len);
    if (written != len)
    {
        LOG_WARNING("LED 485 write incomplete: wrote %u of %u bytes", written, len);
    }
    return written == len;
}
uint16_t LedManager::packTo485(uint8_t index,
                               const char *content,
                               uint8_t *buffer,
                               uint16_t bufferLen)
{
    if (content == nullptr || buffer == nullptr || bufferLen == 0)
    {
        return 0;
    }
    uint8_t payload[64] = {0};
    size_t contentLen = strlen(content);
    if (contentLen > 16)
    {
        contentLen = 16;
        uint8_t last = (uint8_t)content[contentLen - 1];
        if (last >= 0x80)
        {
            contentLen--;
        }
    }
    uint8_t *ptr = payload;
    *ptr++ = index;
    *ptr++ = 0x00;
    *ptr++ = 0x21;
    *ptr++ = 0xFF;
    *ptr++ = (uint8_t)contentLen;
    memcpy(ptr, content, contentLen);
    ptr += contentLen;
    uint16_t payloadLen = ptr - payload;
    uint8_t mac[8] = {0};
    return pack485Buffer(buffer,
                         RS485_UPDATE_DATA,
                         mac,
                         payload,
                         payloadLen);
}
uint16_t LedManager::packTo485Limit(uint8_t index,
                               const char *content,
                               uint8_t *buffer,
                               uint16_t bufferLen)
{
    if (content == nullptr || buffer == nullptr || bufferLen == 0)
    {
        return 0;
    }
    uint8_t payload[64] = {0};
    size_t contentLen = strlen(content);
    if (contentLen > 16)
    {
        contentLen = 16;
        uint8_t last = (uint8_t)content[contentLen - 1];
        if (last >= 0x80)
        {
            contentLen--;
        }
    }
    uint8_t *ptr = payload;
    *ptr++ = index;
    *ptr++ = 0x00;
    *ptr++ = 0xFF;
    *ptr++ = 0xFF;
    *ptr++ = (uint8_t)contentLen;
    memcpy(ptr, content, contentLen);
    ptr += contentLen;
    uint16_t payloadLen = ptr - payload;
    uint8_t mac[8] = {0};
    return pack485Buffer(buffer,
                         RS485_UPDATE_DATA,
                         mac,
                         payload,
                         payloadLen);
}
void LedManager::buildWindSpeedText(float value, char *buffer, size_t size)
{
    static const uint8_t prefix[] = {0xB7, 0xE7, 0xCB, 0xD9};
    memset(buffer, 0, size);
    memcpy(buffer, prefix, sizeof(prefix));
    snprintf(buffer + sizeof(prefix), size - sizeof(prefix), ":%.1fm/s", value);
}

void LedManager::buildWindDirectionText(float value,
                                        char *buffer,
                                        size_t size)
{
    if (buffer == nullptr || size == 0)
    {
        return;
    }
    static const uint8_t prefix[] =
        {
            0xB7, 0xE7, 0xCF, 0xF2};
    static const uint8_t north[] = {0xB1, 0xB1, 0x00, 0x00};     // 正北
    static const uint8_t northeast[] = {0xB1, 0xB1, 0xB6, 0xAB}; // 东北
    static const uint8_t east[] = {0xB6, 0xAB, 0x00, 0x00};      // 正东
    static const uint8_t southeast[] = {0xB6, 0xAB, 0xC4, 0xCF}; // 东南
    static const uint8_t south[] = {0xC4, 0xCF, 0x00, 0x00};     // 正南
    static const uint8_t southwest[] = {0xCE, 0xF7, 0xC4, 0xCF}; // 西南
    static const uint8_t west[] = {0xCE, 0xF7, 0x00, 0x00};      // 正西
    static const uint8_t northwest[] = {0xCE, 0xF7, 0xB1, 0xB1}; // 西北
    memset(buffer, 0, size);
    size_t cursor = 0;
    memcpy(buffer + cursor, prefix, sizeof(prefix));
    cursor += sizeof(prefix);
    int written = snprintf(buffer + cursor,
                           size - cursor,
                           ":%d-",
                           (int)value);

    if (written <= 0)
    {
        return;
    }
    cursor += written;
    while (value < 0)
    {
        value += 360.0f;
    }

    while (value >= 360.0f)
    {
        value -= 360.0f;
    }
    const uint8_t *direction = north;
    size_t dirLen = 4;

    if (value >= 22.5f && value < 67.5f)
    {
        direction = northeast;
    }
    else if (value >= 67.5f && value < 112.5f)
    {
        direction = east;
    }
    else if (value >= 112.5f && value < 157.5f)
    {
        direction = southeast;
    }
    else if (value >= 157.5f && value < 202.5f)
    {
        direction = south;
    }
    else if (value >= 202.5f && value < 247.5f)
    {
        direction = southwest;
    }
    else if (value >= 247.5f && value < 292.5f)
    {
        direction = west;
    }
    else if (value >= 292.5f && value < 337.5f)
    {
        direction = northwest;
    }
    
    if (cursor + dirLen < size)
    {
        memcpy(buffer + cursor, direction, dirLen);
        cursor += dirLen;
    }
    if (cursor < size)
    {
        buffer[cursor] = '\0';
    }
}

void LedManager::buildTemperatureText(float value, char *buffer, size_t size)
{
    static const uint8_t prefix[] = {0xCE, 0xC2, 0xB6, 0xC8};
    static const uint8_t celsius[] = {0xA1, 0xE6};
    memset(buffer, 0, size);
    memcpy(buffer, prefix, sizeof(prefix));

    char numeric[32] = {0};
    snprintf(numeric, sizeof(numeric), ":%.1f", value);
    strncat(buffer, numeric, size - sizeof(prefix) - 1);
    size_t cursor = strlen(buffer);
    if (cursor + sizeof(celsius) < size)
    {
        memcpy(buffer + cursor, celsius, sizeof(celsius));
    }
}

void LedManager::buildHumidityText(float value, char *buffer, size_t size)
{
    static const uint8_t prefix[] = {0xCA, 0xAA, 0xB6, 0xC8};

    memset(buffer, 0, size);
    memcpy(buffer, prefix, sizeof(prefix));
    snprintf(buffer + sizeof(prefix), size - sizeof(prefix), ":%.1f%%", value);
}

void LedManager::buildPressureText(float value, char *buffer, size_t size)
{
    static const uint8_t prefix[] = {0xB4, 0xF3, 0xC6, 0xF8, 0xD1, 0xB9}; // GBK: 大气压

    memset(buffer, 0, size);
    memcpy(buffer, prefix, sizeof(prefix));
    snprintf(buffer + sizeof(prefix), size - sizeof(prefix), ":%.1fKPa", value);
}

void LedManager::buildNoiseText(float value, char *buffer, size_t size)
{
    static const uint8_t prefix[] = {0xD4, 0xEB, 0xC9, 0xF9};

    memset(buffer, 0, size);
    memcpy(buffer, prefix, sizeof(prefix));
    snprintf(buffer + sizeof(prefix), size - sizeof(prefix), ":%.1fdB", value);
}
