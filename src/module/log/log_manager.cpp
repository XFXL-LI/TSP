#include "log_manager.h"
#include <stdarg.h>
#include <Arduino.h>

// 构造函数：设置默认值
LogManager::LogManager() : _currentLevel(LOG_LEVEL_INFO), _currentTarget(LOG_TARGET_SERIAL0)
{
    Serial.begin(9600);
    Serial.println("LOG manager Init!");
}

LogManager::~LogManager() {}

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
        Serial.printf(formattedMessage);
    }
    else if (_currentTarget == LOG_TARGET_SERIAL5)
    {
        // Serial5.print(formattedMessage);
    }
}

void LogManager::log(LogLevel level, const char *message)
{
    if (level >= _currentLevel)
    {
        char buffer[256];
        snprintf(buffer, sizeof(buffer), "[%s] %s\n", _levelToString(level), message);
        _output(buffer);
    }
}

void LogManager::printf(LogLevel level, const char *format, ...)
{
    if (level >= _currentLevel)
    {
        char header[32];
        snprintf(header, sizeof(header), "[%s] ", _levelToString(level));
        _output(header);

        char body[256];
        va_list args;
        va_start(args, format);
        vsnprintf(body, sizeof(body), format, args);
        va_end(args);

        _output(body);
        _output("\n"); // 自动换行
    }
}