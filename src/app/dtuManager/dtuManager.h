#ifndef DTU_MANAGER_H
#define DTU_MANAGER_H
#include "../../module/dtu/dtu_driver.h"
#include "../../inc/sys_init.h"
#include <atomic>

class DTUManager {
private:
    DTUDriver* _remoteDTU;
    DTUDriver* _hj212DTU;
    String readReUart();
    QueueHandle_t _queryQueue;
    std::atomic<uint32_t> _hjUploadWaiters{0};
    std::atomic<bool> _hjUploadActive{false};
    std::atomic<uint32_t> _lastHjUploadEndMs{0};

    DTUManager();
    ~DTUManager();

    void processQuery(JSONCmdData* req);
public:
    static constexpr int CSQ_DEFERRED = -2;
    static DTUManager& getInstance();
    void init(Stream& remoteStream, Stream& hj212Stream);
    bool sendHJ212Packet(const String& dataContent, int maxRetry = 3,
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
