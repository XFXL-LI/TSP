#pragma once
#ifndef LOG_MANAGER_H
#define LOG_MANAGER_H

#include <Arduino.h>

// 日志级别枚举
enum LogLevel
{
    LOG_LEVEL_DEBUG = 0,
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARNING,
    LOG_LEVEL_ERROR,
    LOG_LEVEL_CRITICAL
};

// 日志输出目标枚举
enum LogTarget
{
    LOG_TARGET_SERIAL0 = 0,
    LOG_TARGET_SERIAL5
};

class LogManager
{
public:
    static LogManager &getInstance()
    {
        static LogManager instance;
        return instance;
    }

    LogManager(const LogManager &) = delete;
    LogManager &operator=(const LogManager &) = delete;

    void setLevel(LogLevel level) { _currentLevel = level; }
    void setTarget(LogTarget target) { _currentTarget = target; }
    LogLevel getLevel() const { return _currentLevel; }

    void log(LogLevel level, const char *message);

    void printf(LogLevel level, const char *format, ...);

private:
    LogManager();
    ~LogManager();

    LogLevel _currentLevel;
    LogTarget _currentTarget;

    const char *_levelToString(LogLevel level);
    void _output(const char *formattedMessage);
};

#define LOG_DEBUG(fmt, ...)    LogManager::getInstance().printf(LOG_LEVEL_DEBUG, fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...)     LogManager::getInstance().printf(LOG_LEVEL_INFO, fmt, ##__VA_ARGS__)
#define LOG_WARNING(fmt, ...)  LogManager::getInstance().printf(LOG_LEVEL_WARNING, fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...)    LogManager::getInstance().printf(LOG_LEVEL_ERROR, fmt, ##__VA_ARGS__)
#define LOG_CRITICAL(fmt, ...) LogManager::getInstance().printf(LOG_LEVEL_CRITICAL, fmt, ##__VA_ARGS__)

#endif