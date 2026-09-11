#include "GasCalibrationManager.h"
#include "GasModbusAccess.h"
#include "../Serial/SerialManager.h"
#include "../file/file_storage.h"
#include "../log/log_manager.h"
#include <FFat.h>
#include <cJSON.h>
#include <stdarg.h>
#include <string.h>

namespace {
constexpr const char *CONFIG_PATH = "/gasCalibration.json";
constexpr const char *DEFAULT_CONFIG_JSON =
    "{\"span_targets_ppb\":{\"w34011\":500,\"a21004\":500,"
    "\"a21005\":5000,\"a21026\":500},\"max_deviation_percent\":20,"
    "\"stability_window_seconds\":20,\"min_stability_samples\":10,"
    "\"stability_tolerance_percent\":5}";

constexpr uint32_t CONCENTRATION_INTERVAL_MS = 2000;
constexpr uint32_t STATUS_INTERVAL_MS = 1000;
constexpr uint32_t CURRENT_FRESH_MS = 5000;
constexpr uint32_t HEARTBEAT_TIMEOUT_MS = 10000;
constexpr uint32_t SESSION_TIMEOUT_MS = 30UL * 60UL * 1000UL;
constexpr uint32_t COMMAND_TIMEOUT_MS = 120UL * 1000UL;
constexpr uint32_t COMPLETE_WINDOW_MAX_MS = 22000;

bool appendJson(char *buffer, size_t capacity, size_t &used,
                const char *format, ...)
{
    if (buffer == nullptr || capacity == 0 || used >= capacity) return false;
    va_list args;
    va_start(args, format);
    int written = vsnprintf(buffer + used, capacity - used, format, args);
    va_end(args);
    if (written < 0 || static_cast<size_t>(written) >= capacity - used) {
        buffer[capacity - 1] = '\0';
        return false;
    }
    used += static_cast<size_t>(written);
    return true;
}

bool readUnsigned(cJSON *parent, const char *name, uint32_t &value)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(parent, name);
    if (!cJSON_IsNumber(item) || item->valuedouble < 0.0 ||
        item->valuedouble > 4294967295.0) {
        return false;
    }
    uint32_t parsed = static_cast<uint32_t>(item->valuedouble);
    if (static_cast<double>(parsed) != item->valuedouble) return false;
    value = parsed;
    return true;
}

}

const GasCalibrationManager::Profile GasCalibrationManager::PROFILES[PROFILE_COUNT] = {
    {"w34011", 3, 1000},
    {"a21004", 4, 1000},
    {"a21005", 5, 10000},
    {"a21026", 6, 1000}
};

GasCalibrationManager &GasCalibrationManager::getInstance()
{
    static GasCalibrationManager instance;
    return instance;
}

GasCalibrationManager::GasCalibrationManager()
    : _port(nullptr),
      _stateMutex(xSemaphoreCreateMutex()),
      _maintenanceActive(false),
      _normalCollectionPending(false),
      _initialized(false),
      _configValid(false),
      _maxDeviationPercent(20),
      _stabilityTolerancePercent(5),
      _state(State::IDLE),
      _point(Point::NONE),
      _pointResult(PointResult::NONE),
      _profileIndex(0),
      _sessionTargetPpb(0),
      _session(0),
      _nextSession(1),
      _sessionStartedMs(0),
      _lastHeartbeatMs(0),
      _hasCurrent(false),
      _currentPpb(0),
      _lastReadMs(0),
      _lastConcentrationAttemptMs(0),
      _sampleCount(0),
      _sampleNext(0),
      _stableHint(false),
      _commandIssued(false),
      _commandStartedMs(0),
      _lastStatusAttemptMs(0),
      _statusReadFailures(0),
      _hasModuleStatus(false),
      _moduleStatus(0),
      _busWaiting(false),
      _lastBusDeferLogMs(0)
{
    memset(_spanTargets, 0, sizeof(_spanTargets));
    memset(_samples, 0, sizeof(_samples));
    memset(_sampleTimes, 0, sizeof(_sampleTimes));
}

bool GasCalibrationManager::begin()
{
    if (_initialized) return _configValid;

    _port = SerialManager::getInstance().getStream(SERIAL_485);
    if (_port == nullptr || _stateMutex == nullptr) {
        LOG_ERROR("GAS_CAL_INIT failed: 485 stream or state mutex unavailable");
        return false;
    }

    _modbus.modbus_init(_port);
    _configValid = loadConfiguration();
    _initialized = true;
    LOG_INFO("GAS_CAL_INIT config=%s", _configValid ? "valid" : "invalid");
    return _configValid;
}

