#pragma once
#ifndef GAS_CALIBRATION_MANAGER_H
#define GAS_CALIBRATION_MANAGER_H

#include <Arduino.h>
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "../modbus/modbus_manager.h"

class GasCalibrationManager
{
public:
    static GasCalibrationManager &getInstance();

    bool begin();
    void tick();
    bool handleRequest(const char *json, char *response, size_t responseCapacity);

    bool isMaintenanceActive() const;
    void setNormalCollectionPending(bool pending);

private:
    enum { PROFILE_COUNT = 4, SAMPLE_CAPACITY = 10 };

    enum class State : uint8_t {
        IDLE,
        ZERO_WAIT,
        ZERO_EXECUTING,
        SPAN_WAIT,
        SPAN_EXECUTING,
        PURGING,
        CANCEL_PURGING,
        TIMED_OUT,
        FAILED
    };

    enum class Point : uint8_t { NONE, ZERO, SPAN };

    enum class PointResult : uint8_t {
        NONE,
        PENDING,
        SUCCESS,
        MODULE_FAILED,
        UNSUPPORTED,
        INVALID_PARAMETER,
        MODULE_TIMEOUT,
        STATUS_READ_FAILED,
        UNLOCK_FAILED,
        WRITE_FAILED
    };

    enum class IoResult : uint8_t {
        OK,
        DEFERRED,
        READ_FAILED,
        UNLOCK_FAILED,
        WRITE_FAILED
    };

    struct Profile {
        const char *id;
        uint8_t slave;
        uint16_t maxTargetPpb;
    };

    static const Profile PROFILES[PROFILE_COUNT];

    GasCalibrationManager();
    GasCalibrationManager(const GasCalibrationManager &) = delete;
    GasCalibrationManager &operator=(const GasCalibrationManager &) = delete;

    bool loadConfiguration();
    bool parseConfiguration(const String &json);
    bool createDefaultConfiguration();
    int findProfile(const char *id) const;

    void resetSessionLocked();
    void resetSamplesLocked();
    void addSampleLocked(uint16_t value, uint32_t nowMs);
    bool sampleWindowReadyLocked() const;
    bool sampleAverageLocked(uint16_t &average) const;
    void updateStableHintLocked();

    IoResult executeCalibrationCommand(uint8_t slave, Point point,
                                       uint16_t targetPpb);
    IoResult readRegister(uint8_t slave, uint16_t address, uint16_t &value);

    bool writeError(char *response, size_t capacity,
                    const char *action, const char *reason) const;
    bool writeSimpleSuccessLocked(char *response, size_t capacity,
                                  const char *action, const char *state) const;
    bool writeStateResponseLocked(char *response, size_t capacity,
                                  const char *action) const;

    static const char *stateName(State state);
    static const char *pointName(Point point);
    static const char *pointResultName(PointResult result);

    modbus_manager _modbus;
    Stream *_port;
    SemaphoreHandle_t _stateMutex;
    std::atomic<bool> _maintenanceActive;
    std::atomic<bool> _normalCollectionPending;

    bool _initialized;
    bool _configValid;
    uint16_t _spanTargets[PROFILE_COUNT];
    uint8_t _maxDeviationPercent;
    uint8_t _stabilityTolerancePercent;

    State _state;
    Point _point;
    PointResult _pointResult;
    uint8_t _profileIndex;
    uint16_t _sessionTargetPpb;
    uint32_t _session;
    uint32_t _nextSession;
    uint32_t _sessionStartedMs;
    uint32_t _lastHeartbeatMs;

    bool _hasCurrent;
    uint16_t _currentPpb;
    uint32_t _lastReadMs;
    uint32_t _lastConcentrationAttemptMs;
    uint16_t _samples[SAMPLE_CAPACITY];
    uint32_t _sampleTimes[SAMPLE_CAPACITY];
    uint8_t _sampleCount;
    uint8_t _sampleNext;
    bool _stableHint;

    bool _commandIssued;
    uint32_t _commandStartedMs;
    uint32_t _lastStatusAttemptMs;
    uint8_t _statusReadFailures;
    bool _hasModuleStatus;
    uint16_t _moduleStatus;
    bool _busWaiting;
    uint32_t _lastBusDeferLogMs;
};

#endif
