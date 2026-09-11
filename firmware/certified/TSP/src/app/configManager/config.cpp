#include "config.h"
#include "../../system/ota/remote_ota_manager.h"
#include "config_json.h"
#include "system_json.h"
#include "model_json.h"
#include "../../module/log/log_manager.h"
#include "../../module/gas/GasUnitConverter.h"
#include "../../inc/sys_init.h"
#include "../../system/event/eventBus.h"
#include <Ds1302.h>
#include <esp_heap_caps.h>
#include <math.h>
#include <utility>

namespace {

struct SensorDefinition {
    const char *id;
    const char *name;
    const char *unit;
    int alarmLimit;
};

static const SensorDefinition SENSOR_DEFINITIONS[] = {
    {"a34001", "TSP",           "ug/m3",   500},
    {"a34005", "PM1",           "ug/m3",   500},
    {"a34004", "PM2.5",         "ug/m3",   500},
    {"a34002", "PM10",          "ug/m3",   500},
    {"a01007", "WINDSPEED",     "m/s",     999},
    {"a01008", "WINDDIRECTION", "degree",  999},
    {"a01006", "PRESSURE",      "kPa",     999},
    {"a01001", "TEMP",          "celsius",  40},
    {"a01002", "HUMI",          "%",        90},
    {"L90",    "NOISE",         "dB",       60},
    {"w34011", "O3",            "ug/m3",    20},
    {"a21004", "NO2",           "ug/m3",    20},
    {"a21005", "CO",            "mg/m3",    20},
    {"a21026", "SO2",           "ug/m3",    20},
    {"a24035", "TVOC",          "ug/m3",   500}
};

static constexpr size_t SENSOR_DEFINITION_COUNT =
    sizeof(SENSOR_DEFINITIONS) / sizeof(SENSOR_DEFINITIONS[0]);

static bool applyRuntimeVersionToSystemInfo(String &content)
{
    cJSON *root = cJSON_Parse(content.c_str());
    if (root == nullptr || !cJSON_IsObject(root)) {
        if (root != nullptr) cJSON_Delete(root);
        LOG_ERROR("[DIAG] SYSTEM_INFO_VERSION result=failed reason=invalid_json");
        return false;
    }

    cJSON *runtimeVersion = cJSON_CreateString(VERSION2);
    if (runtimeVersion == nullptr) {
        cJSON_Delete(root);
        LOG_ERROR("[DIAG] SYSTEM_INFO_VERSION result=failed reason=no_memory");
        return false;
    }

    if (cJSON_GetObjectItemCaseSensitive(root, "version") != nullptr) {
        cJSON_ReplaceItemInObject(root, "version", runtimeVersion);
    } else if (!cJSON_AddItemToObject(root, "version", runtimeVersion)) {
        cJSON_Delete(runtimeVersion);
        cJSON_Delete(root);
        LOG_ERROR("[DIAG] SYSTEM_INFO_VERSION result=failed reason=add_failed");
        return false;
    }

    char *serialized = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (serialized == nullptr) {
        LOG_ERROR("[DIAG] SYSTEM_INFO_VERSION result=failed reason=serialize_failed");
        return false;
    }

    content = serialized;
    cJSON_free(serialized);
    LOG_INFO("[DIAG] SYSTEM_INFO_VERSION result=ok version=%s source=runtime",
             VERSION2);
    return true;
}

struct SensorPatch {
    String id;
    bool hasUnit = false;
    String unit;
    bool hasAlarmLimit = false;
    int alarmLimit = 0;
};

static int sensorDefinitionIndex(const char *id)
{
    if (id == nullptr) return -1;
    for (size_t i = 0; i < SENSOR_DEFINITION_COUNT; ++i) {
        if (strcmp(SENSOR_DEFINITIONS[i].id, id) == 0) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

static bool isPlainAsciiUnit(const String &unit)
{
    if (unit.length() == 0 || unit.length() > 16) return false;
    for (size_t i = 0; i < unit.length(); ++i) {
        const unsigned char value = static_cast<unsigned char>(unit.charAt(i));
        if (value < 0x20 || value > 0x7E || value == '"' || value == '\\') {
            return false;
        }
    }
    return true;
}

static void appendEscapedJsonString(String &json, const String &value)
{
    json += '"';
    for (size_t i = 0; i < value.length(); ++i) {
        const char ch = value.charAt(i);
        switch (ch) {
            case '"': json += "\\\""; break;
            case '\\': json += "\\\\"; break;
            case '\b': json += "\\b"; break;
            case '\f': json += "\\f"; break;
            case '\n': json += "\\n"; break;
            case '\r': json += "\\r"; break;
            case '\t': json += "\\t"; break;
            default:
                if (static_cast<unsigned char>(ch) >= 0x20) json += ch;
                break;
        }
    }
    json += '"';
}

static size_t escapedJsonLength(const String &value)
{
    size_t length = 0;
    for (size_t i = 0; i < value.length(); ++i) {
        const unsigned char ch = static_cast<unsigned char>(value.charAt(i));
        if (ch == '"' || ch == '\\' || ch == '\b' || ch == '\f' ||
            ch == '\n' || ch == '\r' || ch == '\t') {
            length += 2;
        } else if (ch >= 0x20) {
            ++length;
        }
    }
    return length;
}

static const SensorPatch *findPatch(const SensorPatch *patches,
                                    size_t patchCount, const String &id)
{
    for (size_t i = 0; i < patchCount; ++i) {
        if (patches[i].id == id) return &patches[i];
    }
    return nullptr;
}

static void appendSensorConfigJson(String &json, const String &id,
                                   const COLLECTCONFIG &config,
                                   const SensorPatch *patch)
{
    appendEscapedJsonString(json, id);
    json += ":{";
    json += "\"name\":";
    appendEscapedJsonString(json, config.name);
    json += ",\"alarmLimit\":";
    json += String(patch != nullptr && patch->hasAlarmLimit
        ? patch->alarmLimit : config.alarmLimit);
    json += ",\"unit\":";
    appendEscapedJsonString(json,
        patch != nullptr && patch->hasUnit ? patch->unit : config.unit);
    json += '}';
}

static bool serializeConfigMap(const COLLECTMAP &configs,
                               const SensorPatch *patches,
                               size_t patchCount, String &json)
{
    size_t capacity = 4;
    for (const auto &entry : configs) {
        capacity += 64 + escapedJsonLength(entry.first) +
                    escapedJsonLength(entry.second.name) +
                    escapedJsonLength(entry.second.unit);
    }
    json = "";
    if (!json.reserve(capacity)) return false;
    json += '{';
    bool first = true;
    for (const auto &entry : configs) {
        if (!first) json += ',';
        first = false;
        appendSensorConfigJson(json, entry.first, entry.second,
                               findPatch(patches, patchCount, entry.first));
    }
    json += '}';
    return json.length() > 2 || configs.empty();
}

static bool serializeSelectedSensors(const COLLECTMAP &catalog,
                                     const bool *selected, String &json)
{
    size_t capacity = 4;
    for (size_t i = 0; i < SENSOR_DEFINITION_COUNT; ++i) {
        if (!selected[i]) continue;
        const auto it = catalog.find(SENSOR_DEFINITIONS[i].id);
        if (it == catalog.end()) return false;
        capacity += 64 + escapedJsonLength(it->first) +
                    escapedJsonLength(it->second.name) +
                    escapedJsonLength(it->second.unit);
    }
    json = "";
    if (!json.reserve(capacity)) return false;
    json += '{';
    bool first = true;
    for (size_t i = 0; i < SENSOR_DEFINITION_COUNT; ++i) {
        if (!selected[i]) continue;
        const auto it = catalog.find(SENSOR_DEFINITIONS[i].id);
        if (!first) json += ',';
        first = false;
        appendSensorConfigJson(json, it->first, it->second, nullptr);
    }
    json += '}';
    return true;
}

static const char *profileForSelection(const bool *selected)
{
    const int tvoc = sensorDefinitionIndex("a24035");
    if (tvoc >= 0 && selected[tvoc]) return "tvoc";

    const char *gasIds[] = {"w34011", "a21004", "a21005", "a21026"};
    for (const char *id : gasIds) {
        const int index = sensorDefinitionIndex(id);
        if (index >= 0 && selected[index]) return "air_station";
    }

    const char *dustIds[] = {
        "a01007", "a01008", "a01006", "a01001", "a01002", "L90"
    };
    for (const char *id : dustIds) {
        const int index = sensorDefinitionIndex(id);
        if (index >= 0 && selected[index]) return "dust";
    }
    return "particulate";
}

} // namespace

static void normalizeGasUnitsInJson(cJSON *sensors)
{
    if (!cJSON_IsObject(sensors)) return;

    cJSON *sensorEntry = nullptr;
    cJSON_ArrayForEach(sensorEntry, sensors)
    {
        if (sensorEntry->string == nullptr) continue;
        const String sensorId(sensorEntry->string);
        if (!GasUnitConverter::isGasSensor(sensorId)) continue;

        cJSON *unitItem = cJSON_GetObjectItemCaseSensitive(sensorEntry, "unit");
        String requestedUnit = cJSON_IsString(unitItem) && unitItem->valuestring != nullptr
            ? String(unitItem->valuestring)
            : String();
        String candidate = requestedUnit;
        candidate.trim();
        candidate.toLowerCase();
        const bool valid = GasUnitConverter::isSupportedUnit(candidate);
        const String normalized = valid
            ? candidate
            : String(GasUnitConverter::defaultUnit(sensorId));

        if (!valid)
        {
            LOG_WARNING("Gas unit invalid for %s: '%s', fallback to %s",
                        sensorId.c_str(), requestedUnit.c_str(), normalized.c_str());
        }

        if (cJSON_IsString(unitItem))
        {
            cJSON_SetValuestring(unitItem, normalized.c_str());
        }
        else
        {
            cJSON_DeleteItemFromObjectCaseSensitive(sensorEntry, "unit");
            cJSON_AddStringToObject(sensorEntry, "unit", normalized.c_str());
        }
    }
}

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

    // 1. ��ӡ System ����
    LOG_INFO("[System Config]");
    LOG_INFO("  Collect Time: %d s", globalCfg.system.collect_time);
    LOG_INFO("  Upload Interval: %d s", globalCfg.system.upload_interval);
    LOG_INFO("  DTU Server: %s", globalCfg.system.dtu_server.c_str());

    LOG_INFO("  Toggles -> Raw:%d, Min:%d, HJ212:%d, DTU:%d",
             globalCfg.systemSwitch.save_raw_data, globalCfg.systemSwitch.save_min_data,
             globalCfg.systemSwitch.enable_hj212, globalCfg.systemSwitch.enable_remote_dtu);

    // 2. ��ӡ �¿� ����
    LOG_INFO("[TempControl Config]");
    LOG_INFO("  Switch: %s", globalCfg.systemSwitch.tempConSwitch ? "ON" : "OFF");
    LOG_INFO("  Temp Range: [%d - %d]", globalCfg.tempControl.tempLowerLimit, globalCfg.tempControl.tempUpperLimit);
    LOG_INFO("  Humi Range: [%d - %d]", globalCfg.tempControl.wetnLowerLimit, globalCfg.tempControl.wetnUpperLimit);

    // 3. ��ӡ HJ212 ����
    LOG_INFO("[HJ212 Config]");
    LOG_INFO("  Server: %s", globalCfg.hj212.ip.c_str());
    LOG_INFO("  MN: %s, PW: %s", globalCfg.hj212.mn.c_str(), globalCfg.hj212.pw.c_str());
    LOG_INFO("  Flag: %s, Protocol: %s, ACK Mode: %s, Timeout: %d, Retry: %d",
             globalCfg.hj212.flag.c_str(),
             globalCfg.hj212.protocol_version.c_str(),
             globalCfg.hj212.ack_mode.c_str(),
             globalCfg.hj212.timeout, globalCfg.hj212.retry_times);

    // 4. ��ӡ ������ (Map ����)
    LOG_INFO("[Sensor Collections] Total: %d", globalCfg.collectConfig.size());
    for (auto const& [id, cfg] : globalCfg.collectConfig) {
        LOG_INFO("  - ID: %s | Name: %s | Unit: %s",
                 id.c_str(), cfg.name.c_str(), cfg.unit.c_str());
    }

    // 5. ��ӡ ���� ����
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
    if ((item = cJSON_GetObjectItem(node, "flag")) && cJSON_IsString(item))
        target.flag = item->valuestring;

    // New LCD configuration uses protocol_version; keep pv for old FFat files.
    item = cJSON_GetObjectItem(node, "protocol_version");
    if (item && cJSON_IsString(item)) {
        target.protocol_version = item->valuestring;
    } else if ((item = cJSON_GetObjectItem(node, "pv")) && cJSON_IsString(item)) {
        target.protocol_version = item->valuestring;
    }

    if ((item = cJSON_GetObjectItem(node, "ack_mode")) && cJSON_IsString(item))
        target.ack_mode = item->valuestring;

    if ((item = cJSON_GetObjectItem(node, "timeout")) && cJSON_IsNumber(item))
        target.timeout = item->valueint;
    if ((item = cJSON_GetObjectItem(node, "retry_times")) && cJSON_IsNumber(item))
        target.retry_times = item->valueint;
}
void ConfigManager::_parseSensors(cJSON *objectNode, COLLECTMAP &target_map)
{
    if (!cJSON_IsObject(objectNode)) {
        LOG_ERROR("Sensors node is NOT an object!");
        return;
    }
    char *rawJson = cJSON_Print(objectNode); // ��ʽ����ӡ����������
    if (rawJson) {
        LOG_INFO("--- [Debug] Sensors JSON Node Content ---");
        LOG_INFO("\n%s", rawJson);
        LOG_INFO("-----------------------------------------");
        cJSON_free(rawJson); // �����ֶ��ͷ� cJSON_Print ������ڴ�
    } else {
        LOG_ERROR("Failed to print sensors JSON node.");
    }
    normalizeGasUnitsInJson(objectNode);

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

        if (defaultTemplate && fs.writeFFAT(path, defaultTemplate) == 0) {
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

    return fs.writeFFAT(path, content.c_str()) == 0;
}
bool ConfigManager::saveConfig(const char *path, const char *content)
{
    if (path == nullptr || strlen(path) == 0 || content == nullptr) return false;
    return fs.writeFFAT(path, content) == 0;
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
    else if (strcmp(path, ALARM_PATH) == 0)
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
        RemoteOtaManager::BusinessActivityGuard businessActivity;
        if (msg.id == EventID::CONFIG_QUERY_REQ) {
            JSONCmdData* req = (JSONCmdData*)msg.data;
            LOG_DEBUG("ConfigManager received CONFIG_QUERY_REQ: %s", req->arguments.c_str());
            processQuery(req);
            req->release();
        } else if (msg.id == EventID::CONFIG_SET_REQ) {
            JSONCmdData* req = (JSONCmdData*)msg.data;
            LOG_DEBUG("ConfigManager received CONFIG_SET_REQ len=%u: %s",
                      (unsigned)req->arguments.length(), req->arguments.c_str());
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

void ConfigManager::setSensorProtocolFailure(configData *resData,
                                             const char *reason)
{
    if (resData == nullptr) return;
    resData->success = false;
    resData->fileName = "sensors";
    resData->rawResponse =
        "{\"operation\":\"set_config\",\"code\":\"NG\","
        "\"config\":\"sensors\",\"reason\":\"";
    resData->rawResponse += reason != nullptr ? reason : "config_save_failed";
    resData->rawResponse += "\"}";
}

void ConfigManager::getSensorSelectionRes(configData *resData)
{
    bool selected[SENSOR_DEFINITION_COUNT] = {};
    size_t enabledCount = 0;
    for (size_t i = 0; i < SENSOR_DEFINITION_COUNT; ++i) {
        if (globalCfg.collectConfig.count(SENSOR_DEFINITIONS[i].id) > 0) {
            selected[i] = true;
            ++enabledCount;
        }
    }

    String response;
    if (!response.reserve(384)) {
        resData->success = false;
        resData->rawResponse =
            "{\"operation\":\"get_config\",\"code\":\"NG\","
            "\"config\":\"sensors\",\"reason\":\"config_save_failed\"}";
        return;
    }
    response = "{\"operation\":\"get_config\",\"code\":\"OK\","
               "\"config\":\"sensors\",\"profile\":\"";
    response += profileForSelection(selected);
    response += "\",\"enabled\":[";
    bool first = true;
    for (size_t i = 0; i < SENSOR_DEFINITION_COUNT; ++i) {
        if (!selected[i]) continue;
        if (!first) response += ',';
        first = false;
        appendEscapedJsonString(response, SENSOR_DEFINITIONS[i].id);
    }
    response += "]}";
    resData->rawResponse = std::move(response);
    LOG_INFO("[DIAG] SENSOR_CONFIG view=selection enabled=%u profile=%s bytes=%u free=%u largest=%u",
             (unsigned)enabledCount, profileForSelection(selected),
             (unsigned)resData->rawResponse.length(),
             ESP.getFreeHeap(), ESP.getMaxAllocHeap());
}

void ConfigManager::getSensorSubsetRes(cJSON *idsArray, configData *resData)
{
    const int count = cJSON_IsArray(idsArray) ? cJSON_GetArraySize(idsArray) : -1;
    if (count <= 0) {
        resData->success = false;
        resData->rawResponse =
            "{\"operation\":\"get_config\",\"code\":\"NG\","
            "\"config\":\"sensors\",\"reason\":\"invalid_sensor_id\"}";
        return;
    }
    if (count > 4) {
        resData->success = false;
        resData->rawResponse =
            "{\"operation\":\"get_config\",\"code\":\"NG\","
            "\"config\":\"sensors\",\"reason\":\"too_many_ids\"}";
        return;
    }

    const char *requestedIds[4] = {};
    for (int i = 0; i < count; ++i) {
        cJSON *item = cJSON_GetArrayItem(idsArray, i);
        if (!cJSON_IsString(item) || item->valuestring == nullptr ||
            sensorDefinitionIndex(item->valuestring) < 0) {
            resData->success = false;
            resData->rawResponse =
                "{\"operation\":\"get_config\",\"code\":\"NG\","
                "\"config\":\"sensors\",\"reason\":\"invalid_sensor_id\"}";
            return;
        }
        for (int previous = 0; previous < i; ++previous) {
            if (strcmp(requestedIds[previous], item->valuestring) == 0) {
                resData->success = false;
                resData->rawResponse =
                    "{\"operation\":\"get_config\",\"code\":\"NG\","
                    "\"config\":\"sensors\",\"reason\":\"duplicate_sensor_id\"}";
                return;
            }
        }
        requestedIds[i] = item->valuestring;
    }

    String response;
    if (!response.reserve(768)) {
        resData->success = false;
        resData->rawResponse =
            "{\"operation\":\"get_config\",\"code\":\"NG\","
            "\"config\":\"sensors\",\"reason\":\"config_save_failed\"}";
        return;
    }
    response = "{\"operation\":\"get_config\",\"code\":\"OK\","
               "\"config\":\"sensors\",\"content\":{";
    bool first = true;
    size_t returnedCount = 0;
    for (int i = 0; i < count; ++i) {
        const auto it = globalCfg.collectConfig.find(requestedIds[i]);
        if (it == globalCfg.collectConfig.end()) continue;
        if (!first) response += ',';
        first = false;
        ++returnedCount;
        appendSensorConfigJson(response, it->first, it->second, nullptr);
    }
    response += "}}";
    resData->rawResponse = std::move(response);
    LOG_INFO("[DIAG] SENSOR_CONFIG view=subset requested=%u returned=%u bytes=%u free=%u largest=%u",
             (unsigned)count, (unsigned)returnedCount,
             (unsigned)resData->rawResponse.length(),
             ESP.getFreeHeap(), ESP.getMaxAllocHeap());
}

bool ConfigManager::loadSensorCatalog(COLLECTMAP &catalog)
{
    catalog.clear();
    for (size_t i = 0; i < SENSOR_DEFINITION_COUNT; ++i) {
        COLLECTCONFIG config = {};
        config.id = SENSOR_DEFINITIONS[i].id;
        config.name = SENSOR_DEFINITIONS[i].name;
        config.unit = SENSOR_DEFINITIONS[i].unit;
        config.alarmLimit = SENSOR_DEFINITIONS[i].alarmLimit;
        catalog[config.id] = config;
    }

    String stored;
    if (fs.readFFAT(SENSOR_CATALOG_PATH, stored) == 0 && stored.length() > 0) {
        config_json parser(stored.c_str());
        cJSON *root = parser.getJsonObject();
        if (cJSON_IsObject(root)) {
            for (size_t i = 0; i < SENSOR_DEFINITION_COUNT; ++i) {
                cJSON *item = cJSON_GetObjectItemCaseSensitive(
                    root, SENSOR_DEFINITIONS[i].id);
                if (!cJSON_IsObject(item)) continue;
                COLLECTCONFIG &config = catalog[SENSOR_DEFINITIONS[i].id];
                cJSON *name = cJSON_GetObjectItemCaseSensitive(item, "name");
                cJSON *alarm = cJSON_GetObjectItemCaseSensitive(item, "alarmLimit");
                cJSON *unit = cJSON_GetObjectItemCaseSensitive(item, "unit");
                if (cJSON_IsString(name) && name->valuestring != nullptr) {
                    config.name = name->valuestring;
                }
                if (cJSON_IsNumber(alarm) && isfinite(alarm->valuedouble) &&
                    alarm->valuedouble >= 0 && alarm->valuedouble <= 1000000000.0) {
                    config.alarmLimit = static_cast<int>(alarm->valuedouble);
                }
                if (cJSON_IsString(unit) && unit->valuestring != nullptr) {
                    String candidate(unit->valuestring);
                    candidate.trim();
                    if (GasUnitConverter::isGasSensor(config.id)) {
                        candidate.toLowerCase();
                        if (GasUnitConverter::isSupportedUnit(candidate)) {
                            config.unit = candidate;
                        }
                    } else if (isPlainAsciiUnit(candidate)) {
                        config.unit = candidate;
                    }
                }
            }
        } else {
            LOG_WARNING("Sensor catalog invalid, rebuilding from defaults and active sensors");
        }
    }

    // The active model is authoritative for currently enabled factors.
    for (const auto &entry : globalCfg.collectConfig) {
        if (sensorDefinitionIndex(entry.first.c_str()) >= 0) {
            catalog[entry.first] = entry.second;
        }
    }
    return catalog.size() == SENSOR_DEFINITION_COUNT;
}

void ConfigManager::setSensorSelectionRes(cJSON *enabledArray,
                                          configData *resData)
{
    if (!cJSON_IsArray(enabledArray)) {
        setSensorProtocolFailure(resData, "invalid_sensor_id");
        return;
    }
    const int enabledCount = cJSON_GetArraySize(enabledArray);
    if (enabledCount == 0) {
        setSensorProtocolFailure(resData, "empty_selection");
        return;
    }

    bool selected[SENSOR_DEFINITION_COUNT] = {};
    for (int i = 0; i < enabledCount; ++i) {
        cJSON *item = cJSON_GetArrayItem(enabledArray, i);
        if (!cJSON_IsString(item) || item->valuestring == nullptr) {
            setSensorProtocolFailure(resData, "invalid_sensor_id");
            return;
        }
        const int index = sensorDefinitionIndex(item->valuestring);
        if (index < 0) {
            setSensorProtocolFailure(resData, "invalid_sensor_id");
            return;
        }
        if (selected[index]) {
            setSensorProtocolFailure(resData, "duplicate_sensor_id");
            return;
        }
        selected[index] = true;
    }

    const int tvocIndex = sensorDefinitionIndex("a24035");
    if (tvocIndex >= 0 && selected[tvocIndex] && enabledCount != 1) {
        setSensorProtocolFailure(resData, "tvoc_exclusive");
        return;
    }
    if ((tvocIndex < 0 || !selected[tvocIndex]) && enabledCount > 14) {
        setSensorProtocolFailure(resData, "too_many_ids");
        return;
    }

    COLLECTMAP catalog;
    if (!loadSensorCatalog(catalog)) {
        setSensorProtocolFailure(resData, "state_save_failed");
        return;
    }

    bool allSensors[SENSOR_DEFINITION_COUNT];
    for (size_t i = 0; i < SENSOR_DEFINITION_COUNT; ++i) allSensors[i] = true;
    String catalogJson;
    if (!serializeSelectedSensors(catalog, allSensors, catalogJson) ||
        !saveConfig(SENSOR_CATALOG_PATH, catalogJson)) {
        setSensorProtocolFailure(resData, "state_save_failed");
        return;
    }
    const size_t catalogBytes = catalogJson.length();
    catalogJson = String();

    String modelJson;
    if (!serializeSelectedSensors(catalog, selected, modelJson) ||
        !saveConfig(MODEL_PATH, modelJson)) {
        setSensorProtocolFailure(resData, "state_save_failed");
        return;
    }

    resData->success = true;
    resData->rawResponse =
        "{\"operation\":\"set_config\",\"code\":\"OK\","
        "\"config\":\"sensors\",\"mode\":\"selection\",\"profile\":\"";
    resData->rawResponse += profileForSelection(selected);
    resData->rawResponse += "\",\"enabled_count\":";
    resData->rawResponse += String(enabledCount);
    resData->rawResponse += ",\"restart_required\":true}";
    LOG_INFO("[DIAG] SENSOR_CONFIG mode=selection enabled=%u profile=%s model_bytes=%u catalog_bytes=%u free=%u largest=%u",
             (unsigned)enabledCount, profileForSelection(selected),
             (unsigned)modelJson.length(), (unsigned)catalogBytes,
             ESP.getFreeHeap(), ESP.getMaxAllocHeap());
}

void ConfigManager::setSensorMergeRes(cJSON *contentObj,
                                      configData *resData)
{
    const int itemCount = cJSON_IsObject(contentObj)
        ? cJSON_GetArraySize(contentObj) : -1;
    if (itemCount <= 0) {
        setSensorProtocolFailure(resData, "unsupported_field");
        return;
    }
    if (itemCount > 4) {
        setSensorProtocolFailure(resData, "too_many_patch_items");
        return;
    }

    SensorPatch patches[4];
    size_t patchCount = 0;
    cJSON *sensorItem = nullptr;
    cJSON_ArrayForEach(sensorItem, contentObj) {
        if (sensorItem->string == nullptr ||
            sensorDefinitionIndex(sensorItem->string) < 0) {
            setSensorProtocolFailure(resData, "invalid_sensor_id");
            return;
        }
        if (!cJSON_IsObject(sensorItem)) {
            setSensorProtocolFailure(resData, "unsupported_field");
            return;
        }
        const String sensorId(sensorItem->string);
        if (globalCfg.collectConfig.count(sensorId) == 0) {
            setSensorProtocolFailure(resData, "sensor_not_enabled");
            return;
        }
        for (size_t previous = 0; previous < patchCount; ++previous) {
            if (patches[previous].id == sensorId) {
                setSensorProtocolFailure(resData, "duplicate_sensor_id");
                return;
            }
        }

        SensorPatch &patch = patches[patchCount];
        patch.id = sensorId;
        cJSON *field = nullptr;
        cJSON_ArrayForEach(field, sensorItem) {
            if (field->string == nullptr) {
                setSensorProtocolFailure(resData, "unsupported_field");
                return;
            }
            if (strcmp(field->string, "unit") == 0) {
                if (!cJSON_IsString(field) || field->valuestring == nullptr) {
                    setSensorProtocolFailure(resData, "invalid_unit");
                    return;
                }
                String unit(field->valuestring);
                unit.trim();
                if (GasUnitConverter::isGasSensor(sensorId)) {
                    unit.toLowerCase();
                    if (!GasUnitConverter::isSupportedUnit(unit)) {
                        setSensorProtocolFailure(resData, "invalid_unit");
                        return;
                    }
                } else if (!isPlainAsciiUnit(unit)) {
                    setSensorProtocolFailure(resData, "invalid_unit");
                    return;
                }
                patch.hasUnit = true;
                patch.unit = unit;
            } else if (strcmp(field->string, "alarmLimit") == 0) {
                const double value = field->valuedouble;
                if (!cJSON_IsNumber(field) || !isfinite(value) || value < 0 ||
                    value > 1000000000.0 || floor(value) != value) {
                    setSensorProtocolFailure(resData, "invalid_alarm_limit");
                    return;
                }
                patch.hasAlarmLimit = true;
                patch.alarmLimit = static_cast<int>(value);
            } else {
                setSensorProtocolFailure(resData, "unsupported_field");
                return;
            }
        }
        if (!patch.hasUnit && !patch.hasAlarmLimit) {
            setSensorProtocolFailure(resData, "unsupported_field");
            return;
        }
        ++patchCount;
    }

    String modelJson;
    if (!serializeConfigMap(globalCfg.collectConfig, patches,
                            patchCount, modelJson) ||
        !saveConfig(MODEL_PATH, modelJson)) {
        setSensorProtocolFailure(resData, "config_save_failed");
        return;
    }

    resData->success = true;
    resData->rawResponse =
        "{\"operation\":\"set_config\",\"code\":\"OK\","
        "\"config\":\"sensors\",\"mode\":\"merge\",\"changed\":[";
    for (size_t i = 0; i < patchCount; ++i) {
        if (i > 0) resData->rawResponse += ',';
        appendEscapedJsonString(resData->rawResponse, patches[i].id);
    }
    resData->rawResponse += "],\"restart_required\":true}";
    LOG_INFO("[DIAG] SENSOR_CONFIG mode=merge changed=%u model_bytes=%u free=%u largest=%u",
             (unsigned)patchCount, (unsigned)modelJson.length(),
             ESP.getFreeHeap(), ESP.getMaxAllocHeap());
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
        cJSON *root = parser.getJsonObject();
        cJSON *view = cJSON_IsObject(root)
            ? cJSON_GetObjectItemCaseSensitive(root, "view") : nullptr;
        cJSON *ids = cJSON_IsObject(root)
            ? cJSON_GetObjectItemCaseSensitive(root, "ids") : nullptr;
        if (view != nullptr) {
            if (cJSON_IsString(view) && view->valuestring != nullptr &&
                strcmp(view->valuestring, "selection") == 0) {
                getSensorSelectionRes(resData);
            } else {
                resData->success = false;
                resData->rawResponse =
                    "{\"operation\":\"get_config\",\"code\":\"NG\","
                    "\"config\":\"sensors\",\"reason\":\"invalid_mode\"}";
            }
        } else if (ids != nullptr) {
            getSensorSubsetRes(ids, resData);
        } else {
            // Legacy full query for existing LCD and Remote clients.
            resData->content = getConfigJson(MODEL_PATH);
        }
    } else if (fileName == "systemInfo") {
        resData->content = getConfigJson(SYSTEM_PATH);
        applyRuntimeVersionToSystemInfo(resData->content);
    } else if (fileName == "tempControlConfig") {
        resData->content = getConfigJson(TEMP_CONTROL_PATH);
    } else if (fileName == "hj212Config") {
        resData->content = getConfigJson(HJ212_PATH);
    } else if (fileName == "switchConfig") {
        resData->content = getConfigJson(SWITCH_PATH);
    } else if (fileName == "alarmConfig"){
        resData->content = getConfigJson(ALARM_PATH);
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

    auto publishResponse = [resData]() {
        int subCount = EventBus::getInstance().getSubscriberCount(EventID::CONFIG_SET_RES);
        for (int i = 0; i < subCount; i++) resData->retain();
        EventBus::getInstance().publish(EventID::CONFIG_SET_RES, resData);
        resData->release();
    };

    auto publishFailure = [resData, &publishResponse](const char* message) {
        resData->success = false;
        resData->message = message;
        resData->content = "null";
        publishResponse();
    };

    config_json parser(req->arguments.c_str());
    if (!parser.isValid()) {
        const char* requestStart = req->arguments.c_str();
        const char* errorPtr = cJSON_GetErrorPtr();
        int errorOffset = -1;
        const uintptr_t requestAddress = reinterpret_cast<uintptr_t>(requestStart);
        const uintptr_t errorAddress = reinterpret_cast<uintptr_t>(errorPtr);
        if (errorPtr != nullptr && errorAddress >= requestAddress &&
            errorAddress <= requestAddress + req->arguments.length()) {
            errorOffset = static_cast<int>(errorAddress - requestAddress);
        }
        LOG_ERROR("[DIAG] Invalid set_config JSON len=%u errorOffset=%d freeHeap=%u largestBlock=%u",
                  (unsigned)req->arguments.length(), errorOffset,
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
        publishFailure("Invalid JSON arguments");
        return;
    }

    String fileName = parser.getString("config", "");
    resData->fileName = fileName;

    if (fileName == "sensors") {
        cJSON *root = parser.getJsonObject();
        cJSON *mode = cJSON_IsObject(root)
            ? cJSON_GetObjectItemCaseSensitive(root, "mode") : nullptr;
        if (mode != nullptr) {
            if (!cJSON_IsString(mode) || mode->valuestring == nullptr) {
                setSensorProtocolFailure(resData, "invalid_mode");
                publishResponse();
                return;
            }
            if (strcmp(mode->valuestring, "selection") == 0) {
                setSensorSelectionRes(
                    cJSON_GetObjectItemCaseSensitive(root, "enabled"), resData);
                publishResponse();
                return;
            }
            if (strcmp(mode->valuestring, "merge") == 0) {
                setSensorMergeRes(
                    cJSON_GetObjectItemCaseSensitive(root, "content"), resData);
                publishResponse();
                return;
            }
            setSensorProtocolFailure(resData, "invalid_mode");
            publishResponse();
            return;
        }
    }

    String path = (fileName == "systemConfig") ? CONFIG_PATH :
                  (fileName == "sensors") ? MODEL_PATH :
                  (fileName == "systemInfo") ? SYSTEM_PATH :
                  (fileName == "tempControlConfig") ? TEMP_CONTROL_PATH :
                  (fileName == "hj212Config") ? HJ212_PATH :
                  (fileName == "switchConfig") ? SWITCH_PATH :
                  (fileName == "alarmConfig") ? ALARM_PATH : "";
    if (path.length() == 0) {
        LOG_ERROR("Unsupported set_config name: %s", fileName.c_str());
        publishFailure("Unsupported config name");
        return;
    }

    cJSON* contentObj = parser.getObject("content");
    if (!contentObj) {
        LOG_ERROR("set_config content is missing or not an object");
        publishFailure("Missing content object");
        return;
    }

    if (fileName == "sensors") {
        normalizeGasUnitsInJson(contentObj);
    }

    // The legacy full set_config path is kept for existing Remote clients,
    // but serialization uses task-stack storage instead of another large
    // heap allocation while the cJSON tree is alive.
    char serializedContent[2048] = {};
    if (!cJSON_PrintPreallocated(contentObj, serializedContent,
                                 sizeof(serializedContent), false)) {
        LOG_ERROR("Failed to serialize set_config content: %s", fileName.c_str());
        publishFailure("Config serialization failed");
        return;
    }

    LOG_DEBUG("New %s content len=%u: %s", fileName.c_str(),
              (unsigned)strlen(serializedContent), serializedContent);

    // Release the request tree before any catalog/model write or full
    // compatibility response. serializedContent remains on this task's stack.
    parser.clear();

    if (fileName == "sensors") {
        // A legacy full update may be the first request after upgrading. Seed
        // the catalog from the current active model before it is replaced, then
        // overlay the new full content. Factors omitted by the legacy request
        // therefore keep their previous details for a later re-enable.
        COLLECTMAP catalog;
        String catalogJson;
        bool allSensors[SENSOR_DEFINITION_COUNT];
        for (size_t i = 0; i < SENSOR_DEFINITION_COUNT; ++i) {
            allSensors[i] = true;
        }

        bool catalogReady = loadSensorCatalog(catalog);
        {
            config_json savedContent(serializedContent);
            cJSON *savedSensors = savedContent.getJsonObject();
            cJSON *sensorEntry = nullptr;
            if (!cJSON_IsObject(savedSensors)) {
                catalogReady = false;
            } else {
                cJSON_ArrayForEach(sensorEntry, savedSensors) {
                    if (sensorEntry->string == nullptr ||
                        sensorDefinitionIndex(sensorEntry->string) < 0 ||
                        !cJSON_IsObject(sensorEntry)) {
                        continue;
                    }
                    COLLECTCONFIG &config = catalog[sensorEntry->string];
                    config.id = sensorEntry->string;
                    cJSON *name = cJSON_GetObjectItemCaseSensitive(sensorEntry, "name");
                    cJSON *alarm = cJSON_GetObjectItemCaseSensitive(sensorEntry, "alarmLimit");
                    cJSON *unit = cJSON_GetObjectItemCaseSensitive(sensorEntry, "unit");
                    if (cJSON_IsString(name) && name->valuestring != nullptr) {
                        config.name = name->valuestring;
                    }
                    if (cJSON_IsNumber(alarm)) {
                        config.alarmLimit = alarm->valueint;
                    }
                    if (cJSON_IsString(unit) && unit->valuestring != nullptr) {
                        config.unit = unit->valuestring;
                    }
                }
            }
        }

        if (!catalogReady ||
            !serializeSelectedSensors(catalog, allSensors, catalogJson) ||
            !saveConfig(SENSOR_CATALOG_PATH, catalogJson)) {
            LOG_ERROR("Failed to preserve full sensor catalog");
            publishFailure("Config state save failed");
            return;
        }
    }

    if (!saveConfig(path.c_str(), serializedContent)) {
        LOG_ERROR("Failed to save config: %s", path.c_str());
        publishFailure("Config save failed");
        return;
    }

    // The request and catalog temporaries are gone before allocating the full
    // compatibility response. This is the critical 14-factor peak reduction.
    resData->content = getConfigJson(path.c_str());
    if (resData->content.length() == 0) {
        LOG_ERROR("Failed to reload saved config: %s", path.c_str());
        publishFailure("Config reload failed");
        return;
    }

    resData->success = true;
    publishResponse();
}

void ConfigManager::updateTimeAuto(void) {
    Ds1302::DateTime now;
    rtc.getDateTime(&now);

    static uint8_t last_second = 0;
    if (last_second != now.second)
    {
        last_second = now.second;

        LOG_DEBUG("RTC time: 20%02u-%02u-%02u %s %02u:%02u:%02u",
                  (unsigned)now.year,
                  (unsigned)now.month,
                  (unsigned)now.day,
                  WeekDays[now.dow - 1],
                  (unsigned)now.hour,
                  (unsigned)now.minute,
                  (unsigned)now.second);
    }
}
