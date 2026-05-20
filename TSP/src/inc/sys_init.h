#pragma once
#ifndef INIT_H
#define INIT_H
#include <stdint.h>
#include <map>
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <Ds1302.h>
#include "../module/log/log_manager.h"

//新版本
#define VERSION2 "2.0.0"

struct CSQINFO {
    int csq;
    SemaphoreHandle_t mutex;
};

extern Ds1302 rtc;
extern const char* WeekDays[];
extern CSQINFO csqInfo;

struct DataPacket {
    char sensor_id[16]; 
    float value;
    bool is_valid;
    std::atomic<int> refCount;
    DataPacket() : value(0.0f), is_valid(false), refCount(1) {
        memset(sensor_id, 0, sizeof(sensor_id));
    }
    void retain() {
        refCount.fetch_add(1, std::memory_order_relaxed);
    }
    void release() {
        int old_val = refCount.fetch_sub(1, std::memory_order_acq_rel);
        if (old_val == 1) {
            // LOG_DEBUG("refCount == 0, DataPacket delete");
            delete this;
        }
    }
private:
    ~DataPacket() {}
};

struct AllDataPacket {
    std::map<String, DataPacket*> data_map;
    uint64_t last_update;
    std::atomic<int> refCount;
    AllDataPacket() : last_update(0), refCount(1) {} 
    void retain() {
        refCount.fetch_add(1, std::memory_order_relaxed);
    }
    void release() {
        int old_val = refCount.fetch_sub(1, std::memory_order_acq_rel);
        if (old_val == 1) {
            // LOG_DEBUG("refCount == 0, AllDataPacket delete");
            delete this;
        }
    }

private:
    ~AllDataPacket() {
        for (auto const& [id, packet] : data_map) {
            if (packet != nullptr) {
                packet->release(); 
            }
        }
        data_map.clear();
    }
};
enum class DataTime : uint8_t {
    REAL_DATA,
    MIN_DATA,
    HOUR_DATA,
    DAY_DATA,
};
struct ProcessedDataPacket {
    float value;
    float min_val; 
    float max_val;
    float cou_val;
    bool is_valid;
};
struct AllProcessedDataPacket {
    std::map<String, ProcessedDataPacket> processed_data_map;
    uint64_t last_update; // 20250427 02
    DataTime dataTime;
    std::atomic<int> refCount;
    AllProcessedDataPacket() : last_update(0), refCount(1) {}
    void retain() { 
        refCount.fetch_add(1, std::memory_order_relaxed); 
    }
    void release() { 
        int old_val = refCount.fetch_sub(1, std::memory_order_acq_rel);
        if (old_val == 1) {
            LOG_DEBUG("refCount == 0, AllProcessedDataPacket delete");
            delete this;
        }
    }

private:
    ~AllProcessedDataPacket() {
    }
};

struct JSONCmdData {
    String command;
    String arguments;
    std::atomic<int> refCount;
    JSONCmdData() : refCount(1) {} 
    void retain() {
        refCount.fetch_add(1, std::memory_order_relaxed);
    }
    void release() {
        int old_val = refCount.fetch_sub(1, std::memory_order_acq_rel);
        if (old_val == 1) {
            LOG_DEBUG("refCount == 0, JSONCmdData delete");
            delete this;
        }
    }

};
struct configData{
    String cmd;
    String fileName;
    String content;
    std::atomic<int> refCount;
    configData() : refCount(1) {} 
    void retain() {
        refCount.fetch_add(1, std::memory_order_relaxed);
    }
    void release() {
        int old_val = refCount.fetch_sub(1, std::memory_order_acq_rel);
        if (old_val == 1) {
            LOG_DEBUG("refCount == 0, configData delete");
            delete this;
        }
    }
};
struct galResult {
    String sensor_id;
    bool galRes;
};
struct galDataRes {
    String cmd;
    std::vector<galResult> results;
    std::atomic<int> refCount;
    galDataRes() : refCount(1) {} 
    void retain() {
        refCount.fetch_add(1, std::memory_order_relaxed);
    }
    void release() {
        int old_val = refCount.fetch_sub(1, std::memory_order_acq_rel);
        if (old_val == 1) {
            LOG_DEBUG("refCount == 0, galDataRes delete");
            delete this;
        }
    }
};
// 总线报警信息结构体
enum class RESTART_REASON {
    POWERON_RESET,          //电源
    RTCWDT_RTC_RESET,       //rtc看门狗
    DEEPSLEEP_RESET,        //睡眠重启
    TG1WDT_CPU_RESET,       //任务看门狗
    RTC_SW_CPU_RESET,       //软件重启
};

enum class CHECK_RESULT {
    DEV_OK = 0,             // 一切正常
    FFAT_ERROR,             // ffat错误
    SD_ERROR,               // sd 错误
    TTL_ERROR,              // TTL硬件通讯失败
    RS485_ERROR,            // 配置参数丢失
    TIMEOUT,                // 通讯超时 
    NETWORK_ERROR,          // 网络问题
    CONFIG_ERROR,           // 配置信息错误
    COLLECT_ERROR,          // 采集问题错误
    LCD_ERROR,              // LCD错误
    LED_ERROR,              // LED错误
};

struct SystemRuntimeStatus {
    CHECK_RESULT systemErrorInfo;
    RESTART_REASON last_reason;
    String errorInfo;
    uint32_t uptime_sec;
    bool is_emergency_mode;
    
    std::atomic<int> refCount;
    SystemRuntimeStatus() : refCount(1) {} 
    void retain() {
        refCount.fetch_add(1, std::memory_order_relaxed);
    }
    void release() {
        int old_val = refCount.fetch_sub(1, std::memory_order_acq_rel);
        if (old_val == 1) {
            LOG_DEBUG("refCount == 0, SystemRuntimeStatus delete");
            delete this;
        }
    }
};

// 温控配置结构
struct TEMPCONTROLCONFIG {
    int tempUpperLimit = 50;
    int tempLowerLimit = 0;
    int wetnUpperLimit = 50;
    int wetnLowerLimit = 0;
};

struct SYSTEMSWITCH {
    bool tempConSwitch = true;
    bool save_raw_data = true;
    bool log_to_sd = true;
    bool save_min_data = true;
    bool save_hour_data = true;
    bool save_day_data = true;
    bool enable_hj212 = true;
    bool enable_remote_dtu = true;
};

// 系统主配置
struct SYSTEMCONFIG {
    int collect_time = 60;
    int upload_interval = 60;
    String dtu_server = "";
};

struct HJ212CONFIG {
    String ip = "";
    String mn = "";
    String pw = "123456";
    String st = "31";
    String flag = "9";
    String protocol_version = "2017";
    int timeout = 5;
    int retry_times = 3;
};
struct COLLECTCONFIG {
    String id;
    String name;
    int alarmLimit;
    String unit;

};
using COLLECTMAP = std::map<String, COLLECTCONFIG>;

struct SYSTEM_SETUP {
    int netCsq;
    uint32_t time;
    SemaphoreHandle_t mutex;
};

struct GLOBALCONFIG {
    COLLECTMAP collectConfig;
    SYSTEMCONFIG system;
    HJ212CONFIG hj212;
    TEMPCONTROLCONFIG tempControl;
    SYSTEMSWITCH systemSwitch;
};

#endif // INIT_H