#include "dtuManager.h"
#include "../../module/pack212/pack212.h"

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
    String res = _hj212DTU->sendCommand(GET_TIME_COMM);
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
    String res = _hj212DTU->sendCommand(GET_CSQ_COMM);
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
        _remoteDTU->sendCommand(CONFIG_SAVE_COM); // 保存配置
        return true; // 返回成功，外部调用者应停止循环调用
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
    String res = _hj212DTU->sendCommand(cmd.c_str());
    LOG_DEBUG("DTU Response: %s", res.c_str());
    if (res.indexOf("ok") != -1) { 
        LOG_DEBUG("Server set success!");
        _hj212DTU->sendCommand(CONFIG_SAVE_COM);
        return true;
    } else {
        LOG_ERROR("Server set failed! DTU Response error.");
        return false;
    }
}
void DTUManager::sendHJ212Packet(String dataContent) {
    String packet = dataContent;
    const int MAX_RETRY = 3;
    for (int retry = 1; retry <= MAX_RETRY; retry++)
    {
        LOG_DEBUG("HJ212 Send Attempt: %d", retry);
        String res = _hj212DTU->sendData(packet);
        if (res.length() > 0)
        {
            //LOG_DEBUG("HJ212 DTU Response: %s", res.c_str());
            if (res.indexOf("CN=9014") != -1)
            {
                LOG_DEBUG("HJ212 ACK Success.");
                return;
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
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    return;
}

void DTUManager::poll(){
    
}