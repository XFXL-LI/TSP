#pragma once

#include <atomic>
#include <stddef.h>
#include <stdint.h>

class modbus_manager;

// The collection task is the sole serial and pump owner for this session.
class TvocCalibrationManager {
public:
    static constexpr uint32_t SessionLimitMs = 30UL * 60UL * 1000UL;
    static constexpr uint32_t CleanupLimitMs = 5000;

    static TvocCalibrationManager& getInstance();
    bool begin();
    bool active() const { return _active.load(std::memory_order_acquire); }
    bool handleRequest(const char* json, char* response, size_t capacity);
    void run(bool (*pumpOn)());
    void finishWorker(const char* failure = nullptr);

private:
    enum class Action : uint8_t { None, Start, Status, Zero, Span, Range, Cancel, Finish };
    enum class Phase : uint8_t { Idle, Starting, Ready, Queued, Sampling, Closing, Closed };
    enum class Exit : uint8_t { None, Finish, Cancel, Timeout, LinkLoss, PumpFault, Unverified };
    struct Board {
        uint16_t words[17]{};
        bool valid() const { return (words[0] & 1U) != 0; }
        bool pendingZero() const { return (words[0] & 2U) != 0; }
        bool busy() const { return (words[0] & 4U) != 0 || state() == 1 || state() == 3; }
        uint16_t state() const { return words[1]; }
        uint16_t error() const { return words[2]; }
        uint16_t samples() const { return words[3]; }
        int32_t zeroUv() const;
        int32_t spanUv() const;
        uint32_t csPpb() const;
        uint32_t rangePpb() const;
        int32_t pendingZeroUv() const;
    };
    struct Request {
        Action action = Action::None;
        uint32_t session = 0;
        uint32_t id = 0;
        uint32_t value = 0;
    };
    struct Snapshot {
        uint32_t session = 0;
        Phase phase = Phase::Idle;
        Request command;
        const char* result = "none";
        const char* reason = "none";
        Board board;
        bool pump = false;
        bool boardOk = false;
        uint32_t boardMs = 0;
        bool measured = false;
        uint32_t measurementMs = 0;
        uint32_t ppb = 0xFFFFFFFFU;
        int32_t uv = 0;
    };

    const char* submit(const Request& request, Snapshot& response);
    Exit checkExit(bool pumpOn);
    Request takeCommand();
    void setResult(const char* result, const char* reason, bool ready);
    bool read(uint16_t address, uint16_t count, uint16_t* words);
    bool writeCommand(uint16_t command);
    void writeSetting(uint16_t address, uint32_t value);
    bool refreshBoard(Board& board);
    void observe(const Board& board);
    void execute(const Request& request, bool (*pumpOn)());
    void cleanup(Exit exit);
    static const char* actionName(Action action);
    static const char* phaseName(Phase phase);

    std::atomic<bool> _available{false};
    std::atomic<bool> _active{false};
    modbus_manager* _modbus = nullptr;
    Snapshot _snapshot;
    uint32_t _nextSession = 0;
    uint32_t _sessionStarted = 0;
    uint32_t _heartbeat = 0;
    Exit _exit = Exit::None;
};
