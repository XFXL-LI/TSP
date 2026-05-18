#ifndef DTU_MANAGER_H
#define DTU_MANAGER_H
#include "../../module/dtu/dtu_driver.h"

class DTUManager {
private:
    DTUDriver* _remoteDTU;
    DTUDriver* _hj212DTU;
    String readReUart();

    DTUManager();
    ~DTUManager();
public:
    static DTUManager& getInstance();
    void init(Stream& remoteStream, Stream& hj212Stream);
    void sendHJ212Packet(String dataContent);

    uint64_t dtuSystemTime(); 
    uint64_t hjSystemTime(); 

    int remoteDTUCSQ();
    int hj212DTUCSQ();

    bool updateReDtuGoalIP(String newIP);
    bool updateHJDtuGoalIP(String newIP);


    void poll();
};

#endif