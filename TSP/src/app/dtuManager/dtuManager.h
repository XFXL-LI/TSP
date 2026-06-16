#ifndef DTU_MANAGER_H
#define DTU_MANAGER_H
#include "../../module/dtu/dtu_driver.h"
#include "../../inc/sys_init.h"

class DTUManager {
private:
    DTUDriver* _remoteDTU;
    DTUDriver* _hj212DTU;
    String readReUart();
    QueueHandle_t _queryQueue;

    DTUManager();
    ~DTUManager();

    void processQuery(JSONCmdData* req);
public:
    static DTUManager& getInstance();
    void init(Stream& remoteStream, Stream& hj212Stream);
    bool sendHJ212Packet(String dataContent);

    uint64_t dtuSystemTime(); 
    uint64_t hjSystemTime(); 

    int remoteDTUCSQ();
    int hj212DTUCSQ();

    bool updateReDtuGoalIP(String newIP);
    bool updateHJDtuGoalIP(String newIP);
    

    void poll();
};

#endif