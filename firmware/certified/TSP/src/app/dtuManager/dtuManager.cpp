#include "dtuManager.h"
#include "../../module/pack212/pack212.h"
#include "../../system/event/eventBus.h"
#include "../../module/json/config_json.h"
#include "../../module/Serial/SerialManager.h"
#include "../../module/diagnostics/RuntimeMemoryDiagnostics.h"
#include "../configManager/config.h"

namespace {

bool parseHj212Flag(const String& packet, uint8_t& flag)
{
    const int tokenPos = packet.indexOf(";Flag=");
    if (tokenPos < 0) return false;

    const int valueStart = tokenPos + 6;
    const int valueEnd = packet.indexOf(';', valueStart);
    if (valueEnd <= valueStart) return false;

    unsigned int value = 0;
    for (int index = valueStart; index < valueEnd; ++index) {
        const char character = packet.charAt(index);
        if (character < '0' || character > '9') return false;

        value = value * 10U + static_cast<unsigned int>(character - '0');
        if (value > 255U) return false;
    }

    flag = static_cast<uint8_t>(value);
    return true;
}

enum class HjAckMode : uint8_t {
    STANDARD,
    COMPATIBLE,
    LEGACY
};

struct AckValidationResult {
    bool accepted = false;
    bool framed = false;
    const char* reason = "no_9014";
};

HjAckMode parseAckMode(String configured)
{
    configured.trim();
    configured.toLowerCase();
    if (configured == "standard") return HjAckMode::STANDARD;
    if (configured == "legacy") return HjAckMode::LEGACY;
    return HjAckMode::COMPATIBLE;
}

const char* ackModeName(HjAckMode mode)
{
    switch (mode) {
        case HjAckMode::STANDARD: return "standard";
        case HjAckMode::LEGACY: return "legacy";
        case HjAckMode::COMPATIBLE:
        default: return "compatible";
    }
}

bool isFieldBoundary(char value)
{
    return value == ';' || value == '&' || value == ',' ||
           value == '\r' || value == '\n' || value == ' ' || value == '\t';
}

bool isFrameContentStart(const String& text, int position)
{
    if (position != 6 || !text.startsWith("##")) return false;
    for (int index = 2; index < 6; ++index) {
        const char value = text.charAt(index);
        if (value < '0' || value > '9') return false;
    }
    return true;
}

bool extractHjField(const String& text, const char* key, String& value)
{
    String pattern(key);
    pattern += '=';
    int searchFrom = 0;
    while (searchFrom < static_cast<int>(text.length())) {
        const int position = text.indexOf(pattern, searchFrom);
        if (position < 0) return false;
        if (position == 0 || isFrameContentStart(text, position) ||
            isFieldBoundary(text.charAt(position - 1))) {
            int end = position + pattern.length();
            while (end < static_cast<int>(text.length()) &&
                   !isFieldBoundary(text.charAt(end))) {
                ++end;
            }
            value = text.substring(position + pattern.length(), end);
            return value.length() > 0;
        }
        searchFrom = position + pattern.length();
    }
    return false;
}

uint16_t calculateHjCrc(const char* data, size_t length)
{
    uint16_t crc = 0xFFFFU;
    const uint8_t* current = reinterpret_cast<const uint8_t*>(data);
    while (length-- > 0) {
        crc = static_cast<uint16_t>((crc >> 8U) ^ *current++);
        for (uint8_t bit = 0; bit < 8; ++bit) {
            const bool lowBit = (crc & 0x0001U) != 0U;
            crc = static_cast<uint16_t>(crc >> 1U);
            if (lowBit) crc = static_cast<uint16_t>(crc ^ 0xA001U);
        }
    }
    return crc;
}

bool parseHexWord(const String& text, size_t offset, uint16_t& value)
{
    if (offset + 4U > text.length()) return false;
    value = 0;
    for (size_t index = offset; index < offset + 4U; ++index) {
        const char ch = text.charAt(index);
        uint8_t nibble = 0;
        if (ch >= '0' && ch <= '9') nibble = static_cast<uint8_t>(ch - '0');
        else if (ch >= 'A' && ch <= 'F') nibble = static_cast<uint8_t>(ch - 'A' + 10);
        else if (ch >= 'a' && ch <= 'f') nibble = static_cast<uint8_t>(ch - 'a' + 10);
        else return false;
        value = static_cast<uint16_t>((value << 4U) | nibble);
    }
    return true;
}

bool identitiesMatch(const String& candidate, const String& expectedQn,
                     const String& expectedMn, bool requireBoth,
                     const char*& reason)
{
    String receivedQn;
    String receivedMn;
    const bool hasQn = extractHjField(candidate, "QN", receivedQn);
    const bool hasMn = extractHjField(candidate, "MN", receivedMn);
    if (requireBoth && (!hasQn || !hasMn)) {
        reason = "identity_missing";
        return false;
    }
    if (hasQn && receivedQn != expectedQn) {
        reason = "qn_mismatch";
        return false;
    }
    if (hasMn && receivedMn != expectedMn) {
        reason = "mn_mismatch";
        return false;
    }
    return true;
}

bool acknowledgementSucceeded(const String& candidate, bool requireField,
                              const char*& reason)
{
    String qnResult;
    const bool present = extractHjField(candidate, "QnRtn", qnResult);
    if (requireField && !present) {
        reason = "qnrtn_missing";
        return false;
    }
    if (present && qnResult != "1") {
        reason = "qnrtn_failed";
        return false;
    }
    return true;
}

AckValidationResult validate9014Response(const String& response,
                                         const String& expectedQn,
                                         const String& expectedMn,
                                         HjAckMode mode)
{
    AckValidationResult result;
    int searchFrom = 0;
    bool sawFramePrefix = false;
    bool sawCompleteValidFrame = false;
    bool sawMalformedFrame = false;

    while (searchFrom < static_cast<int>(response.length())) {
        const int frameStart = response.indexOf("##", searchFrom);
        if (frameStart < 0) break;
        sawFramePrefix = true;
        if (frameStart + 6 > static_cast<int>(response.length())) {
            sawMalformedFrame = true;
            break;
        }

        size_t contentLength = 0;
        bool lengthValid = true;
        for (int pos = frameStart + 2; pos < frameStart + 6; ++pos) {
            const char ch = response.charAt(pos);
            if (ch < '0' || ch > '9') {
                lengthValid = false;
                break;
            }
            contentLength = contentLength * 10U + static_cast<size_t>(ch - '0');
        }
        const size_t totalLength = contentLength + 12U;
        if (!lengthValid || contentLength == 0U ||
            static_cast<size_t>(frameStart) + totalLength > response.length()) {
            sawMalformedFrame = true;
            searchFrom = frameStart + 2;
            continue;
        }

        const size_t contentStart = static_cast<size_t>(frameStart) + 6U;
        const size_t contentEnd = contentStart + contentLength;
        if (response.charAt(contentEnd + 4U) != '\r' ||
            response.charAt(contentEnd + 5U) != '\n') {
            sawMalformedFrame = true;
            searchFrom = frameStart + 2;
            continue;
        }

        uint16_t expectedCrc = 0;
        if (!parseHexWord(response, contentEnd, expectedCrc) ||
            calculateHjCrc(response.c_str() + contentStart, contentLength) != expectedCrc) {
            sawMalformedFrame = true;
            searchFrom = static_cast<int>(contentEnd + 6U);
            continue;
        }

        sawCompleteValidFrame = true;
        const String content = response.substring(contentStart, contentEnd);
        String cn;
        if (extractHjField(content, "CN", cn) && cn == "9014") {
            const char* identityReason = "accepted";
            if (!identitiesMatch(content, expectedQn, expectedMn,
                                 mode == HjAckMode::STANDARD, identityReason)) {
                result.framed = true;
                result.reason = identityReason;
                return result;
            }
            if (!acknowledgementSucceeded(
                    content, mode == HjAckMode::STANDARD, identityReason)) {
                result.framed = true;
                result.reason = identityReason;
                return result;
            }
            result.accepted = true;
            result.framed = true;
            result.reason = "accepted";
            return result;
        }
        searchFrom = static_cast<int>(contentEnd + 6U);
    }

    if (sawMalformedFrame) {
        result.framed = true;
        result.reason = "frame_invalid";
        return result;
    }
    if (sawFramePrefix || sawCompleteValidFrame) {
        result.framed = true;
        result.reason = "cn_not_9014";
        return result;
    }
    if (mode == HjAckMode::STANDARD) {
        result.reason = "frame_required";
        return result;
    }

    if (mode == HjAckMode::LEGACY) {
        result.accepted = response.indexOf("CN=9014") >= 0;
        result.reason = result.accepted ? "accepted_legacy" : "cn_not_9014";
        return result;
    }

    String cn;
    if (!extractHjField(response, "CN", cn) || cn != "9014") {
        result.reason = "cn_not_9014";
        return result;
    }
    const char* identityReason = "accepted_compatible";
    if (!identitiesMatch(response, expectedQn, expectedMn, false,
                         identityReason)) {
        result.reason = identityReason;
        return result;
    }
    if (!acknowledgementSucceeded(response, false, identityReason)) {
        result.reason = identityReason;
        return result;
    }
    result.accepted = true;
    result.reason = "accepted_compatible";
    return result;
}

} // namespace

