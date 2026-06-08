#include "config.h"
#include "config_json.h"
#include "system_json.h"
#include "model_json.h"
#include "../../module/log/log_manager.h"
#include "../../inc/sys_init.h"
#include "../../system/event/eventBus.h"
#include <Ds1302.h>

ConfigManager::ConfigManager() : fs(file_storage::getInstance())
{
    systemSetup.netCsq = 99;
    systemSetup.time = 0;
    systemSetup.mutex = xSemaphoreCreateMutex();
    LOG_INFO("ConfigManager Instance Created.");
}

void ConfigManager::begin(){
    LOG_INFO("FFAT and SDcart Init sucess, start load system file!");
    int res = fs.begin();
    if (res == 1){
        LOG_ERROR("FFAT Init error");
    } else if (res == 2){
        LOG_ERROR("SDcard Init error");
    }
    if (!loadFromFile(CONFIG_PATH)){
        LOG_ERROR("Load CONFIG_PATH error");
    }

    if (!loadFromFile(MODEL_PATH)){
        LOG_ERROR("Load MODEL_PATH error");
    }
    if (!loadFromFile(SYSTEM_PATH)){
        LOG_ERROR("Load SYSTEM_PATH error");
    }
    if (!loadFromFile(TEMP_CONTROL_PATH)){
        LOG_ERROR("Load TEMP_CONTROL_PATH error");
    }
    if (!loadFromFile(HJ212_PATH)){
        LOG_ERROR("Load HJ212_PATH error");
    }
    if (!loadFromFile(SWITCH_PATH)){
        LOG_ERROR("Load SWITCH_PATH error");
    }
    if (!loadFromFile(ALARM_PATH)){
        LOG_ERROR("Load ALARM_PATH error");
    }
    LOG_INFO("======== Global Configuration Dump ========");

    // 1. 打印 System 配置
    LOG_INFO("[System Config]");
    LOG_INFO("  Collect Time: %d s", globalCfg.system.collect_time);
    LOG_INFO("  Upload Interval: %d s", globalCfg.system.upload_interval);
    LOG_INFO("  DTU Server: %s", globalCfg.system.dtu_server.c_str());

    LOG_INFO("  Toggles -> Raw:%d, Min:%d, HJ212:%d, DTU:%d",
             globalCfg.systemSwitch.save_raw_data, globalCfg.systemSwitch.save_min_data,
             globalCfg.systemSwitch.enable_hj212, globalCfg.systemSwitch.enable_remote_dtu);

    // 2. 打印 温控 配置
    LOG_INFO("[TempControl Config]");
    LOG_INFO("  Switch: %s", globalCfg.systemSwitch.tempConSwitch ? "ON" : "OFF");
    LOG_INFO("  Temp Range: [%d - %d]", globalCfg.tempControl.tempLowerLimit, globalCfg.tempControl.tempUpperLimit);
    LOG_INFO("  Humi Range: [%d - %d]", globalCfg.tempControl.wetnLowerLimit, globalCfg.tempControl.wetnUpperLimit);

    // 3. 打印 HJ212 配置
    LOG_INFO("[HJ212 Config]");
    LOG_INFO("  Server: %s", globalCfg.hj212.ip.c_str());
    LOG_INFO("  MN: %s, PW: %s", globalCfg.hj212.mn.c_str(), globalCfg.hj212.pw.c_str());

    // 4. 打印 传感器 (Map 遍历)
    LOG_INFO("[Sensor Collections] Total: %d", globalCfg.collectConfig.size());
    for (auto const& [id, cfg] : globalCfg.collectConfig) {
        LOG_INFO("  - ID: %s | Name: %s | Unit: %s",
                 id.c_str(), cfg.name.c_str(), cfg.unit.c_str());
    }

    // 5. 打印 报警 配置
    LOG_INFO("[Alarm Config]");
    LOG_INFO("  Sensor: %s | Upper Limit: %.2f | Lower Limit: %.2f | Switch: %s",
             globalCfg.alarmConfig.alarm_sensor.c_str(),
             globalCfg.alarmConfig.alarm_upper_limit,
             globalCfg.alarmConfig.alarm_lower_limit,
             globalCfg.alarmConfig.alarm_switch ? "ON" : "OFF");
    
    _queryQueue = EventBus::getInstance().createReceiverQueue(10);
    EventBus::getInstance().subscribe(EventID::CONFIG_QUERY_REQ, _queryQueue);
    EventBus::getInstance().subscribe(EventID::CONFIG_SET_REQ, _queryQueue);
    LOG_INFO("============================================");
    LOG_INFO("Load file system success!");
}
void ConfigManager::_parseSystem(cJSON *node, SYSTEMCONFIG &target)
{
    if (!node) return;
    cJSON *item;
    if ((item = cJSON_GetObjectItem(node, "collect_time")) && cJSON_IsNumber(item))
        target.collect_time = item->valueint;
    if ((item = cJSON_GetObjectItem(node, "upload_interval")) && cJSON_IsNumber(item))
        target.upload_interval = item->valueint;
    if ((item = cJSON_GetObjectItem(node, "dtu_server")) && cJSON_IsString(item))
        target.dtu_server = item->valuestring;
}
void ConfigManager::_parseTempControl(cJSON *node, TEMPCONTROLCONFIG &target) {
    if (!node) return;
    cJSON *item;
    if ((item = cJSON_GetObjectItem(node, "tempUpperLimit")) && cJSON_IsNumber(item))
        target.tempUpperLimit = item->valueint;
    if ((item = cJSON_GetObjectItem(node, "tempLowerLimit")) && cJSON_IsNumber(item))
        target.tempLowerLimit = item->valueint;
    if ((item = cJSON_GetObjectItem(node, "wetnUpperLimit")) && cJSON_IsNumber(item))
        target.wetnUpperLimit = item->valueint;
    if ((item = cJSON_GetObjectItem(node, "wetnLowerLimit")) && cJSON_IsNumber(item))
        target.wetnLowerLimit = item->valueint;
}
void ConfigManager::_parseHJ212(cJSON *node, HJ212CONFIG &target)
{
    if (!node) return;
    cJSON *item;
    if ((item = cJSON_GetObjectItem(node, "ip")) && cJSON_IsString(item))
        target.ip = item->valuestring;
    if ((item = cJSON_GetObjectItem(node, "mn")) && cJSON_IsString(item))
        target.mn = item->valuestring;
    if ((item = cJSON_GetObjectItem(node, "pw")) && cJSON_IsString(item))
        target.pw = item->valuestring;
    if ((item = cJSON_GetObjectItem(node, "st")) && cJSON_IsString(item))
        target.st = item->valuestring;
    if ((item = cJSON_GetObjectItem(node, "pv")) && cJSON_IsString(item))
        target.protocol_version = item->valuestring;
}
void ConfigManager::_parseSensors(cJSON *objectNode, COLLECTMAP &target_map)
{
    if (!cJSON_IsObject(objectNode)) {
        LOG_ERROR("Sensors node is NOT an object!");
        return;
    }
    char *rawJson = cJSON_Print(objectNode); // 格式化打印（带缩进）
    if (rawJson) {
        LOG_INFO("--- [Debug] Sensors JSON Node Content ---");
        LOG_INFO("\n%s", rawJson);
        LOG_INFO("-----------------------------------------");
        cJSON_free(rawJson); // 必须手动释放 cJSON_Print 分配的内存
    } else {
        LOG_ERROR("Failed to print sensors JSON node.");
    }
    cJSON *sensorEntry = nullptr;
    cJSON_ArrayForEach(sensorEntry, objectNode)
    {
        if (sensorEntry->string == nullptr) continue;
        String s_id = String(sensorEntry->string);
        COLLECTCONFIG &s = target_map[s_id];
        s.id = s_id;
        cJSON *attr = nullptr;
        if ((attr = cJSON_GetObjectItem(sensorEntry, "alarmLimit")) && cJSON_IsNumber(attr))
            s.alarmLimit = attr->valueint;

        if ((attr = cJSON_GetObjectItem(sensorEntry, "name")) && cJSON_IsString(attr))
            s.name = String(attr->valuestring); 

        if ((attr = cJSON_GetObjectItem(sensorEntry, "unit")) && cJSON_IsString(attr))
            s.unit = String(attr->valuestring);
            
        LOG_INFO("Successfully parsed sensor: %s (%s)", s.id.c_str(), s.name.c_str());
    }
}
void ConfigManager::_parseSwitch(cJSON *node, SYSTEMSWITCH &target) {
    if (!node) return;
    cJSON *item;
    if ((item = cJSON_GetObjectItem(node, "tempConSwitch")) && cJSON_IsBool(item))
        target.tempConSwitch = cJSON_IsTrue(item);
    if ((item = cJSON_GetObjectItem(node, "save_raw_data")) && cJSON_IsBool(item))
        target.save_raw_data = cJSON_IsTrue(item);
    if ((item = cJSON_GetObjectItem(node, "log_to_sd")) && cJSON_IsBool(item))
        target.log_to_sd = cJSON_IsTrue(item);
    if ((item = cJSON_GetObjectItem(node, "save_min_data")) && cJSON_IsBool(item))
        target.save_min_data = cJSON_IsTrue(item);
    if ((item = cJSON_GetObjectItem(node, "save_hour_data")) && cJSON_IsBool(item))
        target.save_hour_data = cJSON_IsTrue(item);
    if ((item = cJSON_GetObjectItem(node, "save_day_data")) && cJSON_IsBool(item))
        target.save_day_data = cJSON_IsTrue(item);
    if ((item = cJSON_GetObjectItem(node, "enable_hj212")) && cJSON_IsBool(item))
        target.enable_hj212 = cJSON_IsTrue(item);
    if ((item = cJSON_GetObjectItem(node, "enable_remote_dtu")) && cJSON_IsBool(item))
        target.enable_remote_dtu = cJSON_IsTrue(item);
    if ((item = cJSON_GetObjectItem(node, "mqtt_public")) && cJSON_IsBool(item))
        target.mqtt_public = cJSON_IsTrue(item);
}

