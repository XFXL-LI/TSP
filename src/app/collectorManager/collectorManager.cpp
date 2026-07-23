#include "collectorManager.h"
#include "../../system/event/eventBus.h"
#include "../../module/log/log_manager.h"
#include "../../module/Serial/SerialManager.h"

collectorManager *collectorManager::_instance = nullptr;

collectorManager::collectorManager()
{
}

collectorManager &collectorManager::getInstance()
{
    if (_instance == nullptr)
    {
        _instance = new collectorManager();
    }
    return *_instance;
}
void collectorManager::begin(COLLECTMAP &collectMap)
{
    auto &sm = SerialManager::getInstance();
    auto &registry = BaseCollector::getRegistry();

    _queryQueue = EventBus::getInstance().createReceiverQueue(10);
    EventBus::getInstance().subscribe(EventID::GAL_REQ, _queryQueue);

    LOG_INFO("Start load collect map!");
    for (auto &[name, cfg] : collectMap)
    {
        auto it = registry.find(cfg.id);
        if (it != registry.end())
        {
            // Use fixed mapping instead of config
            String serialPortName = idToSerial.count(cfg.id) ? idToSerial[cfg.id] : "485"; // Default to 485 if not found
            Stream *sensorPort = sm.getStream(serialPortName.c_str());
            if (sensorPort)
            {
                BaseCollector *collector = it->second;
                collector->modbusInit(sensorPort, cfg.id, serialPortName, 1.0, cfg.unit);
                registerCollector(collector);
            }
            else
            {
                LOG_WARNING("Serial port %s not available for sensor %s", serialPortName.c_str(), cfg.id.c_str());
            }
        }
        else
        {
            LOG_WARNING("ID %s has no registered collector!", cfg.id.c_str());
        }
    }
    LOG_INFO("Load collect map sucess!");
}

void collectorManager::registerCollector(BaseCollector *collector)
{
    if (collector == nullptr)
        return;

    for (auto c : _collectors)
    {
        if (c == collector)
            return;
    }

    _collectors.push_back(collector);
}

void collectorManager::galpoll()
{
    EventMsg msg;
    if (EventBus::waitEvent(_queryQueue, msg))
    {
        if (msg.id == EventID::GAL_REQ)
        {
            JSONCmdData *req = (JSONCmdData *)msg.data;
            processQuery(req);
            req->release();
        }
        else
        {
            LOG_ERROR("DataManager not subseribe this, send message error!");
        }
    }
}
void collectorManager::processQuery(JSONCmdData *req)
{
    config_json parser(req->arguments.c_str());
    if (!parser.isValid())
    {
        LOG_ERROR("Invalid JSON arguments: %s", req->arguments.c_str());
        return;
    }
    String cmd = req->command;
    LOG_DEBUG("collectorManager received command: %s, arguments: %s", cmd.c_str(), req->arguments.c_str());

    galDataRes *resData = new galDataRes();
    resData->cmd = cmd;

    cJSON *galArray = parser.getArray("ids");
    if (cmd == "gal_data")
    {
        if (galArray)
        {
            int arraySize = cJSON_GetArraySize(galArray);
            LOG_DEBUG("Received GAL_REQ with %d sensor IDs", arraySize);
            for (int i = 0; i < arraySize; i++)
            {
                cJSON *idItem = cJSON_GetArrayItem(galArray, i);
                if (!cJSON_IsObject(idItem))
                {
                    LOG_WARNING("GAL_REQ item at index %d is not an object", i);
                    continue;
                }

                cJSON *idField = cJSON_GetObjectItem(idItem, "id");
                String sensorId = cJSON_IsString(idField) ? String(idField->valuestring) : String();
                if (sensorId.length() == 0)
                {
                    LOG_WARNING("GAL_REQ item at index %d missing or invalid id", i);
                    continue;
                }

                cJSON *paramsArray = cJSON_GetObjectItem(idItem, "params");
                cJSON *valuesArray = cJSON_GetObjectItem(idItem, "values");
                if (cJSON_IsArray(paramsArray) && cJSON_IsArray(valuesArray))
                {
                    LOG_DEBUG("Processing GAL data for sensor ID: %s", sensorId.c_str());
                    int paramsSize = cJSON_GetArraySize(paramsArray);
                    int valuesSize = cJSON_GetArraySize(valuesArray);
                    LOG_DEBUG("Params count: %d, Values count: %d", paramsSize, valuesSize);
                    int intercept = 0, ratio = 100;
                    for (int j = 0; j < paramsSize && j < valuesSize; j++)
                    {
                        cJSON *paramItem = cJSON_GetArrayItem(paramsArray, j);
                        cJSON *valueItem = cJSON_GetArrayItem(valuesArray, j);
                        if (cJSON_IsString(paramItem) && cJSON_IsNumber(valueItem))
                        {
                            String paramStr = paramItem->valuestring;
                            if (paramStr == "DATA") {
                                intercept = valueItem->valueint;
                            }
                            if (paramStr == "RATIO") {
                                ratio = valueItem->valueint;
                            }
                        }
                        else
                        {
                            LOG_WARNING("Invalid param or value in GAL_REQ for sensor ID: %s", sensorId.c_str());
                        }
                    }
                    if (_collectors.empty())
                    {
                        LOG_WARNING("No collectors registered.");
                        return;
                    }
                    bool found = false;
                    for (auto *collector : _collectors)
                    {
                        if (collector->getID() == sensorId)
                        {
                            bool galRes = collector->gal(intercept, ratio);
                            resData->results.push_back({sensorId, galRes});
                            LOG_DEBUG("GAL result for sensor ID %s: %s", sensorId.c_str(), galRes ? "true" : "false");
                            found = true;
                            break;
                        }
                    }
                    if (!found)
                    {
                        LOG_WARNING("No collector matched sensor ID %s for GAL_REQ", sensorId.c_str());
                    }
                }
                else
                {
                    LOG_WARNING("Invalid item in GAL_REQ array at index %d", i);
                }
            }
        }
        else
        {
            LOG_WARNING("GAL_REQ received but 'ids' array is missing or invalid");
        }
        LOG_DEBUG("Received GAL_REQ, processing GAL data collection");

    }
    else
    {
        LOG_WARNING("collectorManager received unknown command: %s", cmd.c_str());
    }
    
    int subCount = EventBus::getInstance().getSubscriberCount(EventID::GAL_RES);
    for (int i = 0; i < subCount; i++)
        resData->retain();
    EventBus::getInstance().publish(EventID::GAL_RES, resData);
    resData->release();
}