bool GasCalibrationManager::loadConfiguration()
{
    file_storage &storage = file_storage::getInstance();
    String content;
    int readResult = storage.readFFAT(CONFIG_PATH, content);

    if (readResult != 0) {
        const String backupPath = String(CONFIG_PATH) + ".bak";
        if (!FFat.exists(CONFIG_PATH) && FFat.exists(backupPath.c_str()) &&
            FFat.rename(backupPath.c_str(), CONFIG_PATH)) {
            LOG_WARNING("GAS_CAL_CONFIG restored atomic backup");
            readResult = storage.readFFAT(CONFIG_PATH, content);
        }
    }

    if (readResult == 2) {
        if (!createDefaultConfiguration()) return false;
        content = DEFAULT_CONFIG_JSON;
    } else if (readResult != 0) {
        LOG_ERROR("GAS_CAL_CONFIG read failed code=%d", readResult);
        return false;
    }

    if (!parseConfiguration(content)) {
        LOG_ERROR("GAS_CAL_CONFIG invalid; calibration start disabled");
        return false;
    }

    const String backupPath = String(CONFIG_PATH) + ".bak";
    if (FFat.exists(backupPath.c_str())) FFat.remove(backupPath.c_str());
    LOG_INFO("GAS_CAL_CONFIG path=%s targets_ppb=%u,%u,%u,%u deviation=%u stability=%u",
             CONFIG_PATH,
             _spanTargets[0], _spanTargets[1], _spanTargets[2], _spanTargets[3],
             _maxDeviationPercent, _stabilityTolerancePercent);
    return true;
}

bool GasCalibrationManager::createDefaultConfiguration()
{
    int result = file_storage::getInstance().writeFFATAtomic(
        CONFIG_PATH, DEFAULT_CONFIG_JSON);
    if (result != 0) {
        LOG_ERROR("GAS_CAL_CONFIG default create failed code=%d", result);
        return false;
    }
    return true;
}

bool GasCalibrationManager::parseConfiguration(const String &json)
{
    cJSON *root = cJSON_Parse(json.c_str());
    if (root == nullptr || !cJSON_IsObject(root)) {
        if (root != nullptr) cJSON_Delete(root);
        return false;
    }

    cJSON *targets = cJSON_GetObjectItemCaseSensitive(root, "span_targets_ppb");
    uint16_t parsedTargets[PROFILE_COUNT] = {};
    bool valid = cJSON_IsObject(targets);
    for (uint8_t i = 0; valid && i < PROFILE_COUNT; ++i) {
        uint32_t value = 0;
        valid = readUnsigned(targets, PROFILES[i].id, value) &&
                value > 0 && value <= PROFILES[i].maxTargetPpb;
        if (valid) parsedTargets[i] = static_cast<uint16_t>(value);
    }

    uint32_t deviation = 20;
    uint32_t windowSeconds = 20;
    uint32_t minSamples = 10;
    uint32_t stabilityTolerance = 5;
    cJSON *item = cJSON_GetObjectItemCaseSensitive(root, "max_deviation_percent");
    if (item != nullptr) valid = valid && readUnsigned(root, "max_deviation_percent", deviation);
    item = cJSON_GetObjectItemCaseSensitive(root, "stability_window_seconds");
    if (item != nullptr) valid = valid && readUnsigned(root, "stability_window_seconds", windowSeconds);
    item = cJSON_GetObjectItemCaseSensitive(root, "min_stability_samples");
    if (item != nullptr) valid = valid && readUnsigned(root, "min_stability_samples", minSamples);
    item = cJSON_GetObjectItemCaseSensitive(root, "stability_tolerance_percent");
    if (item != nullptr) valid = valid && readUnsigned(root, "stability_tolerance_percent", stabilityTolerance);

    valid = valid && deviation >= 1 && deviation <= 50 &&
            windowSeconds == 20 && minSamples == SAMPLE_CAPACITY &&
            stabilityTolerance >= 1 && stabilityTolerance <= 20;

    if (valid) {
        memcpy(_spanTargets, parsedTargets, sizeof(_spanTargets));
        _maxDeviationPercent = static_cast<uint8_t>(deviation);
        _stabilityTolerancePercent = static_cast<uint8_t>(stabilityTolerance);
    }
    cJSON_Delete(root);
    return valid;
}

