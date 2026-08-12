#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include "esp_task_wdt.h"
#include <SoftwareSerial.h>
#include <time.h>
#include <stdio.h>
#include <vector>
#include <new>
#include <utility>
#include "system.h"
#include "src/module/log/log_manager.h"
#include "../../inc/sys_init.h"
#include "../event/eventBus.h"
#include "../ota/remote_ota_manager.h"
#include "../../module/Serial/SerialManager.h"
#include "../call/base_call.h"
#include "../../module/json/config_json.h"
#include "../../module/file/file_storage.h"
#include "../../module/pack212/pack212.h"
#include "../../app/configManager/config.h"
#include "../../app/collectorManager/collect/Tsp/TspCollect.h"
#include "../../app/collectorManager/collectorManager.h"
#include "../../app/dataManager/dataManager.h"
#include "../../app/alarmManager/alarmManager.h"
#include "../../app/permissionManager/permissionManager.h"
#include "../../app/dtuManager/dtuManager.h"
#include "../../app/filesysManager/filesysManager.h"
#include "../../app/tempManager/tempManager.h"
#include "../../app/ledManager/ledManager.h"


// Firmware 2.0.6 integration and hardware validation keep DEBUG enabled.
#define DEBUG

#define PUMP1_PIN 41
#define ALARM_PIN 40    // 12v电控制开�?

// Firmware 2.0.4 (2026-07-31):
// Yinerda M100M-B2 requires packets to be sent one at a time. Keep the
// field-test interval at 3000 ms; this is independent of CSQ scheduling.
static constexpr uint32_t HJ212_PACKET_GAP_MS = 3000;
// Firmware 2.0.2 (2026-07-30): pending recovery is maintenance work, not a
// per-CSQ operation. Back off scans to avoid repeated task allocation.
static constexpr uint32_t PENDING_SCAN_INTERVAL_MS = 5UL * 60UL * 1000UL;
static constexpr uint32_t CSQ_POLL_INTERVAL_MS = 60UL * 1000UL;
// Firmware 2.0.4 hardware validation found that a full 60-second delay after
// CSQ_DEFERRED can phase-lock the CSQ task to the once-per-minute live upload.
// Retry briefly after a collision while keeping healthy polling at 60 seconds.
static constexpr uint32_t CSQ_RETRY_INTERVAL_MS = 5UL * 1000UL;
static constexpr uint32_t TIME_SYNC_INTERVAL_MS = 24UL * 60UL * 60UL * 1000UL;
static constexpr uint32_t TIME_SYNC_RETRY_INTERVAL_MS = 5UL * 60UL * 1000UL;
static constexpr int TIME_SYNC_SAFE_SECOND_START = 10;
static constexpr int TIME_SYNC_SAFE_SECOND_END = 40;
static constexpr int64_t TIME_SYNC_APPLY_THRESHOLD_SEC = 3;

// Firmware 2.0.6: keep durable data work above UI/background work. Serial
// parsers retain their hardware-verified priority 10 pending dedicated tests.
static constexpr UBaseType_t TASK_PRIORITY_OTA_ACTIVE = 11;
static constexpr UBaseType_t TASK_PRIORITY_DATA_PROCESS = 10;
static constexpr UBaseType_t TASK_PRIORITY_SERIAL_CONTROL = 10;
static constexpr UBaseType_t TASK_PRIORITY_COLLECT = 9;
static constexpr UBaseType_t TASK_PRIORITY_STORAGE = 9;
static constexpr UBaseType_t TASK_PRIORITY_HJ212 = 8;
static constexpr UBaseType_t TASK_PRIORITY_ALARM = 7;
static constexpr UBaseType_t TASK_PRIORITY_TEMP_CONTROL = 7;
static constexpr UBaseType_t TASK_PRIORITY_MAINTENANCE = 6;
static constexpr UBaseType_t TASK_PRIORITY_PERMISSION = 4;
static constexpr UBaseType_t TASK_PRIORITY_CONFIG = 4;
static constexpr UBaseType_t TASK_PRIORITY_CALIBRATION = 4;
static constexpr UBaseType_t TASK_PRIORITY_OTA_LISTENER = 4;
static constexpr UBaseType_t TASK_PRIORITY_LED = 3;
static constexpr UBaseType_t TASK_PRIORITY_MQTT = 3;

// ********** 时间相关定义 **********
Ds1302 rtc(17, 6, 7);
const char *WeekDays[] =
    {
        "Monday",
        "Tuesday",
        "Wednesday",
        "Thursday",
        "Friday",
        "Saturday",
        "Sunday"};

void timeInit(uint64_t timestamp);
bool updateMillisTime(uint64_t newTime);
uint64_t getCurrentTime();
static bool isValidClockTime(uint64_t timestamp);
static bool clockTimestampToEpoch(uint64_t timestamp, time_t &epoch);
static bool synchronizeTimeFromDtu();
// ********** 时间相关定义 **********

// ********** 其他全局定义 **********
void setUpInit(void);
void fileRestore(void);
//

SYSINFO systemInfo;
static std::atomic<bool> networkRestoreRunning(false);

// 采集
static void CollectTask(void *pvParameters); // 采集后第一轮判断是否报�?
// HJ212 打包
static void Hj212_2017SendTask(void *pvParameters);
static void Hj212_2025SendTask(void *pvParameters);
// 断点续传
static void recoverPendingPacket(const PendingPacketInfo &pending);
// 保存数据
static void SaveDataFileTask(void *pvParameters);
// OTA 升级
// LED
static void LedPrintTask(void *pvParameters);
// 串口解析及管理权�?
static void SerialControlTask_lcd(void *pvParameters);
static void SerialControlTask_dtu(void *pvParameters);
static void PermissionTask(void *pvParameters);
// 远程控制
static void CollectGalTask(void *pvParameters);
// 数据处理中转
static void DataProcessTask(void *pvParameters);
// 更新环境变量
static void updateSetupTask(void *pvParameters);
// 更新配置（串口）
static void updateConfigTask(void *pvParameters);
// 温度控制
static void TempControlTask(void *pvParameters);
// mqtt 订阅发�?
static void MqttPublicTask(void *pvParameters);

