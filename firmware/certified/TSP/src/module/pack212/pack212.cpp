#include "pack212.h"
#include "../../inc/sys_init.h"
#include "../../module/log/log_manager.h"
#include "../../module/gas/GasUnitConverter.h"
#include "../../app/configManager/config.h"
#include <time.h>
#include <sys/time.h>

namespace {

portMUX_TYPE qnSequenceMux = portMUX_INITIALIZER_UNLOCKED;
uint64_t lastQnMilliseconds = 0;

String factorStatus(const ProcessedDataPacket &packet)
{
    const char code = dataStatusCode(packet.status);
    switch (code) {
        case 'N': case 'F': case 'M': case 'S':
        case 'D': case 'C': case 'T': case 'B':
            return String(code);
        default:
            return packet.is_valid ? String("N") : String("D");
    }
}

} // namespace

HJ212_DataCenter::HJ212_DataCenter() {}

static String configuredHj212GasUnit(const String &sensorId)
{
    if (!GasUnitConverter::isGasSensor(sensorId)) return String();
    const COLLECTMAP &collectMap = ConfigManager::getInstance().getCollectConfigs();
    auto it = collectMap.find(sensorId);
    if (it == collectMap.end() && sensorId == "w34011") {
        it = collectMap.find("a05024");
    } else if (it == collectMap.end() && sensorId == "a05024") {
        it = collectMap.find("w34011");
    }
    const String requested = it != collectMap.end()
        ? it->second.unit
        : String(GasUnitConverter::defaultUnit(sensorId));
    return GasUnitConverter::normalizeUnit(sensorId, requested);
}

static String hj212OutputCode(const String &sensorId)
{
    if (sensorId == "w34011") return "a05024";
    if (sensorId == "L90") return "LA";
    return sensorId;
}

static String formatHj212Number(float value, uint8_t decimals)
{
    char buffer[48];
    const int written = snprintf(buffer, sizeof(buffer), "%.*f",
                                 static_cast<int>(decimals), value);
    if (written <= 0 || static_cast<size_t>(written) >= sizeof(buffer)) {
        return String();
    }
    return String(buffer);
}

static void appendCpField(String &cp, const String &field)
{
    if (field.length() == 0) return;
    if (cp.length() > 0) cp += ';';
    cp += field;
}

static float hj212OutputValue(const String &sensorId, float rawValue,
                              const String &gasUnit)
{
    return GasUnitConverter::isGasSensor(sensorId)
        ? GasUnitConverter::convertFromPpb(sensorId, rawValue, gasUnit)
        : rawValue;
}

String HJ212_DataCenter::getCurrentQn() {
    struct timeval current = {};
    if (gettimeofday(&current, nullptr) != 0) return "";

    uint64_t candidate = static_cast<uint64_t>(current.tv_sec) * 1000ULL +
                         static_cast<uint64_t>(current.tv_usec / 1000);
    portENTER_CRITICAL(&qnSequenceMux);
    if (candidate <= lastQnMilliseconds) candidate = lastQnMilliseconds + 1ULL;
    lastQnMilliseconds = candidate;
    portEXIT_CRITICAL(&qnSequenceMux);

    time_t qnSeconds = static_cast<time_t>(candidate / 1000ULL);
    struct tm timeInfo = {};
    if (localtime_r(&qnSeconds, &timeInfo) == nullptr ||
        timeInfo.tm_year + 1900 < 2020 ||
        timeInfo.tm_year + 1900 > 2099) return "";

    char qn[20];
    snprintf(qn, sizeof(qn), "%04d%02d%02d%02d%02d%02d%03u",
             timeInfo.tm_year + 1900, timeInfo.tm_mon + 1,
             timeInfo.tm_mday, timeInfo.tm_hour,
             timeInfo.tm_min, timeInfo.tm_sec,
             static_cast<unsigned>(candidate % 1000ULL));
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
    appendCpField(cp, "DataTime=" + timeStr);
    bool noiseAdded = false;
    for (auto const& [code, packet] : allData->processed_data_map) {
        // if (!packet.is_valid) continue; // 212协议要求即使无效数据也要上报，所以这里不跳过
        const bool isGas = GasUnitConverter::isGasSensor(code);
        const String gasUnit = isGas ? configuredHj212GasUnit(code) : String();
        const uint8_t decimals = isGas
            ? GasUnitConverter::decimalsForUnit(gasUnit)
            : 2;
        const String outputCode = hj212OutputCode(code);
        if (outputCode == "LA") {
            if (!packet.is_valid || noiseAdded) continue;
            if (cn == "2011") {
                appendCpField(cp, "LA-Rtd=" + formatHj212Number(packet.value, 1));
            } else {
                appendCpField(cp, "LA-Data=" + formatHj212Number(packet.value, 1));
                appendCpField(cp, "LMx-Data=" + formatHj212Number(packet.max_val, 1));
                appendCpField(cp, "LMn-Data=" + formatHj212Number(packet.min_val, 1));
            }
            noiseAdded = true;
            continue;
        }
        const float value = hj212OutputValue(code, packet.value, gasUnit);
        const float minValue = hj212OutputValue(code, packet.min_val, gasUnit);
        const float maxValue = hj212OutputValue(code, packet.max_val, gasUnit);
        String field;
        if (cn == "2011") {
            String flag = factorStatus(packet);
            field = outputCode + "-Rtd=" + formatHj212Number(value, decimals) + "," +
                    outputCode + "-Flag=" + flag;
        } else {
            String flag = factorStatus(packet);
            field = outputCode + "-Min=" + formatHj212Number(minValue, decimals) + "," +
                    outputCode + "-Avg=" + formatHj212Number(value, decimals) + "," +
                    outputCode + "-Max=" + formatHj212Number(maxValue, decimals) + "," +
                    outputCode + "-Flag=" + flag;
        }
        appendCpField(cp, field);
    }
    return finalizePacket(cp, qn, cn, sysCfg, "2017");
}