int GasCalibrationManager::findProfile(const char *id) const
{
    if (id == nullptr) return -1;
    for (uint8_t i = 0; i < PROFILE_COUNT; ++i) {
        if (strcmp(id, PROFILES[i].id) == 0) return i;
    }
    return -1;
}

bool GasCalibrationManager::isMaintenanceActive() const
{
    return _maintenanceActive.load(std::memory_order_acquire);
}

void GasCalibrationManager::setNormalCollectionPending(bool pending)
{
    _normalCollectionPending.store(pending, std::memory_order_release);
}

void GasCalibrationManager::resetSamplesLocked()
{
    memset(_samples, 0, sizeof(_samples));
    memset(_sampleTimes, 0, sizeof(_sampleTimes));
    _sampleCount = 0;
    _sampleNext = 0;
    _stableHint = false;
}

void GasCalibrationManager::resetSessionLocked()
{
    _state = State::IDLE;
    _point = Point::NONE;
    _pointResult = PointResult::NONE;
    _profileIndex = 0;
    _sessionTargetPpb = 0;
    _session = 0;
    _sessionStartedMs = 0;
    _lastHeartbeatMs = 0;
    _hasCurrent = false;
    _currentPpb = 0;
    _lastReadMs = 0;
    _lastConcentrationAttemptMs = 0;
    resetSamplesLocked();
    _commandIssued = false;
    _commandStartedMs = 0;
    _lastStatusAttemptMs = 0;
    _statusReadFailures = 0;
    _hasModuleStatus = false;
    _moduleStatus = 0;
    _busWaiting = false;
}

void GasCalibrationManager::addSampleLocked(uint16_t value, uint32_t nowMs)
{
    if (_sampleCount > 0 && nowMs - _lastReadMs > 4500) {
        resetSamplesLocked();
    }
    _lastReadMs = nowMs;
    _samples[_sampleNext] = value;
    _sampleTimes[_sampleNext] = nowMs;
    _sampleNext = static_cast<uint8_t>((_sampleNext + 1) % SAMPLE_CAPACITY);
    if (_sampleCount < SAMPLE_CAPACITY) ++_sampleCount;
    updateStableHintLocked();
}

bool GasCalibrationManager::sampleWindowReadyLocked() const
{
    if (_sampleCount < SAMPLE_CAPACITY) return false;
    const uint8_t oldest = _sampleNext;
    return _lastReadMs - _sampleTimes[oldest] <= COMPLETE_WINDOW_MAX_MS;
}

bool GasCalibrationManager::sampleAverageLocked(uint16_t &average) const
{
    if (!sampleWindowReadyLocked()) return false;
    uint32_t sum = 0;
    for (uint8_t i = 0; i < SAMPLE_CAPACITY; ++i) sum += _samples[i];
    average = static_cast<uint16_t>((sum + SAMPLE_CAPACITY / 2) / SAMPLE_CAPACITY);
    return true;
}

void GasCalibrationManager::updateStableHintLocked()
{
    if (!sampleWindowReadyLocked() || _sessionTargetPpb == 0) {
        _stableHint = false;
        return;
    }
    uint16_t minValue = _samples[0];
    uint16_t maxValue = _samples[0];
    for (uint8_t i = 1; i < SAMPLE_CAPACITY; ++i) {
        if (_samples[i] < minValue) minValue = _samples[i];
        if (_samples[i] > maxValue) maxValue = _samples[i];
    }
    const uint32_t range = static_cast<uint32_t>(maxValue - minValue);
    _stableHint = range * 100UL <=
                  static_cast<uint32_t>(_sessionTargetPpb) * _stabilityTolerancePercent;
}

bool GasCalibrationManager::writeError(char *response, size_t capacity,
                                       const char *action, const char *reason) const
{
    if (response == nullptr || capacity == 0) return false;
    int written = snprintf(response, capacity,
        "{\"operation\":\"gas_calibration\",\"code\":\"NG\",\"action\":\"%s\",\"reason\":\"%s\"}",
        action != nullptr ? action : "", reason != nullptr ? reason : "invalid_request");
    return written >= 0 && static_cast<size_t>(written) < capacity;
}