void ConfigManager::_parseSystemInfo(cJSON *node, SYSTEMCONFIG &target) {
    
}
void ConfigManager::_parseAlarm(cJSON *node, ALARMCONFIG &target) {
    if (!node) return;
    cJSON *item;
    if ((item = cJSON_GetObjectItem(node, "alarm_sensor")) && cJSON_IsString(item))
        target.alarm_sensor = String(item->valuestring);
    if ((item = cJSON_GetObjectItem(node, "alarm_upper_limit")) && cJSON_IsNumber(item))
        target.alarm_upper_limit = item->valuedouble;
    if ((item = cJSON_GetObjectItem(node, "alarm_lower_limit")) && cJSON_IsNumber(item))
        target.alarm_lower_limit = item->valuedouble;
    if ((item = cJSON_GetObjectItem(node, "alarm_switch")) && cJSON_IsBool(item))
        target.alarm_switch = cJSON_IsTrue(item);
}

bool ConfigManager::loadFromFile(const char *path)
{
    LOG_INFO("ConfigManager: Loading %s", path);
    if (strcmp(path, MODEL_PATH) == 0) {
        globalCfg.collectConfig.clear();
    }
    String content;
    int ret = fs.readFFAT(path, content);
    if (ret == 2) 
    {
        LOG_WARNING("File %s missing. Writing default template...", path);
        const char *defaultTemplate = nullptr;
        if (strcmp(path, CONFIG_PATH) == 0) defaultTemplate = CONFIG_JSON;
        else if (strcmp(path, MODEL_PATH) == 0) defaultTemplate = MODEL_JSON;
        else if (strcmp(path, SYSTEM_PATH) == 0) defaultTemplate = SYSTEMINFO;
        else if (strcmp(path, TEMP_CONTROL_PATH) == 0) defaultTemplate = TEMP_CONTROL_JSON;
        else if (strcmp(path, HJ212_PATH) == 0) defaultTemplate = HJ212_JSON;
        else if (strcmp(path, SWITCH_PATH) == 0) defaultTemplate = SWITCH_JSON;
        else if (strcmp(path, ALARM_PATH) == 0) defaultTemplate = ALARM_JSON;

        if (defaultTemplate && fs.writeFFAT(path, defaultTemplate) == 3) {
            content = String(defaultTemplate);
            ret = 0;
        } else {
            LOG_ERROR("Failed to restore default file: %s", path);
            return false;
        }
    }
    if (ret == 0)
    {
        config_json parser;
        if (!parser.parse(content.c_str()))
        {
            LOG_ERROR("JSON Syntax Error in %s! Parsing aborted.", path);
            return false;
        }
        if (parser.isValid()) {
            LOG_INFO("Parsed JSON content of %s successfully.", path);
        } else {
            LOG_ERROR("Parsed JSON content of %s is invalid!", path);
            return false;
        }
        cJSON *root = parser.getJsonObject();
        if (root){
            if (strcmp(path, CONFIG_PATH) == 0) {
                _parseSystem(root, globalCfg.system);
            } else if (strcmp(path, MODEL_PATH) == 0) {
                _parseSensors(root, globalCfg.collectConfig);
            } else if (strcmp(path, SYSTEM_PATH) == 0) {
               // _parseSystemInfo(root, globalCfg.system);
            } else if (strcmp(path, TEMP_CONTROL_PATH) == 0) {
                _parseTempControl(root, globalCfg.tempControl);
            } else if (strcmp(path, HJ212_PATH) == 0) {
                _parseHJ212(root, globalCfg.hj212);
            } else if (strcmp(path, SWITCH_PATH) == 0) {
                _parseSwitch(root, globalCfg.systemSwitch);
            } else if (strcmp(path, ALARM_PATH) == 0) {
                _parseAlarm(root, globalCfg.alarmConfig);
            }
        } else {
            LOG_ERROR("Root JSON node is null in %s! Parsing aborted.", path);
            return false;
        }
        LOG_INFO("Loaded %s successfully.", path);
        return true;
    }
    return false;
}
String ConfigManager::getConfigJson(const char *path){
    String content;
    int ret = fs.readFFAT(path, content);
    if (ret == 2) 
    {
        LOG_WARNING("File %s missing. Writing default template...", path);
    }
    if (ret == 0)
    {
        config_json parser;
        if (!parser.parse(content.c_str()))
        {
            LOG_ERROR("JSON Syntax Error in %s! Parsing aborted.", path);
        }
    }
    return content;
}
bool ConfigManager::updateSystem(const char *json_str)
{
    config_json parser;
    if (!parser.parse(json_str))
        return false;
    cJSON *node = parser.getJsonObject();
    _parseSystem(node, globalCfg.system);
    return true;
}

