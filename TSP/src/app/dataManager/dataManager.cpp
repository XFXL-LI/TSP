#include "dataManager.h"
#include "../../system/event/eventBus.h"
#include "../../inc/sys_init.h"

DataManager::DataManager() {
    _last_min_time = 1;
    _last_hour_time = 1;
    _last_day_time = 1;
    _l_m_s_timestamp = 1;
    _statsMutex = xSemaphoreCreateMutex();
    _lastMinDataMutex = xSemaphoreCreateMutex();
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
            }
            allData->release();
        } else {
            LOG_ERROR("DataManager not subseribe this, send message error!");
        }
    }
}

void DataManager::processAllData(AllDataPacket* pkg) {
    if (pkg == nullptr) return;
    
    if (xSemaphoreTake(_statsMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        uint64_t currentMinute = (pkg->last_update / 100) % 100;
        bool isThirdMinute = (currentMinute % 3 == 0);
        for (auto const& [id, dataPtr] : pkg->data_map) {
            if (dataPtr != nullptr && dataPtr->is_valid) {
                float val = dataPtr->value;
                _min_stats[id].update(val);
                if (isThirdMinute) {
                    _hour_stats[id].update(val);
                }
            }
        }
        xSemaphoreGive(_statsMutex);
    } else {
        LOG_WARNING("DataManager: Failed to acquire mutex for processAllData");
        return;
    }
    
    checkAndDispatch(pkg->last_update);
}

void DataManager::processQuery(JSONCmdData* req) {
    AllProcessedDataPacket* pkg = new AllProcessedDataPacket();
    if (xSemaphoreTake(_lastMinDataMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        pkg->processed_data_map = _last_min_snapshot;
        pkg->last_update = _l_m_s_timestamp;
        xSemaphoreGive(_lastMinDataMutex);
    }
    int subCount = EventBus::getInstance().getSubscriberCount(EventID::DATA_QUERY_RES);
    for (int i = 0; i < subCount; i++) pkg->retain(); 
    EventBus::getInstance().publish(EventID::DATA_QUERY_RES, pkg);
    pkg->release();
}

void DataManager::checkAndDispatch(uint64_t ts) {
    if (ts < 20260527000000) {
        return;
    }
    uint64_t currentMin = ts; 
    uint64_t currentHour = ts / 10000; 
    uint64_t currentDay = ts / 1000000; 

    LOG_DEBUG("checkAndDispatch time: %llu", currentMin);

    if (xSemaphoreTake(_statsMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        LOG_WARNING("DataManager: Failed to acquire mutex for checkAndDispatch");
        return;
    }
    
    if (_last_min_time != 0 && currentMin > _last_min_time) {
        dispatchPacket(DataTime::MIN_DATA, currentMin, _min_stats);
        if (xSemaphoreTake(_lastMinDataMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            _last_min_snapshot.clear();
            for (auto &item : _min_stats) {
                ProcessedDataPacket p;
                p.is_valid = true;
                p.value = item.second.getAvg();
                _last_min_snapshot[item.first] = p;
            }
            _l_m_s_timestamp = ts;
            xSemaphoreGive(_lastMinDataMutex);
        }
        for(auto &it : _min_stats) it.second.reset(); 
        _last_min_time = currentMin;
    }
    
    if (_last_hour_time != 0 && currentHour > _last_hour_time) {
        vTaskDelay(pdMS_TO_TICKS(2000));
        for (auto &item : _hour_stats) {
            float hourAvg = item.second.getAvg();
            _day_stats[item.first].update(hourAvg);
        }
        dispatchPacket(DataTime::HOUR_DATA, _last_hour_time * 10000, _hour_stats);
        for(auto &it : _hour_stats) it.second.reset(); 
        _last_hour_time = currentHour;
    }
    
    if (_last_day_time != 0 && currentDay > _last_day_time) {
        vTaskDelay(pdMS_TO_TICKS(2000));
        dispatchPacket(DataTime::DAY_DATA, _last_day_time * 1000000, _day_stats);
        for(auto &it : _day_stats) it.second.reset(); 
        _last_day_time = currentDay;
    }
    
    xSemaphoreGive(_statsMutex);
}

void DataManager::dispatchPacket(DataTime type, uint64_t ts, std::map<String, StatValue>& source) {
    AllProcessedDataPacket* pkg = new AllProcessedDataPacket();
    pkg->last_update = ts;
    pkg->dataTime = type;
    for (auto &item : source) {
        ProcessedDataPacket p; 
        p.value = item.second.getAvg();
        p.min_val = item.second.getMin();
        p.max_val = item.second.getMax();
        p.cou_val = item.second.getCou();
        p.is_valid = item.second.count > 0;
        pkg->processed_data_map[item.first] = p;
    }
    int subCount = EventBus::getInstance().getSubscriberCount(EventID::PROCESSED_DATA_COLLECTED);
    for (int i = 0; i < subCount; i++) {
        pkg->retain();
    }
    EventBus::getInstance().publish(EventID::PROCESSED_DATA_COLLECTED, pkg);
    pkg->release();
}