DTUManager::DTUManager() {
    _remoteDTU = new DTUDriver("Remote", 1);
    _hj212DTU  = new DTUDriver("HJ212", 2);
}
DTUManager::~DTUManager(){

}
DTUManager& DTUManager::getInstance() {
    static DTUManager instance;
    return instance;
}

const char* DTUManager::hjSerialOwnerName(HjSerialOwner owner)
{
    switch (owner) {
        case HjSerialOwner::TIME_SYNC: return "time_sync";
        case HjSerialOwner::CSQ: return "csq";
        case HjSerialOwner::SET_IP: return "set_ip";
        case HjSerialOwner::LIVE_PACKET: return "live_packet";
        case HjSerialOwner::PENDING_PACKET: return "pending_packet";
        case HjSerialOwner::DTU_COMMAND: return "dtu_command";
        case HjSerialOwner::NONE:
        default: return "none_or_untracked";
    }
}

void DTUManager::setHjSerialOwner(HjSerialOwner owner)
{
    _hjSerialOwnerSinceMs.store(millis(), std::memory_order_release);
    _hjSerialOwner.store(owner, std::memory_order_release);
}

void DTUManager::clearHjSerialOwner()
{
    _hjSerialOwner.store(HjSerialOwner::NONE, std::memory_order_release);
    _hjSerialOwnerSinceMs.store(0, std::memory_order_release);
}