bool GasCalibrationManager::writeSimpleSuccessLocked(char *response, size_t capacity,
                                                      const char *action,
                                                      const char *state) const
{
    int written = snprintf(response, capacity,
        "{\"operation\":\"gas_calibration\",\"code\":\"OK\",\"action\":\"%s\",\"session\":%u,\"state\":\"%s\"}",
        action, (unsigned)_session, state);
    return written >= 0 && static_cast<size_t>(written) < capacity;
}

bool GasCalibrationManager::writeStateResponseLocked(char *response, size_t capacity,
                                                      const char *action) const
{
    size_t used = 0;
    const uint32_t ageMs = _hasCurrent ? millis() - _lastReadMs : 0;
    const bool fresh = _hasCurrent && ageMs <= CURRENT_FRESH_MS;
    bool ok = appendJson(response, capacity, used,
        "{\"operation\":\"gas_calibration\",\"code\":\"OK\","
        "\"action\":\"%s\",\"session\":%u,\"id\":\"%s\","
        "\"state\":\"%s\",\"current_ppb\":",
        action, (unsigned)_session, PROFILES[_profileIndex].id, stateName(_state));
    if (ok) {
        ok = _hasCurrent
            ? appendJson(response, capacity, used, "%u", _currentPpb)
            : appendJson(response, capacity, used, "null");
    }
    ok = ok && appendJson(response, capacity, used,
        ",\"fresh\":%s,\"age_ms\":%u,\"stable_hint\":%s,"
        "\"sample_count\":%u,\"point\":\"%s\",\"point_result\":\"%s\","
        "\"module_status\":",
        fresh ? "true" : "false", (unsigned)ageMs,
        (_stableHint && fresh) ? "true" : "false", _sampleCount,
        pointName(_point), pointResultName(_pointResult));
    if (ok) {
        ok = _hasModuleStatus
            ? appendJson(response, capacity, used, "%u", _moduleStatus)
            : appendJson(response, capacity, used, "null");
    }
    ok = ok && appendJson(response, capacity, used,
        ",\"bus_waiting\":%s}", _busWaiting ? "true" : "false");
    return ok;
}

