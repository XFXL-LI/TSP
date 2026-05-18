#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include <Arduino.h>
#include <map>
#include "../../module/file/file_storage.h"
#include "../../module/json/config_json.h"
#include "../../inc/sys_init.h"

#define CONFIG_PATH "/config.json"
#define MODEL_PATH "/model.json"
#define SYSTEM_PATH "/system.json"
#define SWITCH_PATH "/switch.json"
#define TEMP_CONTROL_PATH "/tempControl.json"
#define HJ212_PATH "/hj212.json"

enum class FeatureType
{
    SaveRaw,
    SaveMin,
    HJ212,
    Remote
};

class ConfigManager
{
private:
    file_storage &fs;
    GLOBALCONFIG globalCfg;
    SYSTEM_SETUP systemSetup;
    QueueHandle_t _queryQueue;

    void _parseSystem(cJSON *node, SYSTEMCONFIG &target);
    void _parseHJ212(cJSON *node, HJ212CONFIG &target);
    void _parseSensors(cJSON *arrayNode, COLLECTMAP &target_map);
    void _parseTempControl(cJSON *node, TEMPCONTROLCONFIG &target);
    void _parseSwitch(cJSON *node, SYSTEMSWITCH &target);
    void _parseSystemInfo(cJSON *node, SYSTEMCONFIG &target);

    void setConfigRes(JSONCmdData *req);
    void getConfigRes(JSONCmdData *req);
    void processQuery(JSONCmdData *req);

public:
    static ConfigManager &getInstance()
    {
        static ConfigManager instance;
        return instance;
    }
    ConfigManager();

    void begin();
    // --- 核心操作接口 ---
    bool loadFromFile(const char *path);
    bool saveToFile(const char *path);

    bool saveConfig(const char *path, const String &content);
    bool removeConfig(const char *path);

    String getConfigJson(const char *path);

    void poll();

    void runIf(bool toggle, const char *name, std::function<void()> func)
    {
        if (toggle)
        {
            func();
        }
    }
    void runIfSaveRaw(std::function<void()> func)
    {
        runIf(globalCfg.systemSwitch.save_raw_data, "SaveRaw", func);
    }
    void runIfSaveMin(std::function<void()> func)
    {
        runIf(globalCfg.systemSwitch.save_min_data, "SaveMin", func);
    }
    void runIfSaveHour(std::function<void()> func)
    {
        runIf(globalCfg.systemSwitch.save_hour_data, "SaveHour", func);
    }
    void runIfSaveDay(std::function<void()> func)
    {
        runIf(globalCfg.systemSwitch.save_day_data, "SaveDay", func);
    }
    void runIfHJ212(std::function<void()> func)
    {
        runIf(globalCfg.systemSwitch.enable_hj212, "HJ212", func);
    }
    void runIfRemote(std::function<void()> func)
    {
        runIf(globalCfg.systemSwitch.enable_remote_dtu, "RemoteDTU", func);
    }
    void runIfSaveLog(std::function<void()> func)
    {
        runIf(globalCfg.systemSwitch.log_to_sd, "SaveLog", func);
    }
    void runIfTempCon(std::function<void()> func)
    {
        runIf(globalCfg.systemSwitch.tempConSwitch, "TempCon", func);
    }
    // auto &cfg = ConfigManager::getInstance();

    // cfg.runIfSaveMin([&]() {
    //     // 这里的代码只有在 system.json 中 save_min_data 为 true 时才会执行
    //     filesysManager::getInstance().storeProcessedPacket(allData);
    // });
    // cfg.runIfHJ212([]() {
    //     LOG_INFO("HJ212 service starting...");
    //     // 启动 HJ212 相关的 Task
    // });

    // --- 定向解析接口 (传入 JSON 字符串) ---
    bool updateSystem(const char *json_str);
    bool updateHJ212(const char *json_str);
    bool updateSensors(const char *json_str);
    bool updateSetup(const SYSTEM_SETUP &newSetup);
    void updateTimeAuto(void);

    SYSTEM_SETUP getSetup();

    int getCollectInterval() const { return globalCfg.system.collect_time; }

    GLOBALCONFIG &getConfig() { return globalCfg; }
    SYSTEMCONFIG &getSystem() { return globalCfg.system; }
    HJ212CONFIG &getHJ212() { return globalCfg.hj212; }
    COLLECTMAP &getCollectConfigs()
    {
        return globalCfg.collectConfig;
    }
};

#endif