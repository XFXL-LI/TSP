#include "dataManager.h"
#include "../../system/ota/remote_ota_manager.h"
#include "../../system/event/eventBus.h"
#include "../../inc/sys_init.h"
#include <new>
#include <time.h>
#include <math.h>
#include <esp_timer.h>
#include "../../module/diagnostics/CalendarClock.h"
#include "../../module/diagnostics/DeviceRuntimeStatus.h"

DataManager::DataManager() {
    _last_min_time = 0;
    _last_hour_time = 0;
    _last_day_time = 0;
    _l_m_s_timestamp = 0;
    _last_real_timestamp = 0;
    _statsMutex = xSemaphoreCreateMutex();
    _lastMinDataMutex = xSemaphoreCreateMutex();
    _lastRealDataMutex = xSemaphoreCreateMutex();
    if (!_statsMutex || !_lastMinDataMutex || !_lastRealDataMutex) {
        DeviceRuntimeStatus::criticalInitFailed();
    }
}
void DataManager::begin() {
    _queryQueue = EventBus::getInstance().createReceiverQueue(10, "DATA_MANAGER");
    if (!_queryQueue) {
        DeviceRuntimeStatus::criticalInitFailed();
        return;
    }
    EventBus::getInstance().subscribe(EventID::DATA_QUERY_REQ, _queryQueue);
    EventBus::getInstance().subscribe(EventID::RAW_DATA_COLLECTED, _queryQueue);
}

void DataManager::poll() {
    if (!_queryQueue) return;
    EventMsg msg;
    if (EventBus::waitEvent(_queryQueue, msg)) {
        RemoteOtaManager::BusinessActivityGuard businessActivity;
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

bool DataManager::readLatestValues(const char* const* ids, size_t idCount,
                                   float* values, uint64_t& timestamp,
                                   RealtimeSnapshotInfo* sample) {
    if (ids == nullptr || values == nullptr) return false;
    for (size_t i = 0; i < idCount; ++i) values[i] = 0.0f;
    if (_lastRealDataMutex == nullptr) return false;

    if (xSemaphoreTake(_lastRealDataMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        LOG_WARNING("[DIAG] GET_DATA_SNAPSHOT_BUSY");
        return false;
    }
    timestamp = _last_real_timestamp;
    if (sample != nullptr) {
        sample->seq = _last_real_seq;
        sample->ageMs = realtimeAgeMs(_last_real_seq, _last_real_sampled_us,
                                     static_cast<uint64_t>(esp_timer_get_time()));
        sample->validMask = 0;
    }
    for (size_t i = 0; i < idCount; ++i) {
        if (ids[i] == nullptr) continue;
        for (const auto& item : _last_real_snapshot) {
            if (item.first.equals(ids[i])) {
                if (item.second.is_valid && isfinite(item.second.value)) {
                    values[i] = item.second.value;
                    if (sample != nullptr && i < 16) sample->validMask |= 1U << i;
                }
                break;
            }
        }
    }
    xSemaphoreGive(_lastRealDataMutex);
    return true;
}

AllProcessedDataPacket* DataManager::createStatusSnapshot(DataStatus status) {
    // OTA maintenance reports are dated HJ212 data, not local display queries.
    const uint64_t timestamp = CalendarClock::currentTimestamp();
    if (timestamp == 0) return nullptr;
    AllProcessedDataPacket* pkg = new (std::nothrow) AllProcessedDataPacket();
    if (pkg == nullptr) return nullptr;

    if (_lastRealDataMutex == nullptr ||
        xSemaphoreTake(_lastRealDataMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        pkg->release();
        return nullptr;
    }
    for (const auto& item : _last_real_snapshot) {
        if (!item.second.is_valid) continue;
        ProcessedDataPacket copy = item.second;
        copy.is_valid = false;
        copy.status = status;
        pkg->processed_data_map[item.first] = copy;
    }
    xSemaphoreGive(_lastRealDataMutex);

    pkg->last_update = timestamp;
    pkg->dataTime = DataTime::REAL_DATA;
    return pkg;
}

void DataManager::processQuery(JSONCmdData* req) {
    // Kept for compatibility with other internal callers. LCD get_data uses
    // readLatestValues() directly and no longer reaches this allocation path.
    AllProcessedDataPacket* pkg = new (std::nothrow) AllProcessedDataPacket();
    if (pkg == nullptr) {
        LOG_ERROR("[DIAG] DATA_QUERY_OOM free=%u largest=%u",
                  ESP.getFreeHeap(), ESP.getMaxAllocHeap());
        return;
    }
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

    // Local measurements must remain available without RTC or network time.
    // Only dated events may feed SD records, statistics and HJ212.
    updateLatestSnapshot(rawData);
    uint64_t ts = rawData->last_update;
    if (!CalendarClock::isValidTimestamp(ts)) {
        if (_statsMutex != nullptr &&
            xSemaphoreTake(_statsMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            _min_stats.clear();
            _hour_stats.clear();
            _day_stats.clear();
            _last_min_time = _last_hour_time = _last_day_time = 0;
            xSemaphoreGive(_statsMutex);
        }
        return;
    }

    // Real-time reports contain only values from this collection cycle.
    dispatchRealPacket(rawData);

    uint64_t currentMin = ts / 100; 
    uint64_t currentHour = ts / 10000; 
    uint64_t currentDay = ts / 1000000; 

    LOG_DEBUG("checkAndDispatch time: %llu", currentMin);

    if (_statsMutex == nullptr ||
        xSemaphoreTake(_statsMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
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
            } else if (item.second.invalid_observed) {
                _day_stats[item.first].observeInvalid(item.second.invalid_status);
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
                p.status = item.second.getStatus();
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

    for (auto const& [id, dataPtr] : rawData->data_map) {
        if (dataPtr == nullptr) continue;
        if (dataPtr->is_valid) {
            _min_stats[id].update(dataPtr->value);
            _hour_stats[id].update(dataPtr->value);
        } else {
            _min_stats[id].observeInvalid(dataPtr->status);
            _hour_stats[id].observeInvalid(dataPtr->status);
        }
    }

    xSemaphoreGive(_statsMutex);
}

void DataManager::updateLatestSnapshot(const AllDataPacket* rawData) {
    if (_lastRealDataMutex == nullptr ||
        xSemaphoreTake(_lastRealDataMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        LOG_WARNING("DataManager: Failed to update serial real-time snapshot");
        return;
    }
    // Reuse existing nodes; remove disabled factors instead of retaining stale data.
    for (auto it = _last_real_snapshot.begin(); it != _last_real_snapshot.end();) {
        if (rawData->data_map.find(it->first) == rawData->data_map.end()) {
            it = _last_real_snapshot.erase(it);
        } else {
            ++it;
        }
    }
    for (const auto& item : rawData->data_map) {
        ProcessedDataPacket data = {};
        if (item.second != nullptr) {
            data.value = data.min_val = data.max_val = data.cou_val = item.second->value;
            data.is_valid = item.second->is_valid && isfinite(data.value);
            data.status = item.second->status;
        }
        _last_real_snapshot[item.first] = data;
    }
    _last_real_timestamp = CalendarClock::isValidTimestamp(rawData->last_update)
        ? rawData->last_update : 0;
    _last_real_seq = nextRealtimeSequence(_last_real_seq);
    _last_real_sampled_us = static_cast<uint64_t>(esp_timer_get_time());
    xSemaphoreGive(_lastRealDataMutex);
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
            data.status = dataPtr->status;
        }
        pkg->processed_data_map[id] = data;
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
        p.status = item.second.getStatus();
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
