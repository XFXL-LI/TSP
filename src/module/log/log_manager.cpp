#include "log_manager.h"

#include <Arduino.h>
#include <stdarg.h>

static constexpr uint32_t LOG_SERIAL_BAUD_RATE = 115200;

LogManager::LogManager()
    : _currentLevel(LOG_LEVEL_INFO),
      _currentTarget(LOG_TARGET_SERIAL0),
      _outputMutex(xSemaphoreCreateMutex())
{
    Serial.begin(LOG_SERIAL_BAUD_RATE);
    Serial.println("LOG manager Init!");
}

LogManager::~LogManager()
{
    if (_outputMutex != nullptr)
    {
        vSemaphoreDelete(_outputMutex);
        _outputMutex = nullptr;
    }
}

const char *LogManager::_levelToString(LogLevel level)
{
    switch (level)
    {
    case LOG_LEVEL_DEBUG:
        return "DEBUG";
    case LOG_LEVEL_INFO:
        return "INFO";
    case LOG_LEVEL_WARNING:
        return "WARNING";
    case LOG_LEVEL_ERROR:
        return "ERROR";
    case LOG_LEVEL_CRITICAL:
        return "CRITICAL";
    default:
        return "UNKNOWN";
    }
}

void LogManager::_output(const char *formattedMessage)
{
    if (_currentTarget == LOG_TARGET_SERIAL0)
    {
        // The message is already formatted; never treat runtime text as a
        // printf format string.
        Serial.print(formattedMessage);
    }
    else if (_currentTarget == LOG_TARGET_SERIAL5)
    {
        // Serial5.print(formattedMessage);
    }
}

void LogManager::log(LogLevel level, const char *message)
{
    if (level < _currentLevel)
    {
        return;
    }
    if (_outputMutex != nullptr)
    {
        xSemaphoreTake(_outputMutex, portMAX_DELAY);
    }

    char buffer[256];
    snprintf(buffer, sizeof(buffer), "[%s] %s\n", _levelToString(level), message);
    _output(buffer);

    if (_outputMutex != nullptr)
    {
        xSemaphoreGive(_outputMutex);
    }
}

void LogManager::printf(LogLevel level, const char *format, ...)
{
    if (level < _currentLevel)
    {
        return;
    }
    if (_outputMutex != nullptr)
    {
        xSemaphoreTake(_outputMutex, portMAX_DELAY);
    }

    char header[32];
    snprintf(header, sizeof(header), "[%s] ", _levelToString(level));
    _output(header);

    char body[256];
    va_list args;
    va_start(args, format);
    vsnprintf(body, sizeof(body), format, args);
    va_end(args);
    _output(body);
    _output("\n");

    if (_outputMutex != nullptr)
    {
        xSemaphoreGive(_outputMutex);
    }
}