void DTUManager::logHjSerialBusy(const char *requester, uint32_t traceId) const
{
    const HjSerialOwner owner = _hjSerialOwner.load(std::memory_order_acquire);
    const uint32_t since = _hjSerialOwnerSinceMs.load(std::memory_order_acquire);
    const uint32_t heldMs = owner != HjSerialOwner::NONE && since != 0
        ? millis() - since : 0;
    const RuntimeMemorySnapshot memory = observeRuntimeMemory();
    LOG_WARNING("[DIAG] HJ_SERIAL_BUSY req=%s trace=%u owner=%s held_ms=%u uw=%u lw=%u ua=%d la=%d free=%u largest=%u min_free=%u min_largest=%u",
                requester != nullptr ? requester : "unknown", (unsigned)traceId,
                hjSerialOwnerName(owner), (unsigned)heldMs,
                (unsigned)_hjUploadWaiters.load(std::memory_order_acquire),
                (unsigned)_hjLiveWaiters.load(std::memory_order_acquire),
                _hjUploadActive.load(std::memory_order_acquire) ? 1 : 0,
                _hjLiveActive.load(std::memory_order_acquire) ? 1 : 0,
                (unsigned)memory.freeHeap, (unsigned)memory.largestBlock,
                (unsigned)memory.minimumFreeHeap,
                (unsigned)memory.minimumLargestBlock);
}

