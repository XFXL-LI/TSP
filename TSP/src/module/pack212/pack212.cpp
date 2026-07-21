#include "pack212.h"
#include "../../inc/sys_init.h"
#include <time.h>

HJ212_DataCenter::HJ212_DataCenter() {}

String HJ212_DataCenter::getCurrentQn() {
    time_t now;
    struct tm timeInfo;
    time(&now);
    if (localtime_r(&now, &timeInfo) == nullptr) return "";

    char qn[20];
    snprintf(qn, sizeof(qn), "%04d%02d%02d%02d%02d%02d001",
             timeInfo.tm_year + 1900, timeInfo.tm_mon + 1,
             timeInfo.tm_mday, timeInfo.tm_hour,
             timeInfo.tm_min, timeInfo.tm_sec);
    return String(qn);
}

String HJ212_DataCenter::getCnCode(DataTime type) {
    switch (type) {
        case DataTime::MIN_DATA:  return "2051";
        case DataTime::HOUR_DATA: return "2061";
        case DataTime::DAY_DATA:  return "2031";
        case DataTime::REAL_DATA: return "2011";
        default: return "2011";
    }
}
// 20260506120000
String HJ212_DataCenter::build2017Hj212Packet(const AllProcessedDataPacket* allData, const HJ212CONFIG& sysCfg) {
    if (allData == nullptr || allData->processed_data_map.empty()) return "";
    String cp;
    cp.reserve(allData->processed_data_map.size() * 80 + 64);
    String timeStr = String(allData->last_update);
    String qn = getCurrentQn();
    if (qn.length() == 0) return "";
    String cn = getCnCode(allData->dataTime);
    cp += "DataTime=" + timeStr + ";";
    for (auto const& [code, packet] : allData->processed_data_map) {
        // if (!packet.is_valid) continue; // 212协议要求即使无效数据也要上报，所以这里不跳过
        if (cn == "2011") {
            String flag = packet.is_valid ? "N" : "D";
            if (code == "LA" || code == "L90") {
                cp += code + "-Rtd=" + String(packet.value, 1) + ";";
            } else {
                cp += code + "-Rtd=" + String(packet.value, 2) + "," +
                      code + "-Flag=" + flag + ";";
            }
        } else {
            String flag = packet.is_valid ? "N" : "D";
            if (code == "L90") {
                cp += code + "-Data=" + String(packet.value, 1) + ";";
                cp += "LMx-Data=" + String(packet.max_val, 1) + ";";
                cp += "LMn-Data=" + String(packet.min_val, 1) + ";";
            } else if (code == "LA") {
                cp += code + "-Data=" + String(packet.value, 1) + ";";
            } else {
                cp += code + "-Min=" + String(packet.min_val, 2) + ",";
                cp += code + "-Avg=" + String(packet.value, 2) + ","; 
                cp += code + "-Cou=" + String(packet.cou_val, 2) + ","; 
                cp += code + "-Max=" + String(packet.max_val, 2) + "," +
                      code + "-Flag=" + flag + ";";
            }
        }
    }
    String hj212_content;
    hj212_content.reserve(cp.length() + 200);
    hj212_content = "QN=" + qn +
                    ";ST=" + sysCfg.st + 
                    ";CN=" + cn + 
                    ";PW=" + sysCfg.pw + 
                    ";MN=" + sysCfg.mn + 
                    ";Flag=" + sysCfg.flag + 
                    ";CP=&&" + cp + "&&";
    unsigned int crc = calculateCRC(hj212_content.c_str(), hj212_content.length());
    char headAndCrc[32];
    snprintf(headAndCrc, sizeof(headAndCrc), "##%04d%s%04X\r\n", 
             (int)hj212_content.length(), hj212_content.c_str(), crc);
    char finalHead[8];
    snprintf(finalHead, sizeof(finalHead), "##%04d", (int)hj212_content.length());
    char crcStr[8];
    snprintf(crcStr, sizeof(crcStr), "%04X", crc);
    return String(finalHead) + hj212_content + String(crcStr) + "\r\n";
}

unsigned int HJ212_DataCenter::calculateCRC(const char *puchMsg, int usDataLen) {
    unsigned int crc = 0xFFFF;
    unsigned char *ptr = (unsigned char *)puchMsg;
    while (usDataLen--) {
        crc ^= *ptr++;
        for (int i = 0; i < 8; i++) {
            if (crc & 1) crc = (crc >> 1) ^ 0xA001;
            else crc = crc >> 1;
        }
    }
    return crc;
}

String HJ212_DataCenter::build2025Hj212Packet(const AllProcessedDataPacket* allData, const HJ212CONFIG& sysCfg) {
    if (allData == nullptr || allData->processed_data_map.empty()) return "";
    String cp;
    cp.reserve(allData->processed_data_map.size() * 80 + 64);
    String timeStr = String(allData->last_update);
    String qn = getCurrentQn();
    if (qn.length() == 0) return "";
    String cn = getCnCode(allData->dataTime);
    cp += "DataTime=" + timeStr + ";";
    for (auto const& [code, packet] : allData->processed_data_map) {
        // if (!packet.is_valid) continue; // hj212 2025协议不丢弃无效数据
        if (cn == "2011") {
            if (code == "LA" || code == "L90") {
                cp += code + "-Rtd=" + String(packet.value, 1) + ";";
            } else {
                cp += code + "-Rtd=" + String(packet.value, 2) + "," + code + "-Flag=N;";
            }
        } else {
            if (code == "LA" || code == "L90") {
                cp += code + "-Data=" + String(packet.value, 1) + ";";
                cp += code + "-LMx-Data=" + String(packet.max_val, 1) + ";";
                cp += code + "-LMn-Data=" + String(packet.min_val, 1) + ";";
            } else {
                cp += code + "-Cou=" + String(packet.cou_val, 2) + ",";
                cp += code + "-Min=" + String(packet.min_val, 2) + ",";
                cp += code + "-Avg=" + String(packet.value, 2) + ","; 
                cp += code + "-Max=" + String(packet.max_val, 2) + "," + code + "-Flag=N;";
            }
        }
    }
    String hj212_content;
    hj212_content.reserve(cp.length() + 200);
    hj212_content = "QN=" + qn +
                    ";ST=" + sysCfg.st + 
                    ";CN=" + cn + 
                    ";PW=" + sysCfg.pw + 
                    ";MN=" + sysCfg.mn + 
                    ";Flag=" + sysCfg.flag + 
                    ";CP=&&" + cp + "&&";
    unsigned int crc = calculateCRC(hj212_content.c_str(), hj212_content.length());
    char headAndCrc[32];
    snprintf(headAndCrc, sizeof(headAndCrc), "##%04d%s%04X\r\n", 
             (int)hj212_content.length(), hj212_content.c_str(), crc);
    char finalHead[8];
    snprintf(finalHead, sizeof(finalHead), "##%04d", (int)hj212_content.length());
    char crcStr[8];
    snprintf(crcStr, sizeof(crcStr), "%04X", crc);
    return String(finalHead) + hj212_content + String(crcStr) + "\r\n";
}