static void AlarmTask(void *pvParameters);
static bool sendHJ212PacketLocked(const String &packet, int maxRetry);

System::System()
{
}
System::~System()
{
}
void System::SystemInit(void)
{

#ifdef DEBUG
    LogManager::getInstance().setLevel(LOG_LEVEL_DEBUG);
#else
    LogManager::getInstance().setLevel(LOG_LEVEL_INFO);
#endif
    LOG_INFO("Firmware version: %s", VERSION2);
    LOG_DEBUG("System init start");

    // Firmware 2.0.1 (2026-07-30): create shared system-state protection
    // before Temp/CSQ tasks start, removing the startup mutex race.
    if (systemInfo.mutex == NULL) {
        systemInfo.mutex = xSemaphoreCreateMutex();
    }

    alarmManager::getInstance().printRestartInfo();

    SystemSerialInit();
    vTaskDelay(pdMS_TO_TICKS(200));
    SystemConfigInit();
    vTaskDelay(pdMS_TO_TICKS(500));
    SystemSetupInit();
    vTaskDelay(pdMS_TO_TICKS(500));
    SystemTaskInit();
}
void System::SystemSerialInit(void)
{
    LOG_INFO("Serial init start!");
    auto &sm = SerialManager::getInstance();
    sm.begin();
}
void System::SystemConfigInit(void)
{
    auto &cfg = ConfigManager::getInstance();
    cfg.begin();

    HJ212CONFIG hj212Cfg = cfg.getHJ212();
    if (hj212Cfg.protocol_version == "2017")
    {
        LOG_INFO("HJ212 protocol version set to 2017");
        xTaskCreatePinnedToCore(Hj212_2017SendTask, "Hj2017Task", 8 * 1024, NULL, TASK_PRIORITY_HJ212, NULL, 0);
    }
    else if (hj212Cfg.protocol_version == "2025")
    {
        LOG_INFO("HJ212 protocol version set to 2025");
        xTaskCreatePinnedToCore(Hj212_2025SendTask, "Hj2025Task", 8 * 1024, NULL, TASK_PRIORITY_HJ212, NULL, 0);
    }
    else
    {
        LOG_WARNING("Unknown HJ212 protocol version '%s', defaulting to 2017", hj212Cfg.protocol_version.c_str());
        xTaskCreatePinnedToCore(Hj212_2017SendTask, "Hj2017Task", 8 * 1024, NULL, TASK_PRIORITY_HJ212, NULL, 0);
    }
    cfg.runIfTempCon([]()
                     {
        LOG_INFO("Temperature control enabled, starting related tasks...");
        xTaskCreatePinnedToCore(TempControlTask, "TempConTask", 4 * 1024, NULL, TASK_PRIORITY_TEMP_CONTROL, NULL, 1); });
    cfg.runMqttCon([]()
                   {
        LOG_INFO("mqtt control enabled, starting mqtt tasks...");
        xTaskCreatePinnedToCore(MqttPublicTask, "MqttPublicTask", 8 * 1024, NULL, TASK_PRIORITY_MQTT, NULL, 1); });

    cfg.runAlarmCon([]()
                    {
        LOG_INFO("Alarm control enabled, starting alarm tasks...");
        xTaskCreatePinnedToCore(AlarmTask, "AlarmTask", 4 * 1024, NULL, TASK_PRIORITY_ALARM, NULL, 1); });

    xTaskCreatePinnedToCore(updateConfigTask, "updateConfigTask", 8 * 1024, NULL, TASK_PRIORITY_CONFIG, NULL, 0);
}

void System::SystemSetupInit(void)
{
    // Firmware 2.0.3: pending recovery reuses this permanent maintenance task.
    xTaskCreatePinnedToCore(updateSetupTask, "udSetTask", 6 * 1024, NULL, TASK_PRIORITY_MAINTENANCE, NULL, 1);
}

void System::SystemTaskInit(void)
{
    LOG_INFO("System Task Init Start!");

    xTaskCreatePinnedToCore(CollectTask, "CollectTask", 4 * 1024, NULL, TASK_PRIORITY_COLLECT, NULL, 1);

    xTaskCreatePinnedToCore(LedPrintTask, "LedPrintTask", 4 * 1024, NULL, TASK_PRIORITY_LED, NULL, 0);

    xTaskCreatePinnedToCore(SaveDataFileTask, "SaveFileTask", 4 * 1024, NULL, TASK_PRIORITY_STORAGE, NULL, 0);

    xTaskCreatePinnedToCore(PermissionTask, "PermissionTask", 4 * 1024, NULL, TASK_PRIORITY_PERMISSION, NULL, 1);

    xTaskCreatePinnedToCore(CollectGalTask, "CollectGalTask", 4 * 1024, NULL, TASK_PRIORITY_CALIBRATION, NULL, 0);

    RemoteOtaManager::begin(TASK_PRIORITY_OTA_LISTENER,
                            TASK_PRIORITY_OTA_ACTIVE);
}

