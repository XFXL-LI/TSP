#include "pack212.h"
#include "../../inc/sys_init.h"
#include "../../module/log/log_manager.h"
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
    if (!cp.reserve(allData->processed_data_map.size() * 80 + 64)) {
        LOG_ERROR("[DIAG] HJ_BUILD_FAIL version=2017 stage=cp_reserve free=%u largest=%u",
                  ESP.getFreeHeap(), ESP.getMaxAllocHeap());
        return "";
    }
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
    if (!hj212_content.reserve(cp.length() + 200)) {
        LOG_ERROR("[DIAG] HJ_BUILD_FAIL version=2017 stage=content_reserve free=%u largest=%u",
                  ESP.getFreeHeap(), ESP.getMaxAllocHeap());
        return "";
    }
    hj212_content = "QN=";
    hj212_content += qn;
    hj212_content += ";ST=";
    hj212_content += sysCfg.st;
    hj212_content += ";CN=";
    hj212_content += cn;
    hj212_content += ";PW=";
    hj212_content += sysCfg.pw;
    hj212_content += ";MN=";
    hj212_content += sysCfg.mn;
    hj212_content += ";Flag=";
    hj212_content += sysCfg.flag;
    hj212_content += ";CP=&&";
    hj212_content += cp;
    hj212_content += "&&";
    unsigned int crc = calculateCRC(hj212_content.c_str(), hj212_content.length());
    char finalHead[8];
    snprintf(finalHead, sizeof(finalHead), "##%04d", (int)hj212_content.length());
    char crcStr[8];
    snprintf(crcStr, sizeof(crcStr), "%04X", crc);
    String result;
    if (!result.reserve(hj212_content.length() + 12)) {
        LOG_ERROR("[DIAG] HJ_BUILD_FAIL version=2017 stage=packet_reserve free=%u largest=%u",
                  ESP.getFreeHeap(), ESP.getMaxAllocHeap());
        return "";
    }
    result = finalHead;
    result += hj212_content;
    result += crcStr;
    result += "\r\n";
    if (!isValidPacket(result)) {
        LOG_ERROR("[DIAG] HJ_BUILD_FAIL version=2017 stage=validation bytes=%u",
                  (unsigned)result.length());
        return "";
    }
    return result;
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
    if (!cp.reserve(allData->processed_data_map.size() * 80 + 64)) {
        LOG_ERROR("[DIAG] HJ_BUILD_FAIL version=2025 stage=cp_reserve free=%u largest=%u",
                  ESP.getFreeHeap(), ESP.getMaxAllocHeap());
        return "";
    }
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
    if (!hj212_content.reserve(cp.length() + 200)) {
        LOG_ERROR("[DIAG] HJ_BUILD_FAIL version=2025 stage=content_reserve free=%u largest=%u",
                  ESP.getFreeHeap(), ESP.getMaxAllocHeap());
        return "";
    }
    hj212_content = "QN=";
    hj212_content += qn;
    hj212_content += ";ST=";
    hj212_content += sysCfg.st;
    hj212_content += ";CN=";
    hj212_content += cn;
    hj212_content += ";PW=";
    hj212_content += sysCfg.pw;
    hj212_content += ";MN=";
    hj212_content += sysCfg.mn;
    hj212_content += ";Flag=";
    hj212_content += sysCfg.flag;
    hj212_content += ";CP=&&";
    hj212_content += cp;
    hj212_content += "&&";
    unsigned int crc = calculateCRC(hj212_content.c_str(), hj212_content.length());
    char finalHead[8];
    snprintf(finalHead, sizeof(finalHead), "##%04d", (int)hj212_content.length());
    char crcStr[8];
    snprintf(crcStr, sizeof(crcStr), "%04X", crc);
    String result;
    if (!result.reserve(hj212_content.length() + 12)) {
        LOG_ERROR("[DIAG] HJ_BUILD_FAIL version=2025 stage=packet_reserve free=%u largest=%u",
                  ESP.getFreeHeap(), ESP.getMaxAllocHeap());
        return "";
    }
    result = finalHead;
    result += hj212_content;
    result += crcStr;
    result += "\r\n";
    if (!isValidPacket(result)) {
        LOG_ERROR("[DIAG] HJ_BUILD_FAIL version=2025 stage=validation bytes=%u",
                  (unsigned)result.length());
        return "";
    }
    return result;
}

bool HJ212_DataCenter::isValidPacket(const String& packet) {
    if (packet.length() < 32 || !packet.startsWith("##") ||
        !packet.endsWith("\r\n")) {
        return false;
    }

    for (int i = 2; i < 6; ++i) {
        if (!isDigit(packet.charAt(i))) return false;
    }

    int contentLength = packet.substring(2, 6).toInt();
    if (contentLength <= 0 ||
        packet.length() != (unsigned)(contentLength + 12)) {
        return false;
    }

    String content = packet.substring(6, 6 + contentLength);
    if (!content.startsWith("QN=") ||
        content.indexOf(";CN=") < 0 ||
        content.indexOf(";CP=&&DataTime=") < 0 ||
        !content.endsWith("&&")) {
        return false;
    }

    String crcText = packet.substring(6 + contentLength,
                                      10 + contentLength);
    for (unsigned int i = 0; i < crcText.length(); ++i) {
        if (!isHexadecimalDigit(crcText.charAt(i))) return false;
    }
    unsigned int expected =
        (unsigned int)strtoul(crcText.c_str(), nullptr, 16);
    return calculateCRC(content.c_str(), content.length()) == expected;
}
