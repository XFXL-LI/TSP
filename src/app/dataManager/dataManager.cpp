#include "dataManager.h"
#include "../../system/event/eventBus.h"
#include "../../inc/sys_init.h"

DataManager::DataManager() {
    _last_min_time = 0;
    _last_hour_time = 0;
    _last_day_time = 0;
    _l_m_s_timestamp = 0;
    _last_real_timestamp = 0;
    _statsMutex = xSemaphoreCreateMutex();
    _lastMinDataMutex = xSemaphoreCreateMutex();
    _lastRealDataMutex = xSemaphoreCreateMutex();
}
void DataManager::begin() {
    _queryQueue = EventBus::getInstance().createReceiverQueue(10);
    EventBus::getInstance().subscribe(EventID::DATA_QUERY_REQ, _queryQueue);
    EventBus::getInstance().subscribe(EventID::RAW_DATA_COLLECTED, _queryQueue);
}

void DataManager::poll() {
    EventMsg msg;
    if (EventBus::waitEvent(_queryQueue, msg)) {
        if (msg.id == EventID::DATA_QUERY_REQ) {
            JSONCmdData* req = (JSONCmdData*)msg.data;
            processQuery(req);
            req->release();
        } else if (msg.id == EventID::RAW_DATA_COLLECTED) {
            AllDataPacket *allData = (AllDataPacket *)msg.data;
            if (allData != nullptr)
            {
                processAllData(allData);
                allData->release();
            }
        } else {
            LOG_ERROR("DataManager not subseribe this, send message error!");
        }
    }
}

void DataManager::processAllData(AllDataPacket* pkg) {
    if (pkg == nullptr) return;
    checkAndDispatch(pkg);
}