static void updateConfigTask(void *pvParameters)
{
    auto &cfg = ConfigManager::getInstance();
    while (true)
    {
        cfg.poll();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

static void updateSetupTask(void *pvParameters)
{
    setUpInit();
    // Restore starts after the first valid CSQ update.

    volatile bool netWorkError = false;
    uint32_t lastPendingScanMs = 0;
    uint32_t lastTimeSyncAttemptMs = 0;
    uint32_t lastTimeSyncSuccessMs = 0;
    bool hasValidCsq = false;
    bool hasNetworkTimeSync = false;
    while (true)
    {
        uint32_t loopDelayMs = CSQ_POLL_INTERVAL_MS;
        {
            RemoteOtaManager::BusinessActivityGuard businessActivity;
        // Firmware 2.0.4: pending maintenance must not depend on the current
        // CSQ command succeeding. Once one valid CSQ has been observed, keep
        // the five-minute recovery clock running even when CSQ is deferred by
        // a live upload. DTUManager still arbitrates the actual serial send.
        if (hasValidCsq)
        {
            uint32_t nowMs = millis();
            if (lastPendingScanMs == 0 ||
                nowMs - lastPendingScanMs >= PENDING_SCAN_INTERVAL_MS)
            {
                fileRestore();
                lastPendingScanMs = millis();
            }
        }

        // Network time is maintenance work. Use elapsed milliseconds rather
        // than loop counts, which accelerate whenever CSQ is deferred.
        uint32_t nowMs = millis();
        bool timeSyncDue = !hasNetworkTimeSync ||
                           nowMs - lastTimeSyncSuccessMs >= TIME_SYNC_INTERVAL_MS;
        bool timeSyncRetryReady = lastTimeSyncAttemptMs == 0 ||
                                  nowMs - lastTimeSyncAttemptMs >= TIME_SYNC_RETRY_INTERVAL_MS;
        if (hasValidCsq && timeSyncDue && timeSyncRetryReady)
        {
            // Avoid applying a clock correction close to a minute boundary.
            time_t wallNow;
            time(&wallNow);
            struct tm localNow;
            localtime_r(&wallNow, &localNow);
            int waitSeconds = 0;
            if (localNow.tm_sec < TIME_SYNC_SAFE_SECOND_START)
            {
                waitSeconds = TIME_SYNC_SAFE_SECOND_START - localNow.tm_sec;
            }
            else if (localNow.tm_sec > TIME_SYNC_SAFE_SECOND_END)
            {
                waitSeconds = 60 - localNow.tm_sec + TIME_SYNC_SAFE_SECOND_START;
            }
            if (waitSeconds > 0)
            {
                LOG_DEBUG("[DIAG] TIME_SYNC_WAIT seconds=%d", waitSeconds);
                vTaskDelay(pdMS_TO_TICKS(waitSeconds * 1000));
            }

            lastTimeSyncAttemptMs = millis();
            if (synchronizeTimeFromDtu())
            {
                hasNetworkTimeSync = true;
                lastTimeSyncSuccessMs = millis();
            }
        }

        // Firmware 2.0.3: DTUManager arbitrates SERIAL_HJ212 centrally.
        // A CSQ command is skipped whenever an upload is active/waiting.
        int csq = DTUManager::getInstance().hj212DTUCSQ();
        if (csq == DTUManager::CSQ_DEFERRED)
        {
            loopDelayMs = CSQ_RETRY_INTERVAL_MS;
        }
        else
        {
            SYSTEM_SETUP newSetup = ConfigManager::getInstance().getSetup();
            const int lastValidCsq = newSetup.netCsq;
            const bool csqValid = csq >= 0 && csq <= 31;

            // Firmware 2.0.4: a failed CSQ command is not proof that the
            // network is down. Keep the last valid CSQ so live HJ212 delivery
            // is still attempted; the packet ACK remains the delivery truth.
            if (!csqValid)
            {
                netWorkError = true;
                LOG_WARNING("[DIAG] CSQ_INVALID_KEEP value=%d last=%d",
                            csq, lastValidCsq);
                loopDelayMs = CSQ_RETRY_INTERVAL_MS;
            }
            else
            {
                hasValidCsq = true;
                newSetup.netCsq = csq;
                ConfigManager::getInstance().updateSetup(newSetup);
                if (systemInfo.mutex == NULL)
                {
                    LOG_ERROR("Failed to create mutex for CSQ info");
                }
                else if (xSemaphoreTake(systemInfo.mutex, pdMS_TO_TICKS(3000)) == pdTRUE)
                {
                    systemInfo.csq = csq;
                    xSemaphoreGive(systemInfo.mutex);
                }

                if (netWorkError)
                {
                    fileRestore();
                    lastPendingScanMs = millis();
                    LOG_INFO("Network restored with CSQ: %d", csq);
                    netWorkError = false;
                }
                else
                {
                    netWorkError = false;
                    LOG_DEBUG("Updated network CSQ: %d", csq);
                    // Start maintenance after the first valid CSQ. Later scans are
                    // scheduled independently at the top of this task loop.
                    if (lastPendingScanMs == 0)
                    {
                        fileRestore();
                        lastPendingScanMs = millis();
                    }
                }

                // After the first valid CSQ, schedule the initial network-time
                // attempt promptly. Later healthy loops use the normal period.
                loopDelayMs = lastTimeSyncAttemptMs == 0
                                  ? CSQ_RETRY_INTERVAL_MS
                                  : CSQ_POLL_INTERVAL_MS;
            }
        }
        }
        vTaskDelay(pdMS_TO_TICKS(loopDelayMs));
    }
    vTaskDelete(NULL);
}

static void CollectTask(void *pvParameters)
{
    LOG_INFO("CollectTask Started");

    xTaskCreatePinnedToCore(DataProcessTask, "DataProcessTask", 4 * 1024, NULL, TASK_PRIORITY_DATA_PROCESS, NULL, 0);

    auto &collectorManager = collectorManager::getInstance();
    auto &collectMap = ConfigManager::getInstance().getCollectConfigs();

    collectorManager.begin(collectMap);

    SYSTEMCONFIG sysCfg = ConfigManager::getInstance().getSystem();
    int collectTime = sysCfg.collect_time > 0 ? sysCfg.collect_time : 60; // 默认 60s 采集时间

    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = pdMS_TO_TICKS(collectTime * 1000);

    pinMode(PUMP1_PIN, OUTPUT);
    while (true)
    {
        {
            RemoteOtaManager::BusinessActivityGuard businessActivity;
            digitalWrite(PUMP1_PIN, HIGH);
            vTaskDelay(pdMS_TO_TICKS(collectTime * 1000 / 3));
            collectorManager.poll();
            SerialManager::getInstance().checkAndReportOverflow(SERIAL_485);
            digitalWrite(PUMP1_PIN, LOW);
        }
        // vTaskDelayUntil advances xLastWakeTime itself. Do not reset it to
        // the delayed actual wake tick, otherwise scheduling latency accumulates.
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }

    vTaskDelete(NULL);
}

static void DataProcessTask(void *pvParameters)
{
    auto &dataProcessManager = DataManager::getInstance();
    dataProcessManager.begin();
    EventMsg msg;

    while (true)
    {
        dataProcessManager.poll();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

static void TempControlTask(void *pvParameters)
{
    TEMPCONTROLCONFIG tempCon = ConfigManager::getInstance().getTempCon();
    auto &tempManager = TempManager::getInstance();
    tempManager.begin();
    tempManager.setTargetTemp(tempCon.tempUpperLimit, tempCon.tempLowerLimit);
    tempManager.setTargetHumi(tempCon.wetnUpperLimit, tempCon.wetnLowerLimit);

    while (true)
    {
        {
            RemoteOtaManager::BusinessActivityGuard businessActivity;
            tempManager.poll();
        }
        vTaskDelay(pdMS_TO_TICKS(30000));
    }
    vTaskDelete(NULL);
}

// 发送实时数�?/ 小时数据 / 天数�?
static void Hj212_2017SendTask(void *pvParameters)
{
    LOG_INFO("Hj212_2017SendTask Started");
    QueueHandle_t Hj212SendTaskQueue = EventBus::getInstance().createReceiverQueue(20, "HJ212_2017");
    EventBus::getInstance().subscribe(EventID::PROCESSED_DATA_COLLECTED, Hj212SendTaskQueue);
    EventBus::getInstance().subscribe(EventID::RESUME_DATA, Hj212SendTaskQueue);

    HJ212_DataCenter HJ212;
    const auto &config = ConfigManager::getInstance().getHJ212();
    auto &filesys = filesysManager::getInstance();
    EventMsg msg;
    uint16_t count = 0;
    int deviceCsq = 99;
    while (true)
    {
        if (EventBus::getInstance().waitEvent(Hj212SendTaskQueue, msg, portMAX_DELAY))
        {
            RemoteOtaManager::BusinessActivityGuard businessActivity;
            if (msg.id == EventID::PROCESSED_DATA_COLLECTED)
            {
                AllProcessedDataPacket *allData = static_cast<AllProcessedDataPacket *>(msg.data);
                if (allData == nullptr)
                {
                    LOG_ERROR("Received null AllProcessedDataPacket pointer!");
                    continue;
                }
                count++;
                LOG_DEBUG("WAIT event AllProcessedDataPacket Count: %d", count);
                SYSTEM_SETUP newSetup = ConfigManager::getInstance().getSetup();
                deviceCsq = newSetup.netCsq;
                LOG_DEBUG("******** Current network CSQ: %d **********", deviceCsq);
                if (deviceCsq <= 31 && deviceCsq >= 0)
                {
                    String HJ212_str = HJ212.build2017Hj212Packet(allData, config);
                    if (HJ212_str.length() > 0)
                    {
                        LOG_DEBUG("Generated HJ212 Packet %d bytes", HJ212_str.length());
                        bool result = DTUManager::getInstance().sendHJ212Packet(
                            HJ212_str, 3, allData->trace_id,
                            (int)allData->dataTime, allData->last_update);
                        if (!result)
                        {
                            LOG_WARNING("Failed to send HJ212 packet, saving for retry...");
                            filesys.savePendingPacket(allData, HJ212_str);
                        }
                    }
                    else
                    {
                        if (allData->last_update > 20260527000000)
                        {
                            LOG_WARNING("HJ212 Packet construction failed or empty.");
                            filesys.savePendingRebuildMarker(allData);
                        }
                    }
                }
                else
                {
                    if (allData->last_update > 20260527000000)
                    {
                        String HJ212_str = HJ212.build2017Hj212Packet(allData, config);
                        LOG_WARNING("Network unavailable, saving HJ212 packet for retry.");
                        if (HJ212_str.length() > 0)
                        {
                            filesys.savePendingPacket(allData, HJ212_str);
                        }
                        else
                        {
                            filesys.savePendingRebuildMarker(allData);
                        }
                    }
                }
                if (allData != nullptr)
                {   
                    allData->release();
                }
            }
            else if (msg.id == EventID::RESUME_DATA)
            {
                AllProcessedDataPacket *allData = static_cast<AllProcessedDataPacket *>(msg.data);
                if (allData == nullptr)
                {
                    LOG_ERROR("Received null AllProcessedDataPacket pointer!");
                    continue;
                }
                String HJ212_str = HJ212.build2017Hj212Packet(allData, config);
                if (HJ212_str.length() > 0)
                {
                    LOG_DEBUG("Generated HJ212 Packet %d bytes", HJ212_str.length());
                    bool result = DTUManager::getInstance().sendHJ212Packet(
                        HJ212_str, 3, allData->trace_id,
                        (int)allData->dataTime, allData->last_update);
                    if (!result)
                    {
                        LOG_WARNING("Failed to send HJ212 packet, saving for retry...");
                        filesys.savePendingPacket(allData, HJ212_str);
                    }
                }
                if (allData != nullptr)
                {
                    allData->release();
                }
            }
        }
        // Firmware 2.0.1 (2026-07-30): avoid a 15-second backlog at
        // 10-minute and hour boundaries while retaining a small packet gap.
        vTaskDelay(pdMS_TO_TICKS(HJ212_PACKET_GAP_MS));
    }
    vTaskDelete(NULL);
}

static void Hj212_2025SendTask(void *pvParameters)
{
    LOG_INFO("Hj212_2017SendTask Started");
    QueueHandle_t Hj212SendTaskQueue = EventBus::getInstance().createReceiverQueue(20, "HJ212_2025");
    EventBus::getInstance().subscribe(EventID::PROCESSED_DATA_COLLECTED, Hj212SendTaskQueue);
    EventBus::getInstance().subscribe(EventID::RESUME_DATA, Hj212SendTaskQueue);

    HJ212_DataCenter HJ212;
    const auto &config = ConfigManager::getInstance().getHJ212();
    auto &filesys = filesysManager::getInstance();
    EventMsg msg;
    uint16_t count = 0;
    int deviceCsq = 99;
    while (true)
    {
        if (EventBus::getInstance().waitEvent(Hj212SendTaskQueue, msg, portMAX_DELAY))
        {
            RemoteOtaManager::BusinessActivityGuard businessActivity;
            if (msg.id == EventID::PROCESSED_DATA_COLLECTED)
            {
                AllProcessedDataPacket *allData = static_cast<AllProcessedDataPacket *>(msg.data);
                if (allData == nullptr)
                {
                    LOG_ERROR("Received null AllProcessedDataPacket pointer!");
                    continue;
                }
                count++;
                LOG_DEBUG("WAIT event AllProcessedDataPacket Count: %d", count);
                SYSTEM_SETUP newSetup = ConfigManager::getInstance().getSetup();
                deviceCsq = newSetup.netCsq;
                LOG_DEBUG("******** Current network CSQ: %d **********", deviceCsq);
                if (deviceCsq <= 31 && deviceCsq >= 0)
                {
                    String HJ212_str = HJ212.build2025Hj212Packet(allData, config);
                    if (HJ212_str.length() > 0)
                    {
                        LOG_DEBUG("Generated HJ212 Packet %d bytes", HJ212_str.length());
                        bool result = DTUManager::getInstance().sendHJ212Packet(
                            HJ212_str, 3, allData->trace_id,
                            (int)allData->dataTime, allData->last_update);
                        if (!result)
                        {
                            LOG_WARNING("Failed to send HJ212 packet, saving for retry...");
                            filesys.savePendingPacket(allData, HJ212_str);
                        }
                    }
                    else
                    {
                        if (allData->last_update > 20260527000000)
                        {
                            LOG_WARNING("HJ212 Packet construction failed or empty.");
                            filesys.savePendingRebuildMarker(allData);
                        }
                    }
                }
                else
                {
                    if (allData->last_update > 20260527000000)
                    {
                        String HJ212_str = HJ212.build2025Hj212Packet(allData, config);
                        LOG_WARNING("Network unavailable, saving HJ212 packet for retry.");
                        if (HJ212_str.length() > 0)
                        {
                            filesys.savePendingPacket(allData, HJ212_str);
                        }
                        else
                        {
                            filesys.savePendingRebuildMarker(allData);
                        }
                    }
                }
                if (allData != nullptr)
                {
                    allData->release();
                }
            }
            else if (msg.id == EventID::RESUME_DATA)
            {
                AllProcessedDataPacket *allData = static_cast<AllProcessedDataPacket *>(msg.data);
                if (allData == nullptr)
                {
                    LOG_ERROR("Received null AllProcessedDataPacket pointer!");
                    continue;
                }
                String HJ212_str = HJ212.build2025Hj212Packet(allData, config);
                if (HJ212_str.length() > 0)
                {
                    LOG_DEBUG("Generated HJ212 Packet %d bytes", HJ212_str.length());
                    bool result = DTUManager::getInstance().sendHJ212Packet(
                        HJ212_str, 3, allData->trace_id,
                        (int)allData->dataTime, allData->last_update);
                    if (!result)
                    {
                        LOG_WARNING("Failed to send HJ212 packet, saving for retry...");
                        filesys.savePendingPacket(allData, HJ212_str);
                    }
                }
                if (allData != nullptr)
                {   
                    allData->release();
                }
            }
        }
        // Firmware 2.0.1 (2026-07-30): match the 2017 sender pacing.
        vTaskDelay(pdMS_TO_TICKS(HJ212_PACKET_GAP_MS));
    }
    vTaskDelete(NULL);
}

static bool sendHJ212PacketLocked(const String &packet, int maxRetry)
{
    if (packet.length() == 0)
    {
        return false;
    }
    // Firmware 2.0.3: DTUManager is the single SERIAL_HJ212 lock owner.
    return DTUManager::getInstance().sendHJ212Packet(packet, maxRetry);
}

static void recoverPendingPacket(const PendingPacketInfo &pending)
{
    HJ212_DataCenter HJ212;
    const HJ212CONFIG config = ConfigManager::getInstance().getHJ212();
    auto &filesys = filesysManager::getInstance();

    LOG_INFO("Resending one pending packet: type=%d, timestamp=%llu",
             (int)pending.dataTime, pending.timestamp);

    String packet = filesys.loadPendingPacketContent(pending);
    bool storedPacketInvalid = false;
    if (packet.length() > 0 &&
        !HJ212_DataCenter::isValidPacket(packet))
    {
        storedPacketInvalid = true;
        LOG_WARNING("[DIAG] PENDING_INVALID type=%d timestamp=%llu bytes=%u path=%s",
                    (int)pending.dataTime, pending.timestamp,
                    (unsigned)packet.length(), pending.filePath.c_str());
        packet = "";
    }
    if (packet.length() == 0)
    {
        AllProcessedDataPacket *legacyData =
            filesys.readPendingPacket((int)pending.dataTime, pending.timestamp);
        if (legacyData != nullptr)
        {
            packet = config.protocol_version == "2025"
                ? HJ212.build2025Hj212Packet(legacyData, config)
                : HJ212.build2017Hj212Packet(legacyData, config);
            legacyData->release();
        }
    }

    if (packet.length() == 0 ||
        !HJ212_DataCenter::isValidPacket(packet))
    {
        // A rebuild can fail because heap is temporarily tight. Keep the
        // marker for the next maintenance cycle instead of quarantining it.
        LOG_WARNING("[DIAG] RECOVERY_RETAIN reason=rebuild_unavailable path=%s free=%u largest=%u",
                    pending.filePath.c_str(), ESP.getFreeHeap(), ESP.getMaxAllocHeap());
        return;
    }
    if (storedPacketInvalid)
    {
        LOG_INFO("[DIAG] PENDING_REBUILT type=%d timestamp=%llu bytes=%u",
                 (int)pending.dataTime, pending.timestamp,
                 (unsigned)packet.length());
    }

    bool delivered = sendHJ212PacketLocked(packet, 1);

    if (delivered)
    {
        filesys.deletePendingPacket(pending);
    }
    else
    {
        LOG_WARNING("Pending delivery failed, marker retained: %s",
                    pending.filePath.c_str());
    }
}

static void LedPrintTask(void *pvParameters)
{
    LOG_INFO("Serial 485 LED task started");

    auto &ledManager = LedManager::getInstance();
    ledManager.begin();

    QueueHandle_t LedPrintTaskQueue = EventBus::getInstance().createReceiverQueue(5, "LED");
    EventBus::getInstance().subscribe(EventID::PROCESSED_DATA_COLLECTED, LedPrintTaskQueue);
    SYSTEMCONFIG sysCfg = ConfigManager::getInstance().getSystem();
    int collectTime = sysCfg.collect_time > 0 ? sysCfg.collect_time : 60; // 默认 60s 采集时间

    COLLECTMAP collectMap = ConfigManager::getInstance().getCollectConfigs();

    EventMsg msg;
    while (true)
    {
        if (EventBus::waitEvent(LedPrintTaskQueue, msg))
        {
            RemoteOtaManager::BusinessActivityGuard businessActivity;
            if (msg.id == EventID::PROCESSED_DATA_COLLECTED)
            {
                AllProcessedDataPacket *allData = static_cast<AllProcessedDataPacket *>(msg.data);
                uint32_t coalescedCount = 0;
                EventMsg queuedMsg;
                while (EventBus::getInstance().waitEvent(
                    LedPrintTaskQueue, queuedMsg, 0))
                {
                    if (queuedMsg.id != EventID::PROCESSED_DATA_COLLECTED)
                    {
                        continue;
                    }
                    AllProcessedDataPacket *queuedData =
                        static_cast<AllProcessedDataPacket *>(queuedMsg.data);
                    if (queuedData == nullptr)
                    {
                        continue;
                    }
                    if (queuedData->dataTime == DataTime::REAL_DATA)
                    {
                        if (allData != nullptr)
                        {
                            allData->release();
                        }
                        allData = queuedData;
                    }
                    else
                    {
                        queuedData->release();
                    }
                    coalescedCount++;
                }
                if (coalescedCount > 0)
                {
                    LOG_INFO("[DIAG] LED_COALESCE drained=%u latest_timestamp=%llu",
                             (unsigned)coalescedCount,
                             allData != nullptr ? allData->last_update : 0ULL);
                }
                if (allData != nullptr)
                {
                    // Firmware 2.0.4: the LED cycles through one real-time
                    // snapshot for the full collection interval. Minute,
                    // hour and day packets must not consume another full
                    // display cycle or remain queued and retain heap.
                    if (allData->dataTime == DataTime::REAL_DATA)
                    {
                        ledManager.updateDisplay(allData, collectTime, collectMap);
                    }
                    else
                    {
                        LOG_DEBUG("[DIAG] LED_SKIP_NONREAL type=%d timestamp=%llu",
                                  (int)allData->dataTime, allData->last_update);
                    }
                    allData->release();
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    vTaskDelete(NULL);
}
static void SaveDataFileTask(void *pvParameters)
{
    LOG_DEBUG("SaveDataFileTask Started");
    auto &filesys = filesysManager::getInstance();

    EventMsg msg;
    while (true)
    {
        filesys.poll();
    }
    vTaskDelete(NULL);
}

static void PermissionTask(void *pvParameters)
{
    auto &permission = PermissionSystem::getInstance();
    permission.begin(SERIAL_LCD, SERIAL_DTU);

    xTaskCreatePinnedToCore(SerialControlTask_lcd, "Controllcd", 4 * 1024, NULL, TASK_PRIORITY_SERIAL_CONTROL, NULL, 1);
    xTaskCreatePinnedToCore(SerialControlTask_dtu, "Controldtu", 4 * 1024, NULL, TASK_PRIORITY_SERIAL_CONTROL, NULL, 1);

    while (true)
    {
        {
            RemoteOtaManager::BusinessActivityGuard businessActivity;
            permission.poll();
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    vTaskDelete(NULL);
}
static void SerialControlTask_lcd(void *pvParameters)
{
    auto &permission = PermissionSystem::getInstance();

    Stream *_lcdStream = SerialManager::getInstance().getStream(SERIAL_LCD);
    String test = "";
    test.reserve(2048);
    bool isReceiving = false;
    int bracketLevel = 0;
    unsigned long lastByteTime = 0;

    while (true)
    {
        RemoteOtaManager::BusinessActivityGuard businessActivity;
        while (_lcdStream && _lcdStream->available() > 0)
        {
            char c = _lcdStream->read();
            lastByteTime = millis();

            if (!isReceiving)
            {
                if (c == '{')
                {
                    isReceiving = true;
                    test = "{";
                    bracketLevel = 1;
                }
                continue;
            }
            test += c;
            if (c == '{')
                bracketLevel++;
            else if (c == '}')
                bracketLevel--;
            if (isReceiving && bracketLevel == 0)
            {
                LOG_DEBUG("Received TRUE Full JSON: %s", test.c_str());
                permission.processLine(test, _lcdStream);
                isReceiving = false;
                test = "";
                break;
            }

            if (test.length() > 2000)
            {
                isReceiving = false;
                test = "";
                bracketLevel = 0;
                break;
            }
        }
        if (isReceiving && (millis() - lastByteTime > 500))
        {
            LOG_WARNING("JSON reception timeout, resetting...");
            isReceiving = false;
            test = "";
            bracketLevel = 0;
        }
        SerialManager::getInstance().checkAndReportOverflow(SERIAL_LCD);
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    vTaskDelete(NULL);
}
static void SerialControlTask_dtu(void *pvParameters)
{
    auto &permission = PermissionSystem::getInstance();

    Stream *_dtuStream = SerialManager::getInstance().getStream(SERIAL_DTU);
    String test = "";
    test.reserve(2048);
    bool isReceiving = false;
    int bracketLevel = 0;
    unsigned long lastByteTime = 0;
    while (true)
    {
        RemoteOtaManager::BusinessActivityGuard businessActivity;
        while (_dtuStream && _dtuStream->available() > 0)
        {
            char c = _dtuStream->read();
            lastByteTime = millis();

            if (!isReceiving)
            {
                if (c == '{')
                {
                    isReceiving = true;
                    test = "{";
                    bracketLevel = 1;
                }
                continue;
            }
            test += c;
            if (c == '{')
                bracketLevel++;
            else if (c == '}')
                bracketLevel--;
            if (isReceiving && bracketLevel == 0)
            {
                LOG_DEBUG("Received TRUE Full JSON: %s", test.c_str());
                permission.processLine(test, _dtuStream);
                isReceiving = false;
                test = "";
                break;
            }

            if (test.length() > 2000)
            {
                isReceiving = false;
                test = "";
                bracketLevel = 0;
                break;
            }
        }
        if (isReceiving && (millis() - lastByteTime > 500))
        {
            LOG_WARNING("JSON reception timeout, resetting...");
            isReceiving = false;
            test = "";
            bracketLevel = 0;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    vTaskDelete(NULL);
}

static void CollectGalTask(void *pvParameters)
{
    auto &collectorManager = collectorManager::getInstance();
    while (true)
    {
        collectorManager.galpoll();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    vTaskDelete(NULL);
}

static void MqttPublicTask(void *pvParameters)
{
    while (true)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    vTaskDelete(NULL);
}
static void AlarmTask(void *pvParameters){
    LOG_INFO("AlarmTask started");

    QueueHandle_t AlarmTaskQueue = EventBus::getInstance().createReceiverQueue(5, "ALARM_TASK");
    EventBus::getInstance().subscribe(EventID::PROCESSED_DATA_COLLECTED, AlarmTaskQueue);

    EventMsg msg;
    const auto &alarmConfig = ConfigManager::getInstance().getAlarmConfig();
    float tempUpperLimit = alarmConfig.alarm_upper_limit;
    float tempLowerLimit = alarmConfig.alarm_lower_limit;
    String alarm_sensor = alarmConfig.alarm_sensor;
    pinMode(ALARM_PIN, OUTPUT);\

    volatile int alarmCount = 0;
    volatile int noAlarmCount = 0;

    while (true)
    {
        if (EventBus::waitEvent(AlarmTaskQueue, msg))
        {
            RemoteOtaManager::BusinessActivityGuard businessActivity;
            if (msg.id == EventID::PROCESSED_DATA_COLLECTED)
            {
                AllProcessedDataPacket *allData = static_cast<AllProcessedDataPacket *>(msg.data);
                
                if (allData != nullptr)
                {
                    for (const auto& data : allData->processed_data_map)
                    {
                        if (data.first == alarm_sensor)
                        {
                            float tempValue = data.second.value;
                            if (tempValue > tempUpperLimit || tempValue < tempLowerLimit)
                            {
                                alarmCount++;
                                noAlarmCount = 0;
                                LOG_DEBUG("Temperature alarm! Value: %.2f", tempValue);
                                if (alarmCount >= 3){
                                    digitalWrite(ALARM_PIN, HIGH);
                                    alarmCount = 3;
                                }
                            } else {
                                noAlarmCount++;
                                alarmCount = 0;
                                if (noAlarmCount >= 3){
                                    digitalWrite(ALARM_PIN, LOW);
                                    noAlarmCount = 3;
                                }
                            }
                        }
                    }
                    allData->release();
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    vTaskDelete(NULL);
}

// ******************* 时间函数实现 *******************
// 202605201040
// 202605010000
void timeInit(uint64_t timestamp)
{
    rtc.init();
    if (isValidClockTime(timestamp))
    {
        Ds1302::DateTime dt;
        uint64_t temp = timestamp;
        // DTU time is normally YYYYMMDDhhmmss. Keep compatibility with the
        // legacy 12-digit value while preserving seconds whenever available.
        if (timestamp >= 10000000000000ULL)
        {
            dt.second = temp % 100;
            temp /= 100;
        }
        else
        {
            dt.second = 0;
        }
        dt.minute = temp % 100;
        temp /= 100;
        dt.hour = temp % 100;
        temp /= 100;
        dt.day = temp % 100;
        temp /= 100;
        dt.month = temp % 100;
        temp /= 100;
        dt.year = (uint8_t)(temp % 100);
        dt.dow = 3;
        rtc.setDateTime(&dt);
        Serial.println("RTC Init sucessfully with network time!");
        return;
    }
    return;
}
uint64_t getCurrentTime()
{
    Ds1302::DateTime now;
    rtc.getDateTime(&now);
    char buffer[20];
    snprintf(buffer, sizeof(buffer), "%04d%02d%02d%02d%02d%02d",
             now.year + 2000, now.month, now.day,
             now.hour, now.minute, now.second);
    return strtoull(buffer, nullptr, 10);
}
// 202605220939
bool updateMillisTime(uint64_t newTime)
{
    if (!isValidClockTime(newTime))
    {
        LOG_ERROR("Rejected invalid clock time: %llu", newTime);
        return false;
    }

    int year, month, day, hour, minute, second = 0;
    int parsed = sscanf(String(newTime).c_str(), "%4d%2d%2d%2d%2d%2d",
                        &year, &month, &day, &hour, &minute, &second);
    if (parsed == 5 || parsed == 6)
    {
        struct tm timeinfo = {};
        timeinfo.tm_year = year - 1900; // �?900年起的年�?
        timeinfo.tm_mon = month - 1;    // 0-11�?
        timeinfo.tm_mday = day;
        timeinfo.tm_hour = hour;
        timeinfo.tm_min = minute;
        timeinfo.tm_sec = 0;    // 格式中无秒，默认�?
        timeinfo.tm_isdst = -1; // 自动判断夏令�?

        // Override the legacy zero-second default when a 14-digit time is supplied.
        timeinfo.tm_sec = second;
        time_t t = mktime(&timeinfo);
        if (t != -1)
        {
            struct timeval tv;
            tv.tv_sec = t;
            tv.tv_usec = 0;
            settimeofday(&tv, nullptr);

            // 4. 验证设置结果
            time_t now;
            time(&now);
            struct tm *p_tm = localtime(&now);
            char timeBuf[32];
            strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", p_tm);
            LOG_INFO("Time Calibrated: %s", timeBuf);
            return true;
        }
        else
        {
            LOG_ERROR("Failed to parse time from newTime: %llu", newTime);
            return false;
        }
    }
    return false;
}

// ******************* 函数实现 *******************
static bool clockTimestampToEpoch(uint64_t timestamp, time_t &epoch)
{
    if (!isValidClockTime(timestamp)) return false;

    int year, month, day, hour, minute, second = 0;
    char value[20];
    snprintf(value, sizeof(value), "%llu", timestamp);
    int parsed = sscanf(value, "%4d%2d%2d%2d%2d%2d",
                        &year, &month, &day, &hour, &minute, &second);
    if (parsed != 5 && parsed != 6) return false;

    struct tm timeinfo = {};
    timeinfo.tm_year = year - 1900;
    timeinfo.tm_mon = month - 1;
    timeinfo.tm_mday = day;
    timeinfo.tm_hour = hour;
    timeinfo.tm_min = minute;
    timeinfo.tm_sec = second;
    timeinfo.tm_isdst = -1;
    epoch = mktime(&timeinfo);
    return epoch != (time_t)-1;
}

static bool synchronizeTimeFromDtu()
{
    uint64_t networkTime = DTUManager::getInstance().hjSystemTime();
    time_t networkEpoch;
    if (!clockTimestampToEpoch(networkTime, networkEpoch))
    {
        if (networkTime != 0)
        {
            LOG_WARNING("[DIAG] TIME_SYNC_INVALID timestamp=%llu", networkTime);
        }
        return false;
    }

    time_t systemEpoch;
    time(&systemEpoch);
    int64_t deltaSeconds = (int64_t)difftime(networkEpoch, systemEpoch);
    int64_t absoluteDelta = deltaSeconds < 0 ? -deltaSeconds : deltaSeconds;
    if (absoluteDelta <= TIME_SYNC_APPLY_THRESHOLD_SEC)
    {
        LOG_INFO("[DIAG] TIME_SYNC_VERIFIED timestamp=%llu delta_sec=%lld applied=0",
                 networkTime, (long long)deltaSeconds);
        return true;
    }

    timeInit(networkTime);
    if (!updateMillisTime(networkTime))
    {
        LOG_ERROR("[DIAG] TIME_SYNC_APPLY_FAIL timestamp=%llu delta_sec=%lld",
                  networkTime, (long long)deltaSeconds);
        return false;
    }
    LOG_INFO("[DIAG] TIME_SYNC_APPLIED timestamp=%llu delta_sec=%lld",
             networkTime, (long long)deltaSeconds);
    return true;
}

void setUpInit(void)
{
    LOG_INFO("System setup init start!");
    auto &sm = SerialManager::getInstance();
    Stream *HJ212_port = sm.getStream(SERIAL_HJ212);
    Stream *DTU_port = sm.getStream(SERIAL_DTU);
    auto &DTUMg = DTUManager::getInstance();
    DTUMg.init(*DTU_port, *HJ212_port);

    // Start from RTC immediately so collection never waits for DTU command
    // retries. Network time is synchronized later after the first valid CSQ.
    rtc.init();
    uint64_t rtcTime = getCurrentTime();
    if (!updateMillisTime(rtcTime))
    {
        LOG_ERROR("RTC startup time invalid; waiting for network synchronization");
    }
    else
    {
        LOG_INFO("[DIAG] TIME_BOOT_RTC timestamp=%llu", rtcTime);
    }

    HJ212CONFIG hj212Cfg = ConfigManager::getInstance().getHJ212();
    SYSTEMCONFIG systemCfg = ConfigManager::getInstance().getSystem();
    if (!DTUMg.updateHJDtuGoalIP(hj212Cfg.ip))
    {
        LOG_ERROR("Failed to update HJ212 DTU IP");
    }
}
void fileRestore(void)
{
    bool expected = false;
    if (!networkRestoreRunning.compare_exchange_strong(expected, true))
    {
        LOG_DEBUG("Pending packet recovery task is already running");
        return;
    }

    auto &filesys = filesysManager::getInstance();
    LOG_DEBUG("Scanning pending packets, free heap=%u, largest block=%u",
              ESP.getFreeHeap(), ESP.getMaxAllocHeap());
    constexpr size_t RECOVERY_BATCH_SIZE = 1;
    std::vector<PendingPacketInfo> pendingPackets =
        filesys.scanPendingPackets(RECOVERY_BATCH_SIZE);
    if (pendingPackets.empty())
    {
        networkRestoreRunning.store(false);
        return;
    }

    // Firmware 2.0.3: recover one marker in the existing maintenance task.
    // No temporary ResumeData object or 5 KiB FreeRTOS task is allocated.
    recoverPendingPacket(pendingPackets.front());
    networkRestoreRunning.store(false);
    LOG_INFO("[DIAG] RECOVERY_DONE free=%u largest=%u stack_high_water=%u",
             ESP.getFreeHeap(), ESP.getMaxAllocHeap(),
             (unsigned)uxTaskGetStackHighWaterMark(NULL));
}

static bool isValidClockTime(uint64_t timestamp)
{
    int year, month, day, hour, minute, second = 0;
    char value[20];
    snprintf(value, sizeof(value), "%llu", timestamp);
    size_t length = strlen(value);
    int parsed = sscanf(value, "%4d%2d%2d%2d%2d%2d",
                        &year, &month, &day, &hour, &minute, &second);
    if ((length != 12 && length != 14) ||
        (parsed != 5 && parsed != 6))
    {
        return false;
    }
    return year >= 2020 && year <= 2099 &&
           month >= 1 && month <= 12 &&
           day >= 1 && day <= 31 &&
           hour >= 0 && hour <= 23 &&
           minute >= 0 && minute <= 59 &&
           second >= 0 && second <= 59;
}
