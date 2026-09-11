#ifndef DTU_MANAGER_H
#define DTU_MANAGER_H
#include "../../module/dtu/dtu_driver.h"
#include "../../inc/sys_init.h"
#include <atomic>

enum class Hj212SendResult : uint8_t {
    SENT,
    FAILED,
    DEFERRED
};

class DTUManager {
private:
    enum class HjSerialOwner : uint8_t {
        NONE,
        TIME_SYNC,
        CSQ,
        SET_IP,
        LIVE_PACKET,
        PENDING_PACKET,
        DTU_COMMAND
    };

    DTUDriver* _remoteDTU;
    DTUDriver* _hj212DTU;
    String readReUart();
    QueueHandle_t _queryQueue;
    std::atomic<uint32_t> _hjUploadWaiters{0};
    std::atomic<bool> _hjUploadActive{false};
    std::atomic<uint32_t> _hjLiveWaiters{0};
    std::atomic<bool> _hjLiveActive{false};
    std::atomic<uint32_t> _lastHjUploadEndMs{0};
    std::atomic<HjSerialOwner> _hjSerialOwner{HjSerialOwner::NONE};
    std::atomic<uint32_t> _hjSerialOwnerSinceMs{0};

    static const char* hjSerialOwnerName(HjSerialOwner owner);
    void setHjSerialOwner(HjSerialOwner owner);
    void clearHjSerialOwner();
    void logHjSerialBusy(const char *requester, uint32_t traceId = 0) const;

    Hj212SendResult sendHJ212PacketInternal(
        const String& dataContent, int maxRetry, uint32_t traceId,
        int dataType, uint64_t dataTimestamp, bool lowPriorityPending);

    DTUManager();
    ~DTUManager();

    void processQuery(JSONCmdData* req);
public:
    static constexpr int CSQ_DEFERRED = -2;
    static DTUManager& getInstance();
    void init(Stream& remoteStream, Stream& hj212Stream);
    // maxRetry <= 0 uses HJ212CONFIG.retry_times. Recovery callers may pass
    // an explicit one-attempt override without changing the configured live path.
    bool sendHJ212Packet(const String& dataContent, int maxRetry = 0,
                         uint32_t traceId = 0, int dataType = -1,
                         uint64_t dataTimestamp = 0);
    // Pending recovery is low-priority maintenance. DEFERRED means no serial
    // attempt was made, so callers must retain the file without counting a
    // delivery failure or entering cooldown.
    Hj212SendResult sendPendingHJ212Packet(
        const String& dataContent, int maxRetry = 1,
        uint32_t traceId = 0, int dataType = -1,
        uint64_t dataTimestamp = 0);

    uint64_t dtuSystemTime(); 
    uint64_t hjSystemTime(); 

    int remoteDTUCSQ();
    int hj212DTUCSQ();

    bool updateReDtuGoalIP(String newIP);
    bool updateHJDtuGoalIP(String newIP);
    

    void poll();
};

#endif
