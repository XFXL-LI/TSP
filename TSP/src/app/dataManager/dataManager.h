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

// 统计逻辑结构体
struct StatValue {
    float sum = 0;
    float min_v = 1e9;
    float max_v = -1e9;
    int count = 0;

    void update(float val) {
        sum += val;
        count++;
        if (val < min_v) min_v = val;
        if (val > max_v) max_v = val;
    }

    void reset() {
        sum = 0;
        count = 0;
        min_v = 1e9; 
        max_v = -1e9;
    }

    float getAvg() const { return count > 0 ? sum / count : 0; }
    float getMax() const { return (max_v < -1e8) ? 0 : max_v; }
    float getMin() const { return (min_v > 1e8) ? 0 : min_v; }
    float getCou() const { return sum; }
};

class DataManager {
public:
    // 单例模式访问接口
    static DataManager& getInstance() {
        static DataManager instance;
        return instance;
    }

    void processAllData(AllDataPacket* pkg);
    void begin();
    void poll();

private:
    DataManager();
    ~DataManager() = default;

    // 禁止拷贝
    DataManager(const DataManager&) = delete;
    DataManager& operator=(const DataManager&) = delete;

    QueueHandle_t _queryQueue;
    SemaphoreHandle_t _statsMutex;  // Protect access to _min_stats, _hour_stats, _day_stats
    SemaphoreHandle_t _lastMinDataMutex; // Protect access to _last_min_snapshot
    
    void processQuery(JSONCmdData* req);

    // 内部统计 Map (受 _statsMutex 保护)
    std::map<String, StatValue> _min_stats;
    std::map<String, StatValue> _hour_stats;
    std::map<String, StatValue> _day_stats;
    std::map<String, ProcessedDataPacket> _last_min_snapshot;
    uint64_t _l_m_s_timestamp;

    // 时间记录 (格式：YYYYMMDDHHMMSS)
    uint64_t _last_min_time;
    uint64_t _last_hour_time;
    uint64_t _last_day_time;

    // 内部辅助方法
    void checkAndDispatch(AllDataPacket* rawData);
    void dispatchRealPacket(const AllDataPacket* rawData);
    void dispatchPacket(DataTime type, uint64_t ts, std::map<String, StatValue>& source);
};

#endif
