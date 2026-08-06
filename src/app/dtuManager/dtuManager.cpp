#include "dtuManager.h"
#include "../../module/pack212/pack212.h"
#include "../../system/event/eventBus.h"
#include "../../module/json/config_json.h"
#include "../../module/Serial/SerialManager.h"

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
        int year, month, day, hour, minute, second, week;
        int count = sscanf(res.c_str(), "config,nettime,ok,%d,%d,%d,%d,%d,%d,%d", 
                           &year, &month, &day, &hour, &minute, &second, &week);
        if (count >= 5) {
            char buf[16];
            snprintf(buf, sizeof(buf), "%04d%02d%02d%02d%02d", year, month, day, hour, minute);
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
    SemaphoreHandle_t mutex = SerialManager::getInstance().getMutex(SERIAL_HJ212);
    if (mutex == nullptr || xSemaphoreTake(mutex, pdMS_TO_TICKS(3000)) != pdTRUE) {
        LOG_WARNING("[DIAG] HJ_COMMAND_BUSY command=time");
        return 0;
    }
    String res = _hj212DTU->sendCommand(GET_TIME_COMM);
    xSemaphoreGive(mutex);
    LOG_DEBUG("Send GET_TIME_COMM res: %s", res.c_str());

    if (res.length() > 0) {
        int year, month, day, hour, minute, second, week;
        int count = sscanf(res.c_str(), "config,nettime,ok,%d,%d,%d,%d,%d,%d,%d", 
                           &year, &month, &day, &hour, &minute, &second, &week);
        if (count >= 5) {
            char buf[16];
            snprintf(buf, sizeof(buf), "%04d%02d%02d%02d%02d", year, month, day, hour, minute);
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
        return CSQ_DEFERRED;
    }
    if (_hjUploadWaiters.load(std::memory_order_acquire) > 0) {
        xSemaphoreGive(mutex);
        LOG_INFO("[DIAG] CSQ_DEFER reason=upload_waiting");
        return CSQ_DEFERRED;
    }
    String res = _hj212DTU->sendCommand(GET_CSQ_COMM);
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
        return false;
    }
    String res = _hj212DTU->sendCommand(cmd.c_str());
    LOG_DEBUG("DTU Response: %s", res.c_str());
    if (res.indexOf("ok") != -1) { 
        LOG_DEBUG("Server set success!");
        _hj212DTU->sendCommand(CONFIG_SAVE_COM);
        xSemaphoreGive(mutex);
        return true;
    } else {
        xSemaphoreGive(mutex);
        LOG_ERROR("Server set failed! DTU Response error.");
        return false;
    }
}
bool DTUManager::sendHJ212Packet(const String& dataContent, int maxRetry,
                                 uint32_t traceId, int dataType,
                                 uint64_t dataTimestamp) {
    // Firmware 2.0.3: the DTU manager is the single owner of SERIAL_HJ212
    // arbitration. Callers no longer take the same lock independently.
    _hjUploadWaiters.fetch_add(1, std::memory_order_acq_rel);
    SemaphoreHandle_t mutex = SerialManager::getInstance().getMutex(SERIAL_HJ212);
    bool locked = mutex != nullptr &&
                  xSemaphoreTake(mutex, pdMS_TO_TICKS(3000)) == pdTRUE;
    _hjUploadWaiters.fetch_sub(1, std::memory_order_acq_rel);
    if (!locked) {
        LOG_WARNING("[DIAG] HJ_TX_DEFER trace=%u reason=serial_busy",
                    (unsigned)traceId);
        return false;
    }
    _hjUploadActive.store(true, std::memory_order_release);
    if (maxRetry < 1) maxRetry = 1;
    bool success = false;
    for (int retry = 1; retry <= maxRetry; retry++)
    {
        LOG_INFO("[DIAG] TX_ATTEMPT trace=%u type=%d timestamp=%llu attempt=%d bytes=%u",
                 (unsigned)traceId, dataType, dataTimestamp,
                 retry, (unsigned)dataContent.length());
        String res = _hj212DTU->sendData(dataContent);
        if (res.length() > 0)
        {
            //LOG_DEBUG("HJ212 DTU Response: %s", res.c_str());
            if (res.indexOf("CN=9014") != -1)
            {
                LOG_INFO("[DIAG] TX_RESULT trace=%u type=%d timestamp=%llu attempt=%d ack=1 response_bytes=%u",
                         (unsigned)traceId, dataType, dataTimestamp,
                         retry, (unsigned)res.length());
                success = true;
                break;
            }
            else
            {
                LOG_DEBUG("HJ212 ACK Invalid, retry...");
            }
        }
        else
        {
            LOG_DEBUG("No response from HJ212 DTU.");
        }
        if (retry < maxRetry) vTaskDelay(pdMS_TO_TICKS(5000));
    }
    _hjUploadActive.store(false, std::memory_order_release);
    _lastHjUploadEndMs.store(millis(), std::memory_order_release);
    xSemaphoreGive(mutex);
    SerialManager::getInstance().checkAndReportOverflow(SERIAL_HJ212);
    if (!success) {
        LOG_ERROR("[DIAG] TX_RESULT trace=%u type=%d timestamp=%llu attempts=%d ack=0",
                  (unsigned)traceId, dataType, dataTimestamp, maxRetry);
    }
    return success;
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
            resData->content = _hj212DTU->sendCommand(com.c_str());
            xSemaphoreGive(mutex);
        } else {
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
