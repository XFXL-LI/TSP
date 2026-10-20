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
    static bool isValidPacket(const String& packet);
    // Rewrite only ozone CP field names in an already valid cached frame.
    static bool rewriteLegacyOzoneCode(String& packet);

private:
    static unsigned int calculateCRC(const char* data, int len);
    static String finalizePacket(const String& cp, const String& qn,
                                 const String& cn, const HJ212CONFIG& sysCfg,
                                 const char* protocolVersion);
    String getCnCode(DataTime type);
    String getCurrentQn();
};

#endif
