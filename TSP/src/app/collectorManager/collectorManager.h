#pragma once
#ifndef COLLECTOR_MANAGER_H
#define COLLECTOR_MANAGER_H

#include <vector>
#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "../../system/call/base_call.h"
#include "../configManager/config.h"
#include "../../inc/sys_init.h"
#include "collect/Tsp/TspCollect.h"
#include "collect/PM10/Pm10Collect.h"
#include "collect/PM2.5/Pm25Collect.h"
#include "collect/PM1/Pm1Collect.h"
#include "collect/CO/CoCollect.h"
#include "collect/SO2/So2Collect.h"
#include "collect/NO2/No2Collect.h"
#include "collect/O3/O3Collect.h"
#include "collect/Pressure/PressureCollect.h"
#include "collect/Noise/NoiseCollect.h"
#include "collect/Tvoc/TvocCollect.h"
#include "collect/WindSpeed/WindSpeedCollect.h"
#include "collect/WindDirection/WindDirCollect.h"
#include "collect/Temp/TempCollect.h"
#include "collect/Mete/MeteCollect.h"


class collectorManager {
private:
    std::vector<BaseCollector*> _collectors; // 采集器列表
    std::map<String, String> idToSerial = {
        {"a34005", "TTL"},
        {"a34001", "TTL"},
        {"a34004", "TTL"},
        {"a34002", "TTL"},
        {"a01007", "485"},
        {"a01008", "485"},
        {"a21008", "485"},
        {"a21004", "485"},
        {"a21005", "485"},
        {"a21026", "485"},
        {"a01001", "485"},
        {"a01002", "485"},
        {"a01006", "485"},
        {"a24035", "485"},
        {"L90", "485"}
    };

    QueueHandle_t _queryQueue;
    collectorManager();
    static collectorManager* _instance;

    void processQuery(JSONCmdData* req);

public:
    static collectorManager& getInstance();

    collectorManager(const collectorManager&) = delete;
    collectorManager& operator=(const collectorManager&) = delete;

    void begin(COLLECTMAP& collectMap);
    void registerCollector(BaseCollector* collector);
    void poll();
    void galpoll();
    size_t getCollectorCount() const { return _collectors.size(); }
};

#endif