bool GasCalibrationManager::handleRequest(const char *json, char *response,
                                          size_t responseCapacity)
{
    if (response == nullptr || responseCapacity == 0) return false;
    response[0] = '\0';

    cJSON *root = json != nullptr ? cJSON_Parse(json) : nullptr;
    if (root == nullptr || !cJSON_IsObject(root)) {
        if (root != nullptr) cJSON_Delete(root);
        return writeError(response, responseCapacity, "", "invalid_json");
    }
    cJSON *actionItem = cJSON_GetObjectItemCaseSensitive(root, "action");
    const char *action = cJSON_IsString(actionItem) ? actionItem->valuestring : nullptr;
    if (action == nullptr || action[0] == '\0') {
        cJSON_Delete(root);
        return writeError(response, responseCapacity, "", "invalid_action");
    }

    if (!_initialized) {
        bool result = writeError(response, responseCapacity, action, "service_unavailable");
        cJSON_Delete(root);
        return result;
    }
    if (xSemaphoreTake(_stateMutex, pdMS_TO_TICKS(50)) != pdTRUE) {
        bool result = writeError(response, responseCapacity, action, "manager_busy");
        cJSON_Delete(root);
        return result;
    }

    bool result = false;
    const uint32_t nowMs = millis();
    if (strcmp(action, "start") == 0) {
        cJSON *idItem = cJSON_GetObjectItemCaseSensitive(root, "id");
        const char *id = cJSON_IsString(idItem) ? idItem->valuestring : nullptr;
        const int profile = findProfile(id);
        if (profile < 0) {
            result = writeError(response, responseCapacity, action, "invalid_sensor_id");
        } else if (!_configValid) {
            result = writeError(response, responseCapacity, action, "calibration_config_invalid");
        } else if (_maintenanceActive.load(std::memory_order_acquire)) {
            if (_profileIndex == static_cast<uint8_t>(profile)) {
                _lastHeartbeatMs = nowMs;
                result = writeStateResponseLocked(response, responseCapacity, "resume");
            } else {
                result = writeError(response, responseCapacity, action, "session_busy");
            }
        } else {
            resetSessionLocked();
            _profileIndex = static_cast<uint8_t>(profile);
            _sessionTargetPpb = _spanTargets[_profileIndex];
            _session = _nextSession++;
            if (_session == 0 || _nextSession == 0) {
                _session = 1;
                _nextSession = 2;
            }
            _sessionStartedMs = nowMs;
            _lastHeartbeatMs = nowMs;
            _state = State::ZERO_WAIT;
            _maintenanceActive.store(true, std::memory_order_release);
            LOG_INFO("GAS_CAL_SESSION action=start session=%u id=%s",
                     (unsigned)_session, PROFILES[_profileIndex].id);
            result = writeStateResponseLocked(response, responseCapacity, "start");
        }
    } else {
        uint32_t requestedSession = 0;
        const bool sessionValid = readUnsigned(root, "session", requestedSession);
        if (!sessionValid || !_maintenanceActive.load(std::memory_order_acquire) ||
            requestedSession != _session) {
            result = writeError(response, responseCapacity, action, "invalid_session");
        } else if (strcmp(action, "status") == 0) {
            _lastHeartbeatMs = nowMs;
            result = writeStateResponseLocked(response, responseCapacity, action);
        } else if (strcmp(action, "zero") == 0) {
            _lastHeartbeatMs = nowMs;
            const bool retry = _state == State::FAILED && _point == Point::ZERO;
            const bool fresh = _hasCurrent && nowMs - _lastReadMs <= CURRENT_FRESH_MS;
            if (_state != State::ZERO_WAIT && !retry) {
                result = writeError(response, responseCapacity, action, "invalid_state");
            } else if (!fresh) {
                result = writeError(response, responseCapacity, action, "concentration_unavailable");
            } else {
                _state = State::ZERO_EXECUTING;
                _point = Point::ZERO;
                _pointResult = PointResult::PENDING;
                _commandIssued = false;
                _hasModuleStatus = false;
                _statusReadFailures = 0;
                _busWaiting = false;
                resetSamplesLocked();
                result = writeSimpleSuccessLocked(response, responseCapacity,
                                                  action, stateName(_state));
            }
        } else if (strcmp(action, "span") == 0) {
            _lastHeartbeatMs = nowMs;
            cJSON *forbidden = cJSON_GetObjectItemCaseSensitive(root, "target_ppb");
            if (forbidden == nullptr) forbidden = cJSON_GetObjectItemCaseSensitive(root, "current_ppb");
            if (forbidden == nullptr) forbidden = cJSON_GetObjectItemCaseSensitive(root, "DATA");
            if (forbidden == nullptr) forbidden = cJSON_GetObjectItemCaseSensitive(root, "RATIO");
            const bool retry = _state == State::FAILED && _point == Point::SPAN;
            const bool fresh = _hasCurrent && nowMs - _lastReadMs <= CURRENT_FRESH_MS;
            uint16_t average = 0;
            if (forbidden != nullptr) {
                result = writeError(response, responseCapacity, action, "request_parameter_not_allowed");
            } else if (_state != State::SPAN_WAIT && !retry) {
                result = writeError(response, responseCapacity, action, "invalid_state");
            } else if (!fresh) {
                result = writeError(response, responseCapacity, action, "concentration_unavailable");
            } else if (!sampleAverageLocked(average)) {
                result = writeError(response, responseCapacity, action, "insufficient_samples");
            } else {
                const uint32_t difference = average > _sessionTargetPpb
                    ? average - _sessionTargetPpb : _sessionTargetPpb - average;
                if (difference * 100UL >
                    static_cast<uint32_t>(_sessionTargetPpb) * _maxDeviationPercent) {
                    result = writeError(response, responseCapacity, action, "concentration_not_ready");
                } else {
                    _state = State::SPAN_EXECUTING;
                    _point = Point::SPAN;
                    _pointResult = PointResult::PENDING;
                    _commandIssued = false;
                    _hasModuleStatus = false;
                    _statusReadFailures = 0;
                    _busWaiting = false;
                    resetSamplesLocked();
                    result = writeSimpleSuccessLocked(response, responseCapacity,
                                                      action, stateName(_state));
                }
            }
        } else if (strcmp(action, "cancel") == 0) {
            _lastHeartbeatMs = nowMs;
            if (_state == State::CANCEL_PURGING) {
                result = writeSimpleSuccessLocked(response, responseCapacity,
                                                  action, stateName(_state));
            } else if (_state == State::PURGING) {
                result = writeError(response, responseCapacity, action, "invalid_state");
            } else {
                _state = State::CANCEL_PURGING;
                _pointResult = PointResult::NONE;
                _commandIssued = false;
                _busWaiting = false;
                resetSamplesLocked();
                LOG_INFO("GAS_CAL_SESSION action=cancel session=%u id=%s",
                         (unsigned)_session, PROFILES[_profileIndex].id);
                result = writeSimpleSuccessLocked(response, responseCapacity,
                                                  action, stateName(_state));
            }
        } else if (strcmp(action, "finish") == 0) {
            _lastHeartbeatMs = nowMs;
            if (_state != State::PURGING && _state != State::CANCEL_PURGING &&
                _state != State::TIMED_OUT) {
                result = writeError(response, responseCapacity, action, "invalid_state");
            } else {
                const uint32_t completedSession = _session;
                result = writeSimpleSuccessLocked(response, responseCapacity,
                                                  action, "completed");
                LOG_INFO("GAS_CAL_SESSION action=finish session=%u id=%s",
                         (unsigned)completedSession, PROFILES[_profileIndex].id);
                resetSessionLocked();
                _maintenanceActive.store(false, std::memory_order_release);
                LOG_INFO("GAS_CAL_MAINTENANCE active=0");
            }
        } else {
            result = writeError(response, responseCapacity, action, "invalid_action");
        }
    }

    xSemaphoreGive(_stateMutex);
    cJSON_Delete(root);
    return result;
}

