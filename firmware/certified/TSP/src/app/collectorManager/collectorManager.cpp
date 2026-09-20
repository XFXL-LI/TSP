#include "collectorManager.h"
#include "../../system/ota/remote_ota_manager.h"
#include "../../system/event/eventBus.h"
#include "../../module/log/log_manager.h"
#include "../../module/Serial/SerialManager.h"
#include "../../module/gas/GasCalibrationManager.h"
#include "../../module/gas/GasUnitConverter.h"
#include "../../module/diagnostics/RuntimeMemoryDiagnostics.h"
#include "collect/SensorReadPolicy.h"

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
    if (EventBus::waitEvent(_queryQueue, msg, 20))
    {
        RemoteOtaManager::BusinessActivityGuard businessActivity;
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
    GasCalibrationManager::getInstance().tick();
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
                    if (GasUnitConverter::isGasSensor(sensorId))
                    {
                        resData->results.push_back({sensorId, false});
                        LOG_WARNING("Legacy gas calibration disabled for sensor ID %s",
                                    sensorId.c_str());
                        continue;
                    }
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

namespace {

constexpr uint32_t AIR_READ_OFFSET_MS = 15000;
constexpr uint32_t BATCH_FINISH_OFFSET_MS = 25000;

bool isParticleSensor(const String &id)
{
    return id == "a34005" || id == "a34004" ||
           id == "a34002" || id == "a34001";
}

bool deadlineReached(uint32_t deadlineMs)
{
    return static_cast<int32_t>(millis() - deadlineMs) >= 0;
}

void waitUntil(uint32_t targetMs)
{
    while (!deadlineReached(targetMs))
    {
        const uint32_t remaining = targetMs - millis();
        TickType_t ticks = pdMS_TO_TICKS(remaining);
        if (ticks == 0) ticks = 1;
        vTaskDelay(ticks);
    }
}

uint64_t currentCollectionTimestamp()
{
    time_t now;
    struct tm timeinfo = {};
    time(&now);
    if (localtime_r(&now, &timeinfo) == nullptr ||
        timeinfo.tm_year + 1900 < 2020 ||
        timeinfo.tm_year + 1900 > 2099)
    {
        return 0;
    }
    return ((uint64_t)(timeinfo.tm_year + 1900) * 10000000000ULL) +
           ((uint64_t)(timeinfo.tm_mon + 1) * 100000000ULL) +
           ((uint64_t)timeinfo.tm_mday * 1000000ULL) +
           ((uint64_t)timeinfo.tm_hour * 10000ULL) +
           ((uint64_t)timeinfo.tm_min * 100ULL) +
           (uint64_t)timeinfo.tm_sec;
}

void publishCollectionAlarm(const String &id)
{
    auto *alarmData = new SystemRuntimeStatus();
    alarmData->systemErrorInfo = CHECK_RESULT::COLLECT_ERROR;
    alarmData->errorInfo = "Data collection error for sensor ID: " + id;
    const int subscriberCount = EventBus::getInstance().getSubscriberCount(
        EventID::ALARM_TRIGGERED);
    for (int i = 0; i < subscriberCount; ++i) alarmData->retain();
    EventBus::getInstance().publish(EventID::ALARM_TRIGGERED,
                                    (void *)alarmData);
    alarmData->release();
}

void storePacket(AllDataPacket *allData, const String &id,
                 DataPacket *packet, bool reportFailure = true)
{
    if (allData == nullptr || packet == nullptr) return;
    auto existing = allData->data_map.find(id);
    if (existing != allData->data_map.end() && existing->second != nullptr)
    {
        existing->second->release();
    }
    allData->data_map[id] = packet;
    if (packet->is_valid)
    {
        packet->status = DataStatus::NORMAL;
    }
    else if (reportFailure && packet->status != DataStatus::CALIBRATION)
    {
        LOG_WARNING("Invalid data collected - Sensor ID: %s, status=%c",
                    id.c_str(), dataStatusCode(packet->status));
        publishCollectionAlarm(id);
    }
}

DataPacket *unavailablePacket(const String &id)
{
    DataPacket *packet = new DataPacket();
    strncpy(packet->sensor_id, id.c_str(), sizeof(packet->sensor_id) - 1);
    packet->status = DataStatus::COMMUNICATION_FAILURE;
    return packet;
}

float particleUnitFactor(const String &id)
{
    const COLLECTMAP &config = ConfigManager::getInstance().getCollectConfigs();
    const auto it = config.find(id);
    if (it == config.end()) return 1.0f;
    if (it->second.unit == "mg/m3") return 0.001f;
    if (it->second.unit == "ng/m3") return 1000.0f;
    return 1.0f;
}

void collectParticleChannels(const std::vector<BaseCollector *> &collectors,
                             AllDataPacket *allData, uint32_t deadlineMs)
{
    struct Channel { const char *id; uint8_t offset; };
    static const Channel channels[] = {
        {"a34005", 0}, {"a34004", 2},
        {"a34002", 4}, {"a34001", 6}
    };

    BaseCollector *transport = nullptr;
    bool enabled[4] = {false, false, false, false};
    for (BaseCollector *collector : collectors)
    {
        if (collector == nullptr || !isParticleSensor(collector->getID())) continue;
        if (transport == nullptr) transport = collector;
        for (size_t i = 0; i < 4; ++i)
        {
            if (collector->getID() == channels[i].id) enabled[i] = true;
        }
    }
    if (transport == nullptr) return;

    uint16_t raw[8] = {};
    bool readOk = false;
    auto &serialManager = SerialManager::getInstance();
    SemaphoreHandle_t mutex = serialManager.getMutex("TTL");
    if (!deadlineReached(deadlineMs) && mutex != nullptr &&
        xSemaphoreTake(mutex, pdMS_TO_TICKS(3000)) == pdTRUE)
    {
        readOk = transport->readRegistersForBatch(
            1, 0x0010, 8, raw, SensorReadPolicy::MAX_ATTEMPTS,
            SensorReadPolicy::RESPONSE_TIMEOUT_MS,
            SensorReadPolicy::QUERY_GAP_MS);
        xSemaphoreGive(mutex);
    }

    const bool withinDeadline = !deadlineReached(deadlineMs);
    LOG_INFO("[DIAG] PM_BATCH read_ok=%d within_deadline=%d duration_limit_ms=%u",
             readOk ? 1 : 0, withinDeadline ? 1 : 0,
             (unsigned)BATCH_FINISH_OFFSET_MS);
    for (size_t i = 0; i < 4; ++i)
    {
        if (!enabled[i]) continue;
        DataPacket *packet = unavailablePacket(channels[i].id);
        if (readOk && withinDeadline)
        {
            const uint32_t value =
                (static_cast<uint32_t>(raw[channels[i].offset]) << 16) |
                raw[channels[i].offset + 1];
            if (value != 0xFFFFFFFFU)
            {
                packet->value = static_cast<float>(value) *
                                particleUnitFactor(channels[i].id);
                packet->is_valid = true;
            }
            else
            {
                packet->status = DataStatus::SENSOR_FAULT;
            }
        }
        storePacket(allData, channels[i].id, packet);
    }
}

} // namespace

void collectorManager::poll()
{
    static std::atomic<uint32_t> nextTraceId(1);
    if (_collectors.empty())
    {
        LOG_WARNING("No collectors registered.");
        return;
    }

    const uint32_t stageStartedMs = millis();
    const uint32_t airReadMs = stageStartedMs + AIR_READ_OFFSET_MS;
    const uint32_t finishMs = stageStartedMs + BATCH_FINISH_OFFSET_MS;
    auto &gasCalibration = GasCalibrationManager::getInstance();
    gasCalibration.setNormalCollectionPending(true);

    AllDataPacket *allData = new AllDataPacket();
    allData->trace_id = nextTraceId.fetch_add(1, std::memory_order_relaxed);
    LOG_INFO("[DIAG] COLLECT_STAGE trace=%u stage=non_air offset_ms=45000",
             (unsigned)allData->trace_id);

    for (BaseCollector *collector : _collectors)
    {
        if (collector == nullptr || isParticleSensor(collector->getID()) ||
            GasUnitConverter::isGasSensor(collector->getID()))
        {
            continue;
        }
        storePacket(allData, collector->getID(), collector->collect());
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    waitUntil(airReadMs);
    allData->last_update = currentCollectionTimestamp();
    if (allData->last_update == 0)
    {
        LOG_ERROR("System clock is invalid; this collection cycle will not be published");
    }
    LOG_INFO("[DIAG] COLLECT_STAGE trace=%u stage=air_path offset_ms=60000",
             (unsigned)allData->trace_id);

    collectParticleChannels(_collectors, allData, finishMs);
    for (BaseCollector *collector : _collectors)
    {
        if (collector == nullptr || !GasUnitConverter::isGasSensor(collector->getID()))
        {
            continue;
        }
        if (gasCalibration.isMaintenanceActive())
        {
            DataPacket *maintenance = unavailablePacket(collector->getID());
            maintenance->status = DataStatus::CALIBRATION;
            storePacket(allData, collector->getID(), maintenance, false);
            continue;
        }
        if (deadlineReached(finishMs))
        {
            storePacket(allData, collector->getID(),
                        unavailablePacket(collector->getID()));
            continue;
        }

        DataPacket *packet = collector->collect();
        if (deadlineReached(finishMs))
        {
            if (packet != nullptr) packet->release();
            packet = unavailablePacket(collector->getID());
            LOG_WARNING("[DIAG] AIR_READ_DEADLINE id=%s",
                        collector->getID().c_str());
        }
        storePacket(allData, collector->getID(), packet);
    }

    gasCalibration.setNormalCollectionPending(false);
    if (gasCalibration.isMaintenanceActive())
    {
        for (auto &item : allData->data_map)
        {
            if (!GasUnitConverter::isGasSensor(item.first)) continue;
            if (item.second == nullptr)
            {
                item.second = unavailablePacket(item.first);
            }
            item.second->value = 0.0f;
            item.second->is_valid = false;
            item.second->status = DataStatus::CALIBRATION;
        }
    }

    waitUntil(finishMs);
    const int subCount = EventBus::getInstance().getSubscriberCount(
        EventID::RAW_DATA_COLLECTED);
    if (allData->last_update != 0 && !allData->data_map.empty())
    {
        for (int i = 0; i < subCount; ++i) allData->retain();
        const RuntimeMemorySnapshot memory = observeRuntimeMemory();
        LOG_INFO("[DIAG] COLLECT trace=%u timestamp=%llu sensors=%u subscribers=%d free=%u largest=%u min_free=%u min_largest=%u",
                 (unsigned)allData->trace_id, allData->last_update,
                 (unsigned)allData->data_map.size(), subCount,
                 (unsigned)memory.freeHeap, (unsigned)memory.largestBlock,
                 (unsigned)memory.minimumFreeHeap,
                 (unsigned)memory.minimumLargestBlock);
        EventBus::getInstance().publish(EventID::RAW_DATA_COLLECTED,
                                        (void *)allData);
    }
    allData->release();
}