void collectorManager::poll()
{
    static std::atomic<uint32_t> nextTraceId(1);
    if (_collectors.empty())
    {
        LOG_WARNING("No collectors registered.");
        return;
    }
    time_t now;
    struct tm timeinfo = {};
    time(&now);
    if (localtime_r(&now, &timeinfo) == nullptr ||
        timeinfo.tm_year + 1900 < 2020 ||
        timeinfo.tm_year + 1900 > 2099)
    {
        LOG_ERROR("System clock is invalid; skipping this collection cycle");
        return;
    }
    uint64_t time = ((uint64_t)(timeinfo.tm_year + 1900) * 10000000000) +
                    ((uint64_t)(timeinfo.tm_mon + 1) * 100000000) +
                    ((uint64_t)timeinfo.tm_mday * 1000000) +
                    ((uint64_t)timeinfo.tm_hour * 10000) +
                    ((uint64_t)timeinfo.tm_min * 100) +
                    (uint64_t)timeinfo.tm_sec;
    LOG_DEBUG("Current Time: %llu", time);
    int subCount = EventBus::getInstance().getSubscriberCount(EventID::RAW_DATA_COLLECTED);
    LOG_DEBUG("Starting batch polling cycle for %d collectors", _collectors.size());
    AllDataPacket *allData = new AllDataPacket();
    allData->trace_id = nextTraceId.fetch_add(1, std::memory_order_relaxed);
    for (auto *collector : _collectors)
    {
        DataPacket *data = collector->collect();
        if (data != nullptr)
        {
            if (data->is_valid)
            {
                // LOG_DEBUG("PRINT data, value: %.2f", data->value);
                allData->data_map[String(data->sensor_id)] = data;
            }
            else
            {
                LOG_WARNING("Invalid data collected - Sensor ID: %s, Raw Value: %.2f, Collector ID: %s",
                            data->sensor_id, data->value, collector->getID().c_str());
                allData->data_map[String(collector->getID().c_str())] = nullptr;
                auto alarmData = new SystemRuntimeStatus();
                alarmData->systemErrorInfo = CHECK_RESULT::COLLECT_ERROR;
                alarmData->errorInfo = "Data collection error for sensor ID: " + String(collector->getID().c_str());
                EventBus::getInstance().publish(EventID::ALARM_TRIGGERED, (void *)alarmData);
                data->release();
            }
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    allData->last_update = time;
    for (int i = 0; i < subCount; i++)
    {
        allData->retain();
    }
    if (!allData->data_map.empty())
    {
        LOG_INFO("[DIAG] COLLECT trace=%u timestamp=%llu sensors=%u subscribers=%d",
                 (unsigned)allData->trace_id,
                 allData->last_update,
                 (unsigned)allData->data_map.size(),
                 subCount);
        EventBus::getInstance().publish(EventID::RAW_DATA_COLLECTED, (void *)allData);
    }
    else
    {
        EventBus::getInstance().publish(EventID::ALARM_TRIGGERED, (void *)allData);
    }
    allData->release();
}