GasCalibrationManager::IoResult GasCalibrationManager::executeCalibrationCommand(
    uint8_t slave, Point point, uint16_t targetPpb)
{
    if (_normalCollectionPending.load(std::memory_order_acquire)) return IoResult::DEFERRED;
    SemaphoreHandle_t busMutex = SerialManager::getInstance().getMutex(SERIAL_485);
    if (busMutex == nullptr) return IoResult::WRITE_FAILED;
    if (xSemaphoreTake(busMutex, pdMS_TO_TICKS(50)) != pdTRUE) return IoResult::DEFERRED;
    if (_normalCollectionPending.load(std::memory_order_acquire)) {
        xSemaphoreGive(busMutex);
        return IoResult::DEFERRED;
    }

    uint16_t unlock = 0x55AA;
    if (!GasModbusAccess::writeRegisters(
            _modbus, slave, 0x4FFF, 1, &unlock, 1, 500, "cal_unlock")) {
        xSemaphoreGive(busMutex);
        return IoResult::UNLOCK_FAILED;
    }

    vTaskDelay(pdMS_TO_TICKS(1000));
    uint16_t command[2] = {
        point == Point::ZERO ? static_cast<uint16_t>(0x1000)
                             : static_cast<uint16_t>(0x1001),
        point == Point::ZERO ? static_cast<uint16_t>(0) : targetPpb
    };
    const bool writeOk = GasModbusAccess::writeRegisters(
        _modbus, slave, 0x6006, 2, command, 1, 500, "cal_command");
    xSemaphoreGive(busMutex);
    return writeOk ? IoResult::OK : IoResult::WRITE_FAILED;
}

GasCalibrationManager::IoResult GasCalibrationManager::readRegister(
    uint8_t slave, uint16_t address, uint16_t &value)
{
    if (_normalCollectionPending.load(std::memory_order_acquire)) return IoResult::DEFERRED;
    SemaphoreHandle_t busMutex = SerialManager::getInstance().getMutex(SERIAL_485);
    if (busMutex == nullptr) return IoResult::READ_FAILED;
    if (xSemaphoreTake(busMutex, pdMS_TO_TICKS(20)) != pdTRUE) return IoResult::DEFERRED;
    if (_normalCollectionPending.load(std::memory_order_acquire)) {
        xSemaphoreGive(busMutex);
        return IoResult::DEFERRED;
    }
    const bool ok = GasModbusAccess::readRegisters(
        _modbus, slave, address, 1, &value, 1, 500, "calibration");
    xSemaphoreGive(busMutex);
    return ok ? IoResult::OK : IoResult::READ_FAILED;
}