void DTUManager::init(Stream& remoteStr, Stream& hj212Str) {
    _remoteDTU->setStream(remoteStr);
    _hj212DTU->setStream(hj212Str);
    _queryQueue = EventBus::getInstance().createReceiverQueue(5);
    EventBus::getInstance().subscribe(EventID::DTU_COMMAND_REQ, _queryQueue);
}

uint64_t DTUManager::dtuSystemTime() {
    String res = _remoteDTU->sendCommand(GET_TIME_COMM);
    LOG_DEBUG("Send GET_TIME_COMM res: %s", res.c_str());

    if (res.length() > 0) {
        int year, month, day, hour, minute, second = 0, week = 0;
        int count = sscanf(res.c_str(), "config,nettime,ok,%d,%d,%d,%d,%d,%d,%d", 
                           &year, &month, &day, &hour, &minute, &second, &week);
        if (count >= 5) {
            char buf[20];
            snprintf(buf, sizeof(buf), "%04d%02d%02d%02d%02d%02d",
                     year, month, day, hour, minute, second);
            LOG_INFO("Parsed Time String: %s", buf);
            uint64_t fullTime = strtoull(buf, NULL, 10);
            return fullTime;
        } else {
            LOG_ERROR("Failed to parse time string, count: %d", count);
        }
    }
    return 0;
}

