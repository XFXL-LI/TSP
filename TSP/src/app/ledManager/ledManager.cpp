#include "ledManager.h"

#include <cstring>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "../../module/log/log_manager.h"

LedManager::LedManager()
    : _runLedStep(0) {}

LedManager::~LedManager() {}

LedManager &LedManager::getInstance()
{
    static LedManager instance;
    return instance;
}

void LedManager::begin()
{
    _runLedStep = 0;
    LOG_INFO("LED manager initialized on 485 transport");
}

void LedManager::updateDisplay(const AllProcessedDataPacket *packet)
{
    if (packet == nullptr)
    {
        LOG_WARNING("LED update skipped because packet is null");
        return;
    }
    if (packet->processed_data_map.empty())
    {
        LOG_WARNING("LED update skipped because processed data map is empty");
        return;
    }
    int step = 1;
    for (const auto &entry : packet->processed_data_map)
    {
        const String &sensorId = entry.first;
        const ProcessedDataPacket &processedData = entry.second;
        if (!processedData.is_valid)
        {
            continue;
        }
        char content[64] = {0};
        if (!buildDisplayTextBySensorId(sensorId, processedData.value, content, sizeof(content)))
        {
            continue;
        }
        if (step == 1)
        {
            sendLine(41, content);
            step = 0;
        } else if (step == 0)
        {
            sendLine(42, content);
            step = 1;
        }
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

bool LedManager::buildDisplayTextBySensorId(const String &sensorId, float value, char *buffer, size_t size)
{
    if (buffer == nullptr || size == 0)
    {
        return false;
    }
    memset(buffer, 0, size);
    if (sensorId == "a34001")
    {
        snprintf(buffer, size, "TSP:%.1fng/m3", value);
        return true;
    }
    if (sensorId == "a34004")
    {
        snprintf(buffer, size, "PM2.5:%.1fng/m3", value);
        return true;
    }
    if (sensorId == "a34005")
    {
        snprintf(buffer, size, "PM1:%.1fng/m3", value);
        return true;
    }
    if (sensorId == "a34002")
    {
        snprintf(buffer, size, "PM10:%.1fng/m3", value);
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
    if (sensorId == "a21026")
    {
        snprintf(buffer, size, "SO2:%.1fmg/m3", value);
        return true;
    }
    if (sensorId == "a21005")
    {
        snprintf(buffer, size, "CO:%.1fmg/m3", value);
        return true;
    }
    if (sensorId == "a21004")
    {
        snprintf(buffer, size, "NO2:%.1fmg/m3", value);
        return true;
    }
    if (sensorId == "w34011")
    {
        snprintf(buffer, size, "O3:%.1fmg/m3", value);
        return true;
    }

    LOG_WARNING("Unsupported LED sensor ID: %s", sensorId.c_str());
    return false;
}

void LedManager::sendLine(uint8_t index, const char *content)
{
    if (content == nullptr)
    {
        return;
    }

    uint8_t packet[512] = {0};
    uint16_t packetLen = packTo485(index, content, packet, sizeof(packet));
    if (packetLen == 0)
    {
        LOG_WARNING("Failed to build 485 LED packet for %s", content);
        return;
    }

    sendPacket(packet, packetLen);
}

void LedManager::sendPacket(const uint8_t *data, uint16_t len)
{
    if (data == nullptr || len == 0)
    {
        return;
    }

    auto &serialManager = SerialManager::getInstance();
    size_t written = serialManager.write(SERIAL_LED, data, len);
    if (written != len)
    {
        LOG_WARNING("LED 485 write incomplete: wrote %u of %u bytes", written, len);
    }
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
    static const uint8_t prefix[] = {0xD1, 0xB9, 0xC7, 0xBF};

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