unsigned int HJ212_DataCenter::calculateCRC(const char *puchMsg, int usDataLen) {
    uint16_t crc = 0xFFFF;
    const unsigned char *ptr = reinterpret_cast<const unsigned char *>(puchMsg);
    while (usDataLen--) {
        crc = static_cast<uint16_t>((crc >> 8) ^ *ptr++);
        for (int i = 0; i < 8; i++) {
            const bool check = (crc & 0x0001U) != 0;
            crc = static_cast<uint16_t>(crc >> 1);
            if (check) crc = static_cast<uint16_t>(crc ^ 0xA001U);
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
    appendCpField(cp, "DataTime=" + timeStr);
    bool noiseAdded = false;
    for (auto const& [code, packet] : allData->processed_data_map) {
        // if (!packet.is_valid) continue; // hj212 2025协议不丢弃无效数据
        const bool isGas = GasUnitConverter::isGasSensor(code);
        const String gasUnit = isGas ? configuredHj212GasUnit(code) : String();
        const uint8_t decimals = isGas
            ? GasUnitConverter::decimalsForUnit(gasUnit)
            : 2;
        const String outputCode = hj212OutputCode(code);
        if (outputCode == "LA") {
            if (!packet.is_valid || noiseAdded) continue;
            if (cn == "2011") {
                appendCpField(cp, "LA-Rtd=" + formatHj212Number(packet.value, 1));
            } else {
                appendCpField(cp, "LA-Data=" + formatHj212Number(packet.value, 1));
                appendCpField(cp, "LMx-Data=" + formatHj212Number(packet.max_val, 1));
                appendCpField(cp, "LMn-Data=" + formatHj212Number(packet.min_val, 1));
            }
            noiseAdded = true;
            continue;
        }
        const float value = hj212OutputValue(code, packet.value, gasUnit);
        const float minValue = hj212OutputValue(code, packet.min_val, gasUnit);
        const float maxValue = hj212OutputValue(code, packet.max_val, gasUnit);
        const String flag = factorStatus(packet);
        String field;
        if (cn == "2011") {
            field = outputCode + "-Rtd=" + formatHj212Number(value, decimals) + "," +
                    outputCode + "-Flag=" + flag;
        } else {
            field = outputCode + "-Min=" + formatHj212Number(minValue, decimals) + "," +
                    outputCode + "-Avg=" + formatHj212Number(value, decimals) + "," +
                    outputCode + "-Max=" + formatHj212Number(maxValue, decimals) + "," +
                    outputCode + "-Flag=" + flag;
        }
        appendCpField(cp, field);
    }
    return finalizePacket(cp, qn, cn, sysCfg, "2025");
}

String HJ212_DataCenter::finalizePacket(const String& cp, const String& qn,
                                        const String& cn,
                                        const HJ212CONFIG& sysCfg,
                                        const char* protocolVersion) {
    constexpr size_t fixedContentLength =
        (sizeof("QN=") - 1) + (sizeof(";ST=") - 1) +
        (sizeof(";CN=") - 1) + (sizeof(";PW=") - 1) +
        (sizeof(";MN=") - 1) + (sizeof(";Flag=") - 1) +
        (sizeof(";CP=&&") - 1) + (sizeof("&&") - 1);
    const size_t contentLength = fixedContentLength + qn.length() +
        sysCfg.st.length() + cn.length() + sysCfg.pw.length() +
        sysCfg.mn.length() + sysCfg.flag.length() + cp.length();

    if (contentLength == 0 || contentLength > 9999U) {
        LOG_ERROR("[DIAG] HJ_BUILD_FAIL version=%s stage=content_length bytes=%u free=%u largest=%u",
                  protocolVersion, (unsigned)contentLength,
                  ESP.getFreeHeap(), ESP.getMaxAllocHeap());
        return "";
    }

    String packet;
    if (!packet.reserve(contentLength + 12U)) {
        LOG_ERROR("[DIAG] HJ_BUILD_FAIL version=%s stage=packet_reserve bytes=%u free=%u largest=%u",
                  protocolVersion, (unsigned)(contentLength + 12U),
                  ESP.getFreeHeap(), ESP.getMaxAllocHeap());
        return "";
    }

    // Build the complete frame in one String. The first four digits are filled
    // in place after the content length is known, avoiding a second full frame.
    packet = "##0000QN=";
    packet += qn;
    packet += ";ST=";
    packet += sysCfg.st;
    packet += ";CN=";
    packet += cn;
    packet += ";PW=";
    packet += sysCfg.pw;
    packet += ";MN=";
    packet += sysCfg.mn;
    packet += ";Flag=";
    packet += sysCfg.flag;
    packet += ";CP=&&";
    packet += cp;
    packet += "&&";

    if (packet.length() != contentLength + 6U) {
        LOG_ERROR("[DIAG] HJ_BUILD_FAIL version=%s stage=packet_append bytes=%u expected=%u free=%u largest=%u",
                  protocolVersion, (unsigned)packet.length(),
                  (unsigned)(contentLength + 6U),
                  ESP.getFreeHeap(), ESP.getMaxAllocHeap());
        return "";
    }

    packet.setCharAt(2, static_cast<char>('0' + (contentLength / 1000U) % 10U));
    packet.setCharAt(3, static_cast<char>('0' + (contentLength / 100U) % 10U));
    packet.setCharAt(4, static_cast<char>('0' + (contentLength / 10U) % 10U));
    packet.setCharAt(5, static_cast<char>('0' + contentLength % 10U));

    const unsigned int crc = calculateCRC(packet.c_str() + 6,
                                           static_cast<int>(contentLength));
    char crcText[5];
    snprintf(crcText, sizeof(crcText), "%04X", crc);
    packet += crcText;
    packet += "\r\n";

    if (!isValidPacket(packet)) {
        LOG_ERROR("[DIAG] HJ_BUILD_FAIL version=%s stage=validation bytes=%u free=%u largest=%u",
                  protocolVersion, (unsigned)packet.length(),
                  ESP.getFreeHeap(), ESP.getMaxAllocHeap());
        return "";
    }
    return packet;
}

bool HJ212_DataCenter::isValidPacket(const String& packet) {
    if (packet.length() < 32 || !packet.startsWith("##") ||
        !packet.endsWith("\r\n")) {
        return false;
    }

    unsigned int contentLength = 0;
    for (int i = 2; i < 6; ++i) {
        const char value = packet.charAt(i);
        if (value < '0' || value > '9') return false;
        contentLength = contentLength * 10U +
                        static_cast<unsigned int>(value - '0');
    }

    if (contentLength == 0 || packet.length() != contentLength + 12U) {
        return false;
    }

    const unsigned int contentStart = 6U;
    const unsigned int contentEnd = contentStart + contentLength;
    if (packet.charAt(contentStart) != 'Q' ||
        packet.charAt(contentStart + 1U) != 'N' ||
        packet.charAt(contentStart + 2U) != '=' ||
        packet.charAt(contentEnd - 2U) != '&' ||
        packet.charAt(contentEnd - 1U) != '&') {
        return false;
    }

    const auto containsToken = [&packet, contentStart, contentEnd](const char* token,
                                                                  unsigned int tokenLength) {
        if (tokenLength == 0U || tokenLength > contentEnd - contentStart) return false;
        for (unsigned int pos = contentStart;
             pos + tokenLength <= contentEnd; ++pos) {
            unsigned int index = 0;
            while (index < tokenLength &&
                   packet.charAt(pos + index) == token[index]) {
                ++index;
            }
            if (index == tokenLength) return true;
        }
        return false;
    };
    if (!containsToken(";CN=", sizeof(";CN=") - 1) ||
        !containsToken(";CP=&&DataTime=", sizeof(";CP=&&DataTime=") - 1)) {
        return false;
    }

    unsigned int expectedCrc = 0;
    for (unsigned int pos = contentEnd; pos < contentEnd + 4U; ++pos) {
        const char value = packet.charAt(pos);
        unsigned int nibble = 0;
        if (value >= '0' && value <= '9') {
            nibble = static_cast<unsigned int>(value - '0');
        } else if (value >= 'A' && value <= 'F') {
            nibble = static_cast<unsigned int>(value - 'A' + 10);
        } else if (value >= 'a' && value <= 'f') {
            nibble = static_cast<unsigned int>(value - 'a' + 10);
        } else {
            return false;
        }
        expectedCrc = (expectedCrc << 4U) | nibble;
    }

    return calculateCRC(packet.c_str() + contentStart,
                        static_cast<int>(contentLength)) == expectedCrc;
}
