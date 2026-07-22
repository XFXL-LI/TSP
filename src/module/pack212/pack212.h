#ifndef HJ212_DATA_CENTER_H
#define HJ212_DATA_CENTER_H

#include <Arduino.h>
#include <map>
#include <vector>
#include "../../inc/sys_init.h"

class HJ212_DataCenter {
public:
    HJ212_DataCenter();
    String build2017Hj212Packet(const AllProcessedDataPacket* allData, const HJ212CONFIG& sysCfg);
    String build2025Hj212Packet(const AllProcessedDataPacket* allData, const HJ212CONFIG& sysCfg);

private:
    unsigned int calculateCRC(const char* data, int len);
    String getCnCode(DataTime type);
    String getCurrentQn();
};

#endif
