#pragma once
#ifndef DATA_MANAGER_H
#define DATA_MANAGER_H

#include <Arduino.h>
#include <map>
#include <vector>
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "../../module/pack212/pack212.h"
#include "../../inc/sys_init.h" 

// ͳ���߼��ṹ��
struct StatValue {
    float sum = 0;
    float min_v = 1e9;
    float max_v = -1e9;
    int count = 0;
    DataStatus invalid_status = DataStatus::SENSOR_FAULT;
    bool invalid_observed = false;

    void update(float val) {
        sum += val;
        count++;
        if (val < min_v) min_v = val;
        if (val > max_v) max_v = val;
    }

    void observeInvalid(DataStatus status) {
        if (status == DataStatus::NORMAL) return;
        if (!invalid_observed ||
            dataStatusPriority(status) > dataStatusPriority(invalid_status)) {
            invalid_status = status;
        }
        invalid_observed = true;
    }

    void reset() {
        sum = 0;
        count = 0;
        min_v = 1e9; 
        max_v = -1e9;
        invalid_status = DataStatus::SENSOR_FAULT;
        invalid_observed = false;
    }

    float getAvg() const { return count > 0 ? sum / count : 0; }
    float getMax() const { return (max_v < -1e8) ? 0 : max_v; }
    float getMin() const { return (min_v > 1e8) ? 0 : min_v; }
    float getCou() const { return sum; }
    DataStatus getStatus() const {
        return count > 0
            ? DataStatus::NORMAL
            : (invalid_observed ? invalid_status : DataStatus::SENSOR_FAULT);
    }
};

class DataManager {
public:
    // ����ģʽ���ʽӿ�
    static DataManager& getInstance() {
        static DataManager instance;
        return instance;
    }

    void processAllData(AllDataPacket* pkg);
    void begin();
    void poll();
    // Firmware 2.0.3: copy only the values requested by the LCD. This avoids
    // cloning the complete std::map for every get_data command.
    bool readLatestValues(const char* const* ids, size_t idCount,
                          float* values, uint64_t& timestamp);
    // Build a one-shot status view from the latest valid measurements. The
    // caller owns the returned reference and must release it.
    AllProcessedDataPacket* createStatusSnapshot(DataStatus status);

private:
    DataManager();
    ~DataManager() = default;

    // ��ֹ����
    DataManager(const DataManager&) = delete;
    DataManager& operator=(const DataManager&) = delete;

    QueueHandle_t _queryQueue;
    SemaphoreHandle_t _statsMutex;  // Protect access to _min_stats, _hour_stats, _day_stats
    SemaphoreHandle_t _lastMinDataMutex; // Protect access to _last_min_snapshot
    SemaphoreHandle_t _lastRealDataMutex; // Protect the latest real-time snapshot
    
    void processQuery(JSONCmdData* req);

    // �ڲ�ͳ�� Map (�� _statsMutex ����)
    std::map<String, StatValue> _min_stats;
    std::map<String, StatValue> _hour_stats;
    std::map<String, StatValue> _day_stats;
    std::map<String, ProcessedDataPacket> _last_min_snapshot;
    std::map<String, ProcessedDataPacket> _last_real_snapshot;
    uint64_t _l_m_s_timestamp;
    uint64_t _last_real_timestamp;

    // ʱ���¼ (��ʽ��YYYYMMDDHHMMSS)
    uint64_t _last_min_time;
    uint64_t _last_hour_time;
    uint64_t _last_day_time;

    // �ڲ���������
    void checkAndDispatch(AllDataPacket* rawData);
    void dispatchRealPacket(const AllDataPacket* rawData);
    void dispatchPacket(DataTime type, uint64_t ts, std::map<String, StatValue>& source, uint32_t traceId);
};

#endif