bool ConfigManager::updateSensors(const char *json_str)
{
    if (json_str == nullptr || strlen(json_str) == 0) return false;
    cJSON *root = cJSON_Parse(json_str);
    if (!root) return false;

    if (!cJSON_IsObject(root)) {
        LOG_ERROR("updateSensors: root JSON is not an object.");
        cJSON_Delete(root);
        return false;
    }

    _parseSensors(root, globalCfg.collectConfig);
    cJSON_Delete(root);
    return true;
}
bool ConfigManager::updateSetup(const SYSTEM_SETUP& newSetup)
{
    if (systemSetup.mutex == NULL) return false;
    if (xSemaphoreTake(systemSetup.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        systemSetup.netCsq = newSetup.netCsq;
        systemSetup.time = newSetup.time;
        xSemaphoreGive(systemSetup.mutex);
        return true;
    }
    LOG_WARNING("Update SystemSetup failed: Mutex busy");
    return false;
}
SYSTEM_SETUP ConfigManager::getSetup() {
    SYSTEM_SETUP currentSetup;
    if (systemSetup.mutex != NULL && xSemaphoreTake(systemSetup.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        currentSetup.netCsq = systemSetup.netCsq;
        currentSetup.time = systemSetup.time;
        xSemaphoreGive(systemSetup.mutex);
    } else {
        currentSetup.netCsq = 99;
        currentSetup.time = 0;
    }
    return currentSetup;
}
bool ConfigManager::saveToFile(const char *path)
{
    return true;
}
bool ConfigManager::saveConfig(const char *path, const String &content)
{    
    if (path == nullptr || strlen(path) == 0) return false;

    return fs.writeFFAT(path, content.c_str()) == 3;
}
bool ConfigManager::removeConfig(const char *path)
{
    if (path == nullptr || strlen(path) == 0) return false;

    LOG_WARNING("ConfigManager: Deleting file: %s", path);
    if (!fs.FFATremoveFile(path))
    {
        LOG_ERROR("ConfigManager: Could not delete %s", path);
        return false;
    }
    if (strcmp(path, CONFIG_PATH) == 0)
    {
        globalCfg.hj212 = {};
        LOG_INFO("Active HJ212 memory cleared.");
    }
    else if (strcmp(path, MODEL_PATH) == 0)
    {
        globalCfg.collectConfig.clear();
        LOG_INFO("Active Sensor memory cleared.");
    } else if  (strcmp(path, SYSTEM_PATH) == 0) {
        globalCfg.system = {};
        LOG_INFO("Active System memory cleared.");
    }
    else if (strcmp(path, TEMP_CONTROL_PATH) == 0)
    {
        globalCfg.tempControl = {};
        LOG_INFO("Active TempControl memory cleared.");
    }
    else if (strcmp(path, HJ212_PATH) == 0)
    {
        globalCfg.hj212 = {};
        LOG_INFO("Active HJ212 memory cleared.");
    }
    else if (strcmp(path, SWITCH_PATH) == 0)
    {
        globalCfg.systemSwitch = {};
        LOG_INFO("Active Switch memory cleared.");
    }

    LOG_INFO("Config %s removal process finished.", path);
    return true;
}

void ConfigManager::poll(){
    EventMsg msg;
    if (EventBus::waitEvent(_queryQueue, msg)) {
        if (msg.id == EventID::CONFIG_QUERY_REQ) {
            JSONCmdData* req = (JSONCmdData*)msg.data;
            LOG_DEBUG("ConfigManager received CONFIG_QUERY_REQ: %s", req->arguments.c_str());
            processQuery(req);
            req->release();
        } else if (msg.id == EventID::CONFIG_SET_REQ) {
            JSONCmdData* req = (JSONCmdData*)msg.data;
            LOG_DEBUG("ConfigManager received CONFIG_SET_REQ: %s", req->arguments.c_str());
            processQuery(req);
            req->release();
        } else {
            LOG_ERROR("DataManager not subseribe this, send message error!");
        }
    }
}

void ConfigManager::processQuery(JSONCmdData* req){
    String cmd = req->command;
    if (cmd == "get_config"){
        getConfigRes(req);
    } else if (cmd == "set_config"){
        setConfigRes(req);
    }
}
void ConfigManager::getConfigRes(JSONCmdData* req){
    configData* resData = new configData();
    resData->cmd = req->command;

    String fileName;
    config_json parser;
    if (parser.parse(req->arguments.c_str())) {
        fileName = parser.getString("config", req->arguments);
    } else {
        fileName = req->arguments;
    }
    resData->fileName = fileName;
    if (fileName == "systemConfig"){
        resData->content = getConfigJson(CONFIG_PATH);
    } else if (fileName == "sensors") {
        resData->content = getConfigJson(MODEL_PATH);
    } else if (fileName == "systemInfo") {
        resData->content = getConfigJson(SYSTEM_PATH);
    } else if (fileName == "tempControlConfig") {
        resData->content = getConfigJson(TEMP_CONTROL_PATH);
    } else if (fileName == "hj212Config") {
        resData->content = getConfigJson(HJ212_PATH);
    } else if (fileName == "switchConfig") {
        resData->content = getConfigJson(SWITCH_PATH);
    }
    LOG_DEBUG("command : %s", req->command);
    LOG_DEBUG("arguments : %s", req->arguments.c_str());
    int subCount = EventBus::getInstance().getSubscriberCount(EventID::CONFIG_QUERY_RES);
    for (int i = 0; i < subCount; i++) resData->retain(); 
    EventBus::getInstance().publish(EventID::CONFIG_QUERY_RES, resData);
    resData->release();
}
void ConfigManager::setConfigRes(JSONCmdData* req){
    configData* resData = new configData();
    resData->cmd = req->command;

    config_json parser(req->arguments.c_str());
    if (!parser.isValid()) {
        LOG_ERROR("Invalid JSON arguments: %s", req->arguments.c_str());
        delete resData;
        return;
    }

    String fileName = parser.getString("config", "systemConfig");
    resData->fileName = fileName;

    cJSON* contentObj = parser.getObject("content");
    if (contentObj) {
        config_json contentParser;
        char* rawStr = cJSON_PrintUnformatted(contentObj);
        if (rawStr) {
            String newConetent = String(rawStr);
            LOG_DEBUG("New %s content: %s", fileName.c_str(), rawStr);

            String path = (fileName == "systemConfig") ? CONFIG_PATH : 
                          (fileName == "sensors") ? MODEL_PATH : 
                          (fileName == "systemInfo") ? SYSTEM_PATH : 
                          (fileName == "tempControlConfig") ? TEMP_CONTROL_PATH : 
                          (fileName == "hj212Config") ? HJ212_PATH : 
                          (fileName == "switchConfig") ? SWITCH_PATH : "";
            if (path != "") {
                saveConfig(path.c_str(), newConetent);
                resData->content = getConfigJson(path.c_str());
            }
            cJSON_free(rawStr);
        }
    }
    
    int subCount = EventBus::getInstance().getSubscriberCount(EventID::CONFIG_SET_RES);
    for (int i = 0; i < subCount; i++) resData->retain(); 
    EventBus::getInstance().publish(EventID::CONFIG_SET_RES, resData);
    resData->release();
}

void ConfigManager::updateTimeAuto(void) {
    Ds1302::DateTime now;
    rtc.getDateTime(&now);

    static uint8_t last_second = 0;
    if (last_second != now.second)
    {
        last_second = now.second;

        Serial.print("20");
        Serial.print(now.year);    // 00-99
        Serial.print('-');
        if (now.month < 10) Serial.print('0');
        Serial.print(now.month);   // 01-12
        Serial.print('-');
        if (now.day < 10) Serial.print('0');
        Serial.print(now.day);     // 01-31
        Serial.print(' ');
        Serial.print(WeekDays[now.dow - 1]); // 1-7
        Serial.print(' ');
        if (now.hour < 10) Serial.print('0');
        Serial.print(now.hour);    // 00-23
        Serial.print(':');
        if (now.minute < 10) Serial.print('0');
        Serial.print(now.minute);  // 00-59
        Serial.print(':');
        if (now.second < 10) Serial.print('0');
        Serial.print(now.second);  // 00-59
        Serial.println();
    }
}