void DataManager::processQuery(JSONCmdData* req) {
    AllProcessedDataPacket* pkg = new AllProcessedDataPacket();
    if (xSemaphoreTake(_lastRealDataMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        pkg->processed_data_map = _last_real_snapshot;
        pkg->last_update = _last_real_timestamp;
        pkg->dataTime = DataTime::REAL_DATA;
        xSemaphoreGive(_lastRealDataMutex);
    }
    int subCount = EventBus::getInstance().getSubscriberCount(EventID::DATA_QUERY_RES);
    for (int i = 0; i < subCount; i++) pkg->retain(); 
    EventBus::getInstance().publish(EventID::DATA_QUERY_RES, pkg);
    pkg->release();
}

void DataManager::checkAndDispatch(AllDataPacket* rawData) {
    if (rawData == nullptr) {
        return;
    }

    uint64_t ts = rawData->last_update;
    if (ts < 20260527000000) {
        return;
    }

    // Real-time reports contain only values from this collection cycle.
    dispatchRealPacket(rawData);

    uint64_t currentMin = ts / 100; 
    uint64_t currentHour = ts / 10000; 
    uint64_t currentDay = ts / 1000000; 

    LOG_DEBUG("checkAndDispatch time: %llu", currentMin);

    if (xSemaphoreTake(_statsMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        LOG_WARNING("DataManager: Failed to acquire mutex for checkAndDispatch");
        return;
    }

    bool firstSample = _last_min_time == 0 ||
                       _last_hour_time == 0 ||
                       _last_day_time == 0;
    if (firstSample) {
        _last_min_time = currentMin;
        _last_hour_time = currentHour;
        _last_day_time = currentDay;
    }

    // Close the completed hour before adding the first sample of the new hour.
    if (!firstSample && currentHour > _last_hour_time) {
        for (auto &item : _hour_stats) {
            if (item.second.count > 0) {
                _day_stats[item.first].update(item.second.getAvg());
            }
        }
        dispatchPacket(DataTime::HOUR_DATA, _last_hour_time * 10000, _hour_stats, rawData->trace_id);
        for (auto &item : _hour_stats) item.second.reset();
        _last_hour_time = currentHour;
    }

    // Close the completed day after its final hour has entered day statistics.
    if (!firstSample && currentDay > _last_day_time) {
        dispatchPacket(DataTime::DAY_DATA, _last_day_time * 1000000, _day_stats, rawData->trace_id);
        for (auto &item : _day_stats) item.second.reset();
        _last_day_time = currentDay;
    }

    // CN=2051 is reported once per completed 10-minute window.
    if (!firstSample && currentMin / 10 > _last_min_time / 10) {
        uint64_t windowStartTime = (_last_min_time / 10) * 10 * 100;
        dispatchPacket(DataTime::MIN_DATA, windowStartTime, _min_stats, rawData->trace_id);
        if (xSemaphoreTake(_lastMinDataMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            _last_min_snapshot.clear();
            for (auto &item : _min_stats) {
                ProcessedDataPacket p = {};
                p.is_valid = item.second.count > 0;
                p.value = item.second.getAvg();
                p.min_val = item.second.getMin();
                p.max_val = item.second.getMax();
                p.cou_val = item.second.getCou();
                _last_min_snapshot[item.first] = p;
            }
            _l_m_s_timestamp = windowStartTime;
            xSemaphoreGive(_lastMinDataMutex);
        }
        for (auto &item : _min_stats) item.second.reset();
        LOG_INFO("Dispatched completed 10-minute data window: start=%llu, end=%llu",
                 windowStartTime, currentMin * 100);
    }

    _last_min_time = currentMin;

    uint64_t minuteOfHour = currentMin % 100;
    bool isThirdMinute = minuteOfHour % 3 == 0;
    for (auto const& [id, dataPtr] : rawData->data_map) {
        if (dataPtr != nullptr && dataPtr->is_valid) {
            _min_stats[id].update(dataPtr->value);
            if (isThirdMinute) {
                _hour_stats[id].update(dataPtr->value);
            }
        }
    }

    xSemaphoreGive(_statsMutex);
}

void DataManager::dispatchRealPacket(const AllDataPacket* rawData) {
    AllProcessedDataPacket* pkg = new AllProcessedDataPacket();
    pkg->last_update = rawData->last_update;
    pkg->dataTime = DataTime::REAL_DATA;
    pkg->trace_id = rawData->trace_id;

    for (auto const& [id, dataPtr] : rawData->data_map) {
        ProcessedDataPacket data = {};
        if (dataPtr != nullptr) {
            data.value = dataPtr->value;
            data.min_val = dataPtr->value;
            data.max_val = dataPtr->value;
            data.cou_val = dataPtr->value;
            data.is_valid = dataPtr->is_valid;
        }
        pkg->processed_data_map[id] = data;
    }

    if (xSemaphoreTake(_lastRealDataMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        _last_real_snapshot = pkg->processed_data_map;
        _last_real_timestamp = pkg->last_update;
        xSemaphoreGive(_lastRealDataMutex);
        LOG_DEBUG("Updated serial real-time snapshot: %llu, sensors=%d",
                  _last_real_timestamp, _last_real_snapshot.size());
    } else {
        LOG_WARNING("DataManager: Failed to update serial real-time snapshot");
    }

    LOG_INFO("[DIAG] PROCESS trace=%u type=%u timestamp=%llu records=%u",
             (unsigned)pkg->trace_id,
             (unsigned)pkg->dataTime,
             pkg->last_update,
             (unsigned)pkg->processed_data_map.size());

    int subCount = EventBus::getInstance().getSubscriberCount(
        EventID::PROCESSED_DATA_COLLECTED);
    for (int i = 0; i < subCount; i++) {
        pkg->retain();
    }
    EventBus::getInstance().publish(EventID::PROCESSED_DATA_COLLECTED, pkg);
    pkg->release();
}

void DataManager::dispatchPacket(DataTime type, uint64_t ts, std::map<String, StatValue>& source, uint32_t traceId) {
    AllProcessedDataPacket* pkg = new AllProcessedDataPacket();
    pkg->last_update = ts;
    pkg->dataTime = type;
    pkg->trace_id = traceId;
    for (auto &item : source) {
        ProcessedDataPacket p; 
        p.value = item.second.getAvg();
        p.min_val = item.second.getMin();
        p.max_val = item.second.getMax();
        p.cou_val = item.second.getCou();
        p.is_valid = item.second.count > 0;
        pkg->processed_data_map[item.first] = p;
    }
    LOG_INFO("[DIAG] PROCESS trace=%u type=%u timestamp=%llu records=%u",
             (unsigned)pkg->trace_id,
             (unsigned)pkg->dataTime,
             pkg->last_update,
             (unsigned)pkg->processed_data_map.size());
    int subCount = EventBus::getInstance().getSubscriberCount(EventID::PROCESSED_DATA_COLLECTED);
    for (int i = 0; i < subCount; i++) {
        pkg->retain();
    }
    EventBus::getInstance().publish(EventID::PROCESSED_DATA_COLLECTED, pkg);
    pkg->release();
}
