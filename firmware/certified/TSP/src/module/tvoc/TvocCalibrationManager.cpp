#include "TvocCalibrationManager.h"
#include "TvocPolicy.h"
#include "../../app/collectorManager/collect/Tvoc/TvocCollect.h"
#include "../../inc/sys_init.h"
#include "../../system/ota/remote_ota_manager.h"
#include "../Serial/SerialManager.h"
#include "../json/config_json.h"
#include "../log/log_manager.h"
#include <cstdio>
#include <cstring>
#include <initializer_list>

namespace {
constexpr uint32_t HeartbeatLimitMs = 20000;
constexpr uint32_t BoardFreshMs = 3000;
constexpr uint32_t OperationLimitMs = 18000;
constexpr uint32_t ObserveIntervalMs = 1000;
constexpr uint16_t StatusRegister = 272;
constexpr uint16_t CommandRegister = 512;
constexpr uint16_t CsRegister = 513;
constexpr uint16_t RangeRegister = 515;
constexpr uint32_t InvalidPpb = 0xFFFFFFFFU;

portMUX_TYPE stateMux = portMUX_INITIALIZER_UNLOCKED;

class StateLock {
public:
    StateLock() { portENTER_CRITICAL(&stateMux); }
    ~StateLock() { portEXIT_CRITICAL(&stateMux); }
    StateLock(const StateLock&) = delete;
    StateLock& operator=(const StateLock&) = delete;
};

class BusLock {
public:
    BusLock() : _mutex(SerialManager::getInstance().getMutex(SERIAL_485)),
                _locked(_mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {}
    ~BusLock() { if (_locked) xSemaphoreGive(_mutex); }
    explicit operator bool() const { return _locked; }
    BusLock(const BusLock&) = delete;
    BusLock& operator=(const BusLock&) = delete;
private:
    SemaphoreHandle_t _mutex;
    bool _locked;
};

uint32_t wordPair(const uint16_t* words) {
    return (uint32_t(words[0]) << 16) | words[1];
}
int32_t signedPair(const uint16_t* words) {
    const uint32_t value = wordPair(words);
    return value <= INT32_MAX ? static_cast<int32_t>(value)
                              : -1 - static_cast<int32_t>(UINT32_MAX - value);
}
bool readUnsigned(cJSON* root, const char* name, uint32_t min, uint32_t max, uint32_t& out) {
    const cJSON* item = cJSON_GetObjectItemCaseSensitive(root, name);
    if (!cJSON_IsNumber(item) || !(item->valuedouble >= min && item->valuedouble <= max))
        return false;
    const uint32_t value = static_cast<uint32_t>(item->valuedouble);
    if (item->valuedouble != value) return false;
    out = value;
    return true;
}
}

int32_t TvocCalibrationManager::Board::zeroUv() const { return signedPair(words + 4); }
int32_t TvocCalibrationManager::Board::spanUv() const { return signedPair(words + 6); }
uint32_t TvocCalibrationManager::Board::csPpb() const { return wordPair(words + 8); }
uint32_t TvocCalibrationManager::Board::rangePpb() const { return wordPair(words + 12); }
int32_t TvocCalibrationManager::Board::pendingZeroUv() const { return signedPair(words + 15); }

TvocCalibrationManager& TvocCalibrationManager::getInstance() {
    static TvocCalibrationManager manager;
    return manager;
}

bool TvocCalibrationManager::begin() {
    auto& collector = TvocCollect::getInstance();
    _modbus = collector._modbus;
    const bool available = collector.begin() && _modbus &&
        SerialManager::getInstance().getMutex(SERIAL_485);
    _available.store(available, std::memory_order_release);
    return available;
}

const char* TvocCalibrationManager::actionName(Action action) {
    switch (action) {
        case Action::Start: return "start";
        case Action::Status: return "status";
        case Action::Zero: return "zero";
        case Action::Span: return "span";
        case Action::Range: return "set_range";
        case Action::Cancel: return "cancel";
        case Action::Finish: return "finish";
        default: return "none";
    }
}

const char* TvocCalibrationManager::phaseName(Phase phase) {
    switch (phase) {
        case Phase::Starting: return "starting";
        case Phase::Ready: return "ready";
        case Phase::Queued: return "queued";
        case Phase::Sampling: return "sampling";
        case Phase::Closing: return "closing";
        case Phase::Closed: return "closed";
        default: return "idle";
    }
}

bool TvocCalibrationManager::handleRequest(const char* json, char* response, size_t capacity) {
    config_json parsed(json);
    cJSON* root = parsed.getJsonObject();
    Request request;
    const cJSON* action = cJSON_GetObjectItemCaseSensitive(root, "action");
    if (cJSON_IsString(action)) {
        for (Action candidate : {Action::Start, Action::Status, Action::Zero, Action::Span,
                                Action::Range, Action::Cancel, Action::Finish}) {
            if (strcmp(action->valuestring, actionName(candidate)) == 0) {
                request.action = candidate;
                break;
            }
        }
    }
    const char* error = nullptr;
    if (request.action == Action::None) error = "invalid_action";
    else if (request.action != Action::Start &&
             !readUnsigned(root, "session", 1, UINT32_MAX, request.session))
        error = "invalid_session";
    else if (request.action != Action::Start && request.action != Action::Status &&
             !readUnsigned(root, "request_id", 1, UINT32_MAX, request.id))
        error = "invalid_request_id";
    else if (request.action == Action::Span &&
             !readUnsigned(root, "cs_ppb", 1, 20000, request.value))
        error = "invalid_cs_ppb";
    else if (request.action == Action::Range &&
             !readUnsigned(root, "r_ppb", 1, 20000, request.value))
        error = "invalid_r_ppb";

    Snapshot snapshot;
    if (!error) error = submit(request, snapshot);
    if (error) {
        const int length = snprintf(response, capacity,
            "{\"operation\":\"tvoc_calibration\",\"code\":\"NG\",\"action\":\"%s\",\"reason\":\"%s\"}",
            actionName(request.action), error);
        return length >= 0 && static_cast<size_t>(length) < capacity;
    }

    const uint32_t responseNow = millis();
    const bool boardFresh = snapshot.boardOk && responseNow - snapshot.boardMs <= BoardFreshMs;
    const bool measurementFresh = boardFresh && snapshot.phase == Phase::Ready &&
        !snapshot.board.busy() && snapshot.measured && responseNow - snapshot.measurementMs <= BoardFreshMs;
    char ppb[12] = "null";
    char uv[14] = "null";
    if (measurementFresh) {
        if (snapshot.ppb != InvalidPpb)
            snprintf(ppb, sizeof(ppb), "%lu", (unsigned long)snapshot.ppb);
        snprintf(uv, sizeof(uv), "%ld", (long)snapshot.uv);
    }
    const Board& board = snapshot.board;
    const int length = snprintf(response, capacity,
        "{\"operation\":\"tvoc_calibration\",\"code\":\"OK\",\"action\":\"%s\","
        "\"session\":%lu,\"request_id\":%lu,\"state\":\"%s\",\"result\":\"%s\",\"reason\":\"%s\","
        "\"pump\":%s,\"board_ok\":%s,\"busy\":%s,\"calibrated\":%s,\"pending_zero\":%s,"
        "\"board_state\":%u,\"error\":%u,\"flags\":%u,\"samples\":%u,\"elapsed_s\":%u,"
        "\"ppb\":%s,\"uv\":%s,\"zero_uv\":%ld,\"span_uv\":%ld,\"pending_zero_uv\":%ld,"
        "\"cs_ppb\":%lu,\"r_ppb\":%lu}",
        actionName(request.action), (unsigned long)snapshot.session,
        (unsigned long)snapshot.command.id, phaseName(snapshot.phase), snapshot.result, snapshot.reason,
        snapshot.pump ? "true" : "false", boardFresh ? "true" : "false",
        board.busy() ? "true" : "false", board.valid() ? "true" : "false",
        board.pendingZero() ? "true" : "false", unsigned(board.state()), unsigned(board.error()),
        unsigned(board.words[0]), unsigned(board.samples()), unsigned(board.words[14]),
        ppb, uv, (long)board.zeroUv(), (long)board.spanUv(), (long)board.pendingZeroUv(),
        (unsigned long)board.csPpb(), (unsigned long)board.rangePpb());
    return length >= 0 && static_cast<size_t>(length) < capacity;
}

const char* TvocCalibrationManager::submit(const Request& request, Snapshot& response) {
    if (!_available.load(std::memory_order_acquire)) return "tvoc_unavailable";
    StateLock lock;
    const uint32_t now = millis();
    if (request.action == Action::Start) {
        if (!active()) {
            if (!RemoteOtaManager::tryBeginTvocCalibration()) return "maintenance_busy";
            _snapshot = Snapshot{};
            if (++_nextSession == 0) ++_nextSession;
            _snapshot.session = _nextSession;
            _snapshot.phase = Phase::Starting;
            _sessionStarted = now;
            _exit = Exit::None;
            _active.store(true, std::memory_order_release);
        }
    } else {
        if (_snapshot.session != request.session) return "invalid_session";
        if (request.action != Action::Status) {
            if (request.id == _snapshot.command.id) {
                if (request.action != _snapshot.command.action || request.value != _snapshot.command.value)
                    return "request_id_conflict";
                if (active()) _heartbeat = now;
                response = _snapshot;
                return nullptr;
            }
            if (request.id < _snapshot.command.id) return "stale_request";
            if (!active() || _exit != Exit::None) return "session_closing";
            if (request.action != Action::Cancel) {
                if (_snapshot.phase != Phase::Ready) return "session_busy";
                if (!_snapshot.boardOk || now - _snapshot.boardMs > BoardFreshMs)
                    return "board_unavailable";
                if (_snapshot.board.busy()) return "board_busy";
                if ((request.action == Action::Zero || request.action == Action::Span) &&
                    (!_snapshot.measured || now - _snapshot.measurementMs > BoardFreshMs))
                    return "measurement_unavailable";
                if (request.action == Action::Span && !_snapshot.board.valid() &&
                    !_snapshot.board.pendingZero()) return "zero_required";
                if (request.action == Action::Finish && _snapshot.board.pendingZero())
                    return "span_or_cancel_required";
            }
            _snapshot.command = request;
            _snapshot.result = "none";
            _snapshot.reason = "none";
            _snapshot.measured = false;
            if (request.action == Action::Cancel || request.action == Action::Finish) {
                _exit = request.action == Action::Cancel ? Exit::Cancel : Exit::Finish;
                _snapshot.phase = Phase::Closing;
            } else {
                _snapshot.phase = Phase::Queued;
            }
        }
    }
    if (active()) _heartbeat = now;
    response = _snapshot;
    return nullptr;
}

TvocCalibrationManager::Exit TvocCalibrationManager::checkExit(bool pumpOn) {
    StateLock lock;
    // Read time after locking so a concurrent heartbeat cannot look newer than now.
    const uint32_t now = millis();
    if (!pumpOn && _exit == Exit::None) _exit = Exit::PumpFault;
    if (_exit == Exit::None &&
        (now - _heartbeat >= HeartbeatLimitMs || now - _sessionStarted >= SessionLimitMs))
        _exit = Exit::Timeout;
    if (_exit != Exit::None) _snapshot.phase = Phase::Closing;
    return _exit;
}

TvocCalibrationManager::Request TvocCalibrationManager::takeCommand() {
    StateLock lock;
    if (_snapshot.phase != Phase::Queued || _exit != Exit::None) return Request{};
    _snapshot.phase = Phase::Sampling;
    return _snapshot.command;
}

void TvocCalibrationManager::setResult(const char* result, const char* reason, bool ready) {
    StateLock lock;
    // Cancel/finish owns the result after it is queued.
    if (ready && _exit != Exit::None) return;
    _snapshot.result = result;
    _snapshot.reason = reason;
    _snapshot.measured = false;
    _snapshot.phase = ready ? Phase::Ready : Phase::Closing;
}

bool TvocCalibrationManager::read(uint16_t address, uint16_t count, uint16_t* words) {
    BusLock lock;
    return lock && _modbus->readModbusRegs(TvocPolicy::SLAVE, address, count, words, 1, 500, 0);
}

bool TvocCalibrationManager::writeCommand(uint16_t command) {
    BusLock lock;
    return lock && _modbus->writeModbusRegOnce(TvocPolicy::SLAVE, CommandRegister, command);
}

void TvocCalibrationManager::writeSetting(uint16_t address, uint32_t value) {
    uint16_t words[2] = {uint16_t(value >> 16), uint16_t(value)};
    BusLock lock;
    if (lock) _modbus->writeModbusRegs(TvocPolicy::SLAVE, address, 2, words, 1, 800, 0);
}

bool TvocCalibrationManager::refreshBoard(Board& board) {
    Board fresh;
    const bool ok = read(StatusRegister, 17, fresh.words) && fresh.state() <= 6 &&
        fresh.samples() <= 10 && fresh.words[14] <= 10;
    const uint32_t now = millis();
    StateLock lock;
    _snapshot.boardOk = ok;
    if (!ok) {
        _snapshot.measured = false;
        return false;
    }
    board = fresh;
    _snapshot.board = fresh;
    _snapshot.boardMs = now;
    if (fresh.busy()) _snapshot.measured = false;
    return true;
}

void TvocCalibrationManager::observe(const Board& board) {
    // One measurement request triggers one conversion for concentration and voltage.
    uint16_t words[5]{};
    const bool readOk = read(5, 5, words);
    const uint32_t now = millis();
    const int32_t uv = signedPair(words + 3);
    StateLock lock;
    _snapshot.measured = readOk && uv > -1199000 && uv < 1199000;
    _snapshot.measurementMs = now;
    _snapshot.ppb = board.valid() ? wordPair(words) : InvalidPpb;
    _snapshot.uv = uv;
}

void TvocCalibrationManager::execute(const Request& request, bool (*pumpOn)()) {
    Board before;
    if (!refreshBoard(before) || before.busy()) {
        setResult("failed", "board_unavailable", true);
        return;
    }
    if (request.action == Action::Span && !before.valid() && !before.pendingZero()) {
        setResult("failed", "zero_required", true);
        return;
    }
    if (checkExit(pumpOn()) != Exit::None) return;
    if (request.action == Action::Range) {
        writeSetting(RangeRegister, request.value);
        Board after;
        const bool verified = refreshBoard(after) && !after.busy() && after.rangePpb() == request.value;
        setResult(verified ? "range_verified" : "unknown",
                  verified ? "none" : "range_unverified", true);
        return;
    }

    const bool zero = request.action == Action::Zero;
    if (!zero) {
        writeSetting(CsRegister, request.value);
        uint16_t cs[2]{};
        if (!read(CsRegister, 2, cs) || wordPair(cs) != request.value) {
            setResult("failed", "cs_unverified", true);
            return;
        }
        if (checkExit(pumpOn()) != Exit::None) return;
    }

    const uint32_t started = millis();
    const bool ack = writeCommand(zero ? 1 : 2);
    LOG_INFO("TVOC_CAL_COMMAND action=%s ack=%d", actionName(request.action), ack ? 1 : 0);
    bool observedSampling = false;
    while (millis() - started < OperationLimitMs && checkExit(pumpOn()) == Exit::None) {
        Board after;
        if (refreshBoard(after)) {
            if (after.busy()) {
                if (after.state() == (zero ? 1 : 3)) observedSampling = true;
            } else {
                // An ACK or a stale terminal state alone cannot prove a new calibration.
                const bool newResult = observedSampling || after.state() != before.state() ||
                    after.error() != before.error() || after.zeroUv() != before.zeroUv() ||
                    after.spanUv() != before.spanUv() || after.csPpb() != before.csPpb() ||
                    after.pendingZero() != before.pendingZero() ||
                    after.pendingZeroUv() != before.pendingZeroUv();
                if (newResult && after.state() == 5) {
                    setResult("failed", "board_rejected", true);
                    return;
                }
                if (newResult && after.state() == (zero ? 2 : 4) && after.error() == 0 &&
                    after.samples() == 10 && after.words[14] == 10 &&
                    after.rangePpb() == before.rangePpb()) {
                    if (zero && !before.valid() && after.pendingZero() && !after.valid()) {
                        setResult("zero_pending", "none", true);
                        return;
                    }
                    const int64_t span = int64_t(after.spanUv()) - after.zeroUv();
                    const int32_t expectedZero = before.valid() ? before.zeroUv() : before.pendingZeroUv();
                    const bool consistent = zero
                        ? before.valid() && after.csPpb() == before.csPpb() &&
                            span == int64_t(before.spanUv()) - before.zeroUv()
                        : after.csPpb() == request.value && after.zeroUv() == expectedZero;
                    if (after.valid() && !after.pendingZero() && span >= 5000 && consistent) {
                        setResult("saved", "none", true);
                        return;
                    }
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(250));
    }

    StateLock lock;
    if (_exit == Exit::None) _exit = Exit::Unverified;
}

void TvocCalibrationManager::cleanup(Exit exit) {
    if (exit == Exit::Finish) {
        setResult("finished", "none", false);
        return;
    }
    const char* reason = exit == Exit::Cancel ? "operator_cancelled" :
                         exit == Exit::Timeout ? "session_timeout" :
                         exit == Exit::PumpFault ? "pump_cutoff" :
                         exit == Exit::LinkLoss ? "board_unavailable" : "result_unverified";
    const uint32_t started = millis();
    writeCommand(3);
    while (millis() - started < CleanupLimitMs) {
        Board board;
        if (refreshBoard(board) && board.state() == 6 && board.error() == 0 &&
            !board.busy() && !board.pendingZero()) {
            setResult(exit == Exit::Cancel ? "cancelled" : "unknown", reason, false);
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(250));
    }
    setResult("unknown", "cancel_unverified", false);
}

void TvocCalibrationManager::run(bool (*pumpOn)()) {
    uint32_t session = 0;
    {
        StateLock lock;
        _snapshot.pump = true;
        session = _snapshot.session;
    }
    LOG_INFO("TVOC_CAL_START session=%lu", (unsigned long)session);
    uint16_t version[3]{};
    if (!read(TvocPolicy::VERSION_REGISTER, 3, version) ||
        version[0] != 1 || version[1] != 2 || version[2] != 0) {
        setResult("failed", "board_version_unverified", false);
        return;
    }
    Board board;
    if (!refreshBoard(board) || board.busy()) {
        setResult("failed", board.busy() ? "board_busy" : "board_unavailable", false);
        return;
    }
    setResult("none", "none", true);
    uint32_t lastObservation = millis() - ObserveIntervalMs;
    unsigned misses = 0;
    for (;;) {
        const Exit exit = checkExit(pumpOn());
        if (exit != Exit::None) {
            cleanup(exit);
            return;
        }
        const Request request = takeCommand();
        if (request.action != Action::None) {
            execute(request, pumpOn);
            lastObservation = millis() - ObserveIntervalMs;
        } else if (millis() - lastObservation >= ObserveIntervalMs) {
            lastObservation = millis();
            if (refreshBoard(board)) {
                misses = 0;
                if (!board.busy() && checkExit(pumpOn()) == Exit::None) observe(board);
            } else if (++misses >= 3) {
                StateLock lock;
                if (_exit == Exit::None) _exit = Exit::LinkLoss;
                _snapshot.phase = Phase::Closing;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void TvocCalibrationManager::finishWorker(const char* failure) {
    TvocCollect::getInstance()._checkIdleBeforeDaily = true;
    Snapshot final;
    {
        StateLock lock;
        if (failure) {
            _snapshot.result = "failed";
            _snapshot.reason = failure;
        }
        _snapshot.pump = false;
        _snapshot.measured = false;
        _snapshot.phase = Phase::Closed;
        final = _snapshot;
        _active.store(false, std::memory_order_release);
        RemoteOtaManager::endTvocCalibration();
    }
    LOG_INFO("TVOC_CAL_END session=%lu result=%s reason=%s board_state=%u error=%u",
             (unsigned long)final.session, final.result, final.reason,
             unsigned(final.board.state()), unsigned(final.board.error()));
}