uint64_t DTUManager::hjSystemTime() {
    // Live and pending HJ212 packets always take priority over maintenance
    // time queries. A deferred query is retried by the maintenance scheduler.
    uint32_t now = millis();
    if (_hjUploadWaiters.load(std::memory_order_acquire) > 0 ||
        _hjUploadActive.load(std::memory_order_acquire) ||
        now - _lastHjUploadEndMs.load(std::memory_order_acquire) < 1500) {
        LOG_INFO("[DIAG] TIME_SYNC_DEFER reason=upload_priority");
        return 0;
    }
    SemaphoreHandle_t mutex = SerialManager::getInstance().getMutex(SERIAL_HJ212);
    if (mutex == nullptr || xSemaphoreTake(mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        LOG_INFO("[DIAG] TIME_SYNC_DEFER reason=serial_busy");
        logHjSerialBusy("time_sync");
        return 0;
    }
    setHjSerialOwner(HjSerialOwner::TIME_SYNC);
    if (_hjUploadWaiters.load(std::memory_order_acquire) > 0) {
        clearHjSerialOwner();
        xSemaphoreGive(mutex);
        LOG_INFO("[DIAG] TIME_SYNC_DEFER reason=upload_waiting");
        return 0;
    }
    String res = _hj212DTU->sendCommand(GET_TIME_COMM);
    clearHjSerialOwner();
    xSemaphoreGive(mutex);
    LOG_DEBUG("Send GET_TIME_COMM res: %s", res.c_str());

    if (res.length() > 0) {
        int year, month, day, hour, minute, second = 0, week = 0;
        int count = sscanf(res.c_str(), "config,nettime,ok,%d,%d,%d,%d,%d,%d,%d", 
                           &year, &month, &day, &hour, &minute, &second, &week);
        if (count >= 5) {
            char buf[20];
            snprintf(buf, sizeof(buf), "%04d%02d%02d%02d%02d%02d",
                     year, month, day, hour, minute, second);
            LOG_INFO("Parsed Time String: %s", buf);
            uint64_t fullTime = strtoull(buf, NULL, 10);
            return fullTime;
        } else {
            LOG_ERROR("Failed to parse time string, count: %d", count);
        }
    }
    return 0;
}
int DTUManager::remoteDTUCSQ() {
    String res = _remoteDTU->sendCommand(GET_CSQ_COMM);
    LOG_DEBUG("Send GET_CSQ_COMM res: %s", res.c_str());
    int CSQ_COUNT = 99;
    if (res.length() > 0)
    {
        if (sscanf(res.c_str(), "config,csq,ok,%d", &CSQ_COUNT) == 1)
        {
            LOG_DEBUG(("Parsed CSQ_COUNT: " + String(CSQ_COUNT)).c_str());
            return CSQ_COUNT;
        }
        else
        {
            LOG_DEBUG("Failed to parse CSQ string");
            return CSQ_COUNT;
        }
    }
    return CSQ_COUNT;
}
int DTUManager::hj212DTUCSQ() {
    // Firmware 2.0.3: live data always wins over maintenance commands. A
    // deferred CSQ keeps the last known value and is retried next cycle.
    uint32_t now = millis();
    if (_hjUploadWaiters.load(std::memory_order_acquire) > 0 ||
        _hjUploadActive.load(std::memory_order_acquire) ||
        now - _lastHjUploadEndMs.load(std::memory_order_acquire) < 1500) {
        LOG_INFO("[DIAG] CSQ_DEFER reason=upload_priority");
        return CSQ_DEFERRED;
    }
    SemaphoreHandle_t mutex = SerialManager::getInstance().getMutex(SERIAL_HJ212);
    if (mutex == nullptr || xSemaphoreTake(mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        LOG_INFO("[DIAG] CSQ_DEFER reason=serial_busy");
        logHjSerialBusy("csq");
        return CSQ_DEFERRED;
    }
    setHjSerialOwner(HjSerialOwner::CSQ);
    if (_hjUploadWaiters.load(std::memory_order_acquire) > 0) {
        clearHjSerialOwner();
        xSemaphoreGive(mutex);
        LOG_INFO("[DIAG] CSQ_DEFER reason=upload_waiting");
        return CSQ_DEFERRED;
    }
    String res = _hj212DTU->sendCommand(GET_CSQ_COMM);
    clearHjSerialOwner();
    xSemaphoreGive(mutex);
    int CSQ_COUNT = 99;
    if (res.length() > 0)
    {
        if (sscanf(res.c_str(), "config,csq,ok,%d", &CSQ_COUNT) == 1)
        {
            LOG_DEBUG(("Parsed CSQ_COUNT: " + String(CSQ_COUNT)).c_str());
            return CSQ_COUNT;
        }
        else
        {
            LOG_DEBUG("Failed to parse CSQ string");
            return CSQ_COUNT;
        }
    }
    return CSQ_COUNT;
}
bool DTUManager::updateReDtuGoalIP(String newIP){
    int sep = newIP.indexOf(':');
    if (sep < 0) return false;
    String ip = newIP.substring(0, sep);
    String port = newIP.substring(sep + 1);
    String cmd = String(SET_TCPIP_COM_Uart) + ip + "," + port + String(SET_IP_END);
    LOG_DEBUG("Sending to DTU: %s", cmd.c_str());
    String res = _remoteDTU->sendCommand(cmd.c_str());
    LOG_DEBUG("DTU Response: %s", res.c_str());
    if (res.indexOf("ok") != -1) { 
        LOG_DEBUG("Server set success!");
        _remoteDTU->sendCommand(CONFIG_SAVE_COM); // ��������
        return true; // ���سɹ����ⲿ������Ӧֹͣѭ������
    } else {
        LOG_ERROR("Server set failed! DTU Response error.");
        return false;
    }
}

bool DTUManager::updateHJDtuGoalIP(String newIP){
    int sep = newIP.indexOf(':');
    if (sep < 0) return false;
    String ip = newIP.substring(0, sep);
    String port = newIP.substring(sep + 1);
    String cmd = String(SET_TCPIP_COM_Uart) + ip + "," + port + String(SET_IP_END);
    LOG_DEBUG("Sending to DTU: %s", cmd.c_str());
    SemaphoreHandle_t mutex = SerialManager::getInstance().getMutex(SERIAL_HJ212);
    if (mutex == nullptr || xSemaphoreTake(mutex, pdMS_TO_TICKS(3000)) != pdTRUE) {
        LOG_WARNING("[DIAG] HJ_COMMAND_BUSY command=set_ip");
        logHjSerialBusy("set_ip");
        return false;
    }
    setHjSerialOwner(HjSerialOwner::SET_IP);
    String res = _hj212DTU->sendCommand(cmd.c_str());
    LOG_DEBUG("DTU Response: %s", res.c_str());
    if (res.indexOf("ok") != -1) { 
        LOG_DEBUG("Server set success!");
        _hj212DTU->sendCommand(CONFIG_SAVE_COM);
        clearHjSerialOwner();
        xSemaphoreGive(mutex);
        return true;
    } else {
        clearHjSerialOwner();
        xSemaphoreGive(mutex);
        LOG_ERROR("Server set failed! DTU Response error.");
        return false;
    }
}
bool DTUManager::sendHJ212Packet(const String& dataContent, int maxRetry,
                                 uint32_t traceId, int dataType,
                                 uint64_t dataTimestamp) {
    return sendHJ212PacketInternal(
               dataContent, maxRetry, traceId, dataType, dataTimestamp, false) ==
           Hj212SendResult::SENT;
}

Hj212SendResult DTUManager::sendPendingHJ212Packet(
    const String& dataContent, int maxRetry, uint32_t traceId, int dataType,
    uint64_t dataTimestamp) {
    return sendHJ212PacketInternal(
        dataContent, maxRetry, traceId, dataType, dataTimestamp, true);
}

Hj212SendResult DTUManager::sendHJ212PacketInternal(
    const String& dataContent, int maxRetry, uint32_t traceId, int dataType,
    uint64_t dataTimestamp, bool lowPriorityPending) {
    uint8_t flagValue = 0;
    if (!parseHj212Flag(dataContent, flagValue)) {
        LOG_ERROR("HJ212 Flag missing or invalid");
        return Hj212SendResult::FAILED;
    }

    // Bit 1 indicates split packets. This firmware does not generate PNUM/PNO,
    // so accepting a split Flag would produce a non-compliant packet.
    if ((flagValue & 0x02U) != 0U) {
        LOG_ERROR("HJ212 split packet Flag unsupported: %u",
                  (unsigned)flagValue);
        return Hj212SendResult::FAILED;
    }

    // The least significant bit is the ACK bit in both HJ 212-2017 and 2025.
    const bool requiresAck = (flagValue & 0x01U) != 0U;
    const HJ212CONFIG hjConfig = ConfigManager::getInstance().getHJ212();
    const HjAckMode ackMode = parseAckMode(hjConfig.ack_mode);
    const int configuredTimeout = hjConfig.timeout < 1
        ? 1 : (hjConfig.timeout > 60 ? 60 : hjConfig.timeout);
    const uint32_t responseTimeoutMs =
        static_cast<uint32_t>(configuredTimeout) * 1000U;
    const int configuredRetries = hjConfig.retry_times < 1
        ? 1 : (hjConfig.retry_times > 10 ? 10 : hjConfig.retry_times);
    if (maxRetry <= 0) maxRetry = configuredRetries;
    if (maxRetry > 10) maxRetry = 10;

    String expectedQn;
    String expectedMn;
    if (requiresAck &&
        (!extractHjField(dataContent, "QN", expectedQn) ||
         !extractHjField(dataContent, "MN", expectedMn))) {
        LOG_ERROR("[DIAG] ACK_REJECT trace=%u mode=%s reason=request_identity_missing",
                  (unsigned)traceId, ackModeName(ackMode));
        return Hj212SendResult::FAILED;
    }
    observeRuntimeMemory();

    LOG_INFO("[DIAG] HJ_POLICY trace=%u source=%s ack_mode=%s timeout_ms=%u attempts=%d",
             (unsigned)traceId,
             lowPriorityPending ? "pending" : "live",
             ackModeName(ackMode),
             (unsigned)responseTimeoutMs, maxRetry);

    // Firmware 2.0.3: the DTU manager is the single owner of SERIAL_HJ212
    // arbitration. Callers no longer take the same lock independently.
    // Firmware 2.0.22: pending recovery never waits behind live traffic. The
    // second check after taking the mutex closes the check-before-lock race.
    if (lowPriorityPending &&
        (_hjLiveWaiters.load(std::memory_order_acquire) > 0 ||
         _hjLiveActive.load(std::memory_order_acquire))) {
        LOG_INFO("[DIAG] PENDING_TX_DEFER reason=live_priority");
        return Hj212SendResult::DEFERRED;
    }
    if (!lowPriorityPending) {
        _hjLiveWaiters.fetch_add(1, std::memory_order_acq_rel);
    }
    _hjUploadWaiters.fetch_add(1, std::memory_order_acq_rel);
    SemaphoreHandle_t mutex = SerialManager::getInstance().getMutex(SERIAL_HJ212);
    bool locked = mutex != nullptr &&
                  xSemaphoreTake(
                      mutex,
                      lowPriorityPending ? pdMS_TO_TICKS(100)
                                         : pdMS_TO_TICKS(3000)) == pdTRUE;
    _hjUploadWaiters.fetch_sub(1, std::memory_order_acq_rel);
    if (!lowPriorityPending) {
        _hjLiveWaiters.fetch_sub(1, std::memory_order_acq_rel);
    }
    if (!locked) {
        if (lowPriorityPending) {
            LOG_INFO("[DIAG] PENDING_TX_DEFER reason=serial_busy");
            logHjSerialBusy("pending", traceId);
            return Hj212SendResult::DEFERRED;
        }
        LOG_WARNING("[DIAG] HJ_TX_DEFER trace=%u reason=serial_busy",
                    (unsigned)traceId);
        logHjSerialBusy("live", traceId);
        return Hj212SendResult::FAILED;
    }
    setHjSerialOwner(lowPriorityPending
        ? HjSerialOwner::PENDING_PACKET : HjSerialOwner::LIVE_PACKET);
    if (lowPriorityPending &&
        _hjLiveWaiters.load(std::memory_order_acquire) > 0) {
        clearHjSerialOwner();
        xSemaphoreGive(mutex);
        LOG_INFO("[DIAG] PENDING_TX_DEFER reason=live_waiting");
        return Hj212SendResult::DEFERRED;
    }
    _hjUploadActive.store(true, std::memory_order_release);
    if (!lowPriorityPending) {
        _hjLiveActive.store(true, std::memory_order_release);
    }
    bool success = false;
    for (int retry = 1; retry <= maxRetry; retry++)
    {
        LOG_INFO("[DIAG] TX_ATTEMPT trace=%u type=%d timestamp=%llu attempt=%d bytes=%u",
                 (unsigned)traceId, dataType, dataTimestamp,
                 retry, (unsigned)dataContent.length());
        String res = _hj212DTU->sendData(dataContent, requiresAck,
                                         responseTimeoutMs);
        observeRuntimeMemory();
        if (!requiresAck)
        {
            if (res == "TX_OK")
            {
                LOG_INFO("[DIAG] TX_RESULT trace=%u type=%d timestamp=%llu attempt=%d flag=%u ack_required=0 sent=1",
                         (unsigned)traceId, dataType, dataTimestamp, retry,
                         (unsigned)flagValue);
                success = true;
                break;
            }
            LOG_WARNING("HJ212 DTU write failed without ACK requirement");
        }
        else if (res.length() > 0)
        {
            const AckValidationResult validation = validate9014Response(
                res, expectedQn, expectedMn, ackMode);
            if (validation.accepted)
            {
                LOG_INFO("[DIAG] ACK_ACCEPT trace=%u mode=%s framed=%d response_bytes=%u",
                         (unsigned)traceId, ackModeName(ackMode),
                         validation.framed ? 1 : 0, (unsigned)res.length());
                LOG_INFO("[DIAG] TX_RESULT trace=%u type=%d timestamp=%llu attempt=%d ack=1 response_bytes=%u",
                         (unsigned)traceId, dataType, dataTimestamp,
                         retry, (unsigned)res.length());
                success = true;
                break;
            }
            else
            {
                LOG_WARNING("[DIAG] ACK_REJECT trace=%u mode=%s framed=%d reason=%s response_bytes=%u",
                            (unsigned)traceId, ackModeName(ackMode),
                            validation.framed ? 1 : 0,
                            validation.reason, (unsigned)res.length());
            }
        }
        else
        {
            LOG_DEBUG("No response from HJ212 DTU.");
        }
        if (retry < maxRetry) vTaskDelay(pdMS_TO_TICKS(5000));
    }
    if (!lowPriorityPending) {
        _hjLiveActive.store(false, std::memory_order_release);
    }
    _hjUploadActive.store(false, std::memory_order_release);
    _lastHjUploadEndMs.store(millis(), std::memory_order_release);
    clearHjSerialOwner();
    xSemaphoreGive(mutex);
    SerialManager::getInstance().checkAndReportOverflow(SERIAL_HJ212);
    if (!success) {
        LOG_ERROR("[DIAG] TX_RESULT trace=%u type=%d timestamp=%llu attempts=%d flag=%u ack_required=%d success=0",
                  (unsigned)traceId, dataType, dataTimestamp, maxRetry,
                  (unsigned)flagValue, requiresAck ? 1 : 0);
    }
    return success ? Hj212SendResult::SENT : Hj212SendResult::FAILED;
}
void DTUManager::processQuery(JSONCmdData* req){
    
    configData* resData = new configData();
    resData->cmd = req->command;
    config_json parser;
    String com;
    String dtuName;
    if (parser.parse(req->arguments.c_str())) {
        com = parser.getString("command", req->arguments);
        dtuName = parser.getString("dtuName", req->arguments);
    } else {
        com = req->arguments;
        dtuName = req->arguments;
    }
    LOG_DEBUG("command : %s", req->command);
    if (dtuName == "Remote") {
        String res = _remoteDTU->sendCommand(com.c_str());
        resData->content = res;
    } else if (dtuName == "HJ212") {
        SemaphoreHandle_t mutex = SerialManager::getInstance().getMutex(SERIAL_HJ212);
        if (mutex != nullptr && xSemaphoreTake(mutex, pdMS_TO_TICKS(3000)) == pdTRUE) {
            setHjSerialOwner(HjSerialOwner::DTU_COMMAND);
            resData->content = _hj212DTU->sendCommand(com.c_str());
            clearHjSerialOwner();
            xSemaphoreGive(mutex);
        } else {
            logHjSerialBusy("dtu_command");
            resData->content = "HJ212 busy";
        }
    } else {
        LOG_ERROR("Unknown DTU name: %s", dtuName.c_str());
        resData->content = "Unknown DTU";
    }

    int subCount = EventBus::getInstance().getSubscriberCount(EventID::CONFIG_QUERY_RES);
    for (int i = 0; i < subCount; i++) resData->retain(); 
    EventBus::getInstance().publish(EventID::CONFIG_QUERY_RES, resData);
    resData->release();
}
void DTUManager::poll(){
    EventMsg msg;
    if (EventBus::waitEvent(_queryQueue, msg)) {
        if (msg.id == EventID::DTU_COMMAND_REQ) {
            JSONCmdData* req = (JSONCmdData*)msg.data;
            processQuery(req);
            req->release();
        } else {
            LOG_ERROR("DataManager not subseribe this, send message error!");
        }
    }
}