void GasCalibrationManager::tick()
{
    if (!_initialized || !_maintenanceActive.load(std::memory_order_acquire)) return;

    const uint32_t nowMs = millis();
    if (xSemaphoreTake(_stateMutex, pdMS_TO_TICKS(20)) != pdTRUE) return;

    // finish may reset the session after the lock-free maintenance check above.
    // Revalidate under the state lock before evaluating session timestamps.
    if (!_maintenanceActive.load(std::memory_order_acquire) || _session == 0) {
        xSemaphoreGive(_stateMutex);
        return;
    }

    if (nowMs - _sessionStartedMs >= SESSION_TIMEOUT_MS &&
        _state != State::TIMED_OUT) {
        _state = State::TIMED_OUT;
        _commandIssued = false;
        _busWaiting = false;
        LOG_WARNING("GAS_CAL_TIMEOUT session=%u id=%s",
                    (unsigned)_session, PROFILES[_profileIndex].id);
        xSemaphoreGive(_stateMutex);
        return;
    }

    const uint32_t session = _session;
    const uint8_t profileIndex = _profileIndex;
    const State state = _state;
    const Point point = _point;

    bool doCommand = false;
    bool doStatusRead = false;
    bool doConcentrationRead = false;

    if (state == State::ZERO_EXECUTING || state == State::SPAN_EXECUTING) {
        if (!_commandIssued) {
            _commandIssued = true;
            doCommand = true;
        } else if (nowMs - _commandStartedMs >= COMMAND_TIMEOUT_MS) {
            _state = State::FAILED;
            _pointResult = PointResult::MODULE_TIMEOUT;
            _commandIssued = false;
            _busWaiting = false;
            LOG_ERROR("GAS_CAL_RESULT point=%s success=0 reason=command_timeout",
                      pointName(_point));
        } else if (nowMs - _lastStatusAttemptMs >= STATUS_INTERVAL_MS) {
            _lastStatusAttemptMs = nowMs;
            doStatusRead = true;
        }
    } else {
        const bool readableState = state == State::ZERO_WAIT ||
            state == State::SPAN_WAIT || state == State::PURGING ||
            state == State::CANCEL_PURGING || state == State::FAILED;
        const bool heartbeatActive = nowMs - _lastHeartbeatMs <= HEARTBEAT_TIMEOUT_MS;
        if (readableState && heartbeatActive &&
            nowMs - _lastConcentrationAttemptMs >= CONCENTRATION_INTERVAL_MS) {
            _lastConcentrationAttemptMs = nowMs;
            doConcentrationRead = true;
        }
    }
    xSemaphoreGive(_stateMutex);

    if (doCommand) {
        IoResult io = executeCalibrationCommand(
            PROFILES[profileIndex].slave, point, _sessionTargetPpb);
        if (xSemaphoreTake(_stateMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            if (_session == session && _state == state && _commandIssued) {
                if (io == IoResult::DEFERRED) {
                    _commandIssued = false;
                    _busWaiting = true;
                } else if (io == IoResult::OK) {
                    _commandStartedMs = millis();
                    // Do not poll 0x6006 immediately after the command ACK.
                    // The first status query follows the normal 1-second
                    // calibration interval (and therefore also exceeds the
                    // common 200 ms gas-bus minimum).
                    _lastStatusAttemptMs = _commandStartedMs;
                    _statusReadFailures = 0;
                    _busWaiting = false;
                    LOG_INFO("GAS_CAL_COMMAND point=%s target_ppb=%u write_ack=1",
                             pointName(point),
                             point == Point::SPAN ? _sessionTargetPpb : 0);
                } else {
                    _state = State::FAILED;
                    _pointResult = io == IoResult::UNLOCK_FAILED
                        ? PointResult::UNLOCK_FAILED : PointResult::WRITE_FAILED;
                    _commandIssued = false;
                    _busWaiting = false;
                    LOG_ERROR("GAS_CAL_RESULT point=%s success=0 reason=%s",
                              pointName(point), pointResultName(_pointResult));
                }
            }
            xSemaphoreGive(_stateMutex);
        }
        return;
    }

    if (doStatusRead) {
        uint16_t status = 0;
        IoResult io = readRegister(PROFILES[profileIndex].slave, 0x6006, status);
        if (xSemaphoreTake(_stateMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            if (_session == session && _state == state && _commandIssued) {
                if (io == IoResult::DEFERRED) {
                    _busWaiting = true;
                } else if (io != IoResult::OK) {
                    _busWaiting = false;
                    if (++_statusReadFailures >= 3) {
                        _state = State::FAILED;
                        _pointResult = PointResult::STATUS_READ_FAILED;
                        _commandIssued = false;
                        LOG_ERROR("GAS_CAL_RESULT point=%s success=0 reason=status_read_failed",
                                  pointName(_point));
                    }
                } else {
                    _busWaiting = false;
                    _statusReadFailures = 0;
                    _hasModuleStatus = true;
                    _moduleStatus = status;
                    LOG_INFO("GAS_CAL_STATUS value=0x%04X point=%s",
                             status, pointName(_point));
                    if (status == 0x0001) {
                        _pointResult = PointResult::SUCCESS;
                        _commandIssued = false;
                        if (_point == Point::ZERO) {
                            _state = State::SPAN_WAIT;
                            resetSamplesLocked();
                        } else {
                            _state = State::PURGING;
                            resetSamplesLocked();
                        }
                        LOG_INFO("GAS_CAL_RESULT point=%s success=1",
                                 pointName(_point));
                    } else if (status >= 0x0002 && status <= 0x0005) {
                        _state = State::FAILED;
                        _commandIssued = false;
                        if (status == 0x0002) _pointResult = PointResult::MODULE_FAILED;
                        else if (status == 0x0003) _pointResult = PointResult::UNSUPPORTED;
                        else if (status == 0x0004) _pointResult = PointResult::INVALID_PARAMETER;
                        else _pointResult = PointResult::MODULE_TIMEOUT;
                        LOG_ERROR("GAS_CAL_RESULT point=%s success=0 reason=%s",
                                  pointName(_point), pointResultName(_pointResult));
                    }
                }
            }
            xSemaphoreGive(_stateMutex);
        }
        return;
    }

    if (doConcentrationRead) {
        uint16_t concentration = 0;
        IoResult io = readRegister(
            PROFILES[profileIndex].slave, 0x6001, concentration);
        if (xSemaphoreTake(_stateMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            if (_session == session &&
                _maintenanceActive.load(std::memory_order_acquire)) {
                if (io == IoResult::DEFERRED) {
                    _busWaiting = true;
                    if (millis() - _lastBusDeferLogMs >= 2000) {
                        _lastBusDeferLogMs = millis();
                        LOG_INFO("GAS_CAL_BUS_DEFER reason=normal_collect id=%s",
                                 PROFILES[_profileIndex].id);
                    }
                } else if (io == IoResult::OK) {
                    const uint32_t readAt = millis();
                    _busWaiting = false;
                    _hasCurrent = true;
                    _currentPpb = concentration;
                    addSampleLocked(concentration, readAt);
                    LOG_DEBUG("GAS_CAL_READ id=%s value_ppb=%u ok=1",
                              PROFILES[_profileIndex].id, concentration);
                } else {
                    _busWaiting = false;
                    LOG_WARNING("GAS_CAL_READ id=%s ok=0",
                                PROFILES[_profileIndex].id);
                }
            }
            xSemaphoreGive(_stateMutex);
        }
    }
}

const char *GasCalibrationManager::stateName(State state)
{
    switch (state) {
        case State::ZERO_WAIT: return "zero_wait";
        case State::ZERO_EXECUTING: return "zero_executing";
        case State::SPAN_WAIT: return "span_wait";
        case State::SPAN_EXECUTING: return "span_executing";
        case State::PURGING: return "purging";
        case State::CANCEL_PURGING: return "cancel_purging";
        case State::TIMED_OUT: return "timed_out";
        case State::FAILED: return "failed";
        default: return "idle";
    }
}

const char *GasCalibrationManager::pointName(Point point)
{
    if (point == Point::ZERO) return "zero";
    if (point == Point::SPAN) return "span";
    return "none";
}

const char *GasCalibrationManager::pointResultName(PointResult result)
{
    switch (result) {
        case PointResult::PENDING: return "pending";
        case PointResult::SUCCESS: return "success";
        case PointResult::MODULE_FAILED: return "module_failed";
        case PointResult::UNSUPPORTED: return "unsupported";
        case PointResult::INVALID_PARAMETER: return "invalid_parameter";
        case PointResult::MODULE_TIMEOUT: return "module_timeout";
        case PointResult::STATUS_READ_FAILED: return "status_read_failed";
        case PointResult::UNLOCK_FAILED: return "unlock_failed";
        case PointResult::WRITE_FAILED: return "calibration_write_failed";
        default: return "none";
    }
}
