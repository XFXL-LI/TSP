#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <Update.h>
#include <esp_ota_ops.h>
#include "esp_task_wdt.h"
#include <esp_https_ota.h>
#include <SoftwareSerial.h>
#include <time.h>
#include <stdio.h>
#include <vector>
#include "system.h"
#include "src/module/log/log_manager.h"
#include "../../inc/sys_init.h"
#include "../event/eventBus.h"
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

#define PUMP1_PIN 14
#define PUMP2_PIN 40

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
// ********** 时间相关定义 **********

// ********** 其他全局定义 **********
void setUpInit(void);
void fileRestore(void);
void otaUpload(int otaSize);
//

CSQINFO csqInfo;
struct ResumeData
{
    std::vector<uint64_t> packets;
};

// 采集
static void CollectTask(void *pvParameters); // 采集后第一轮判断是否报警?
// HJ212 打包
static void Hj212_2017SendTask(void *pvParameters);
static void Hj212_2025SendTask(void *pvParameters);
// 断点续传
static void netWorkRestoreTask(void *pvParameters);
// 保存数据
static void SaveDataFileTask(void *pvParameters);
// OTA 升级
static void OtaUploadTask(void *pvParameters);
// LED
static void LedPrintTask(void *pvParameters);
// 串口解析及管理权限
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
// mqtt 订阅发送
static void MqttPublicTask(void *pvParameters);

System::System()
{
}
System::~System()
{
}
void System::SystemInit(void)
{

    LogManager::getInstance().setLevel(LOG_LEVEL_DEBUG);
    LOG_DEBUG("System init start");

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
        xTaskCreatePinnedToCore(Hj212_2017SendTask, "Hj2017Task", 8 * 1024, NULL, 5, NULL, 0);
    }
    else if (hj212Cfg.protocol_version == "2025")
    {
        LOG_INFO("HJ212 protocol version set to 2025");
        xTaskCreatePinnedToCore(Hj212_2025SendTask, "Hj2025Task", 8 * 1024, NULL, 5, NULL, 0);
    }
    else
    {
        LOG_WARNING("Unknown HJ212 protocol version '%s', defaulting to 2017", hj212Cfg.protocol_version.c_str());
        xTaskCreatePinnedToCore(Hj212_2017SendTask, "Hj2017Task", 8 * 1024, NULL, 5, NULL, 0);
    }
    cfg.runIfTempCon([]()
                     {
        LOG_INFO("Temperature control enabled, starting related tasks...");
        xTaskCreatePinnedToCore(TempControlTask, "TempConTask", 4 * 1024, NULL, 5, NULL, 1); });
    cfg.runMqttCon([]()
                   {
        LOG_INFO("mqtt control enabled, starting mqtt tasks...");
        xTaskCreatePinnedToCore(MqttPublicTask, "MqttPublicTask", 8 * 1024, NULL, 5, NULL, 1); });
    xTaskCreatePinnedToCore(updateConfigTask, "updateConfigTask", 8 * 1024, NULL, 5, NULL, 0);
}

void System::SystemSetupInit(void)
{
    xTaskCreatePinnedToCore(updateSetupTask, "udSetTask", 4 * 1024, NULL, 5, NULL, 1);
}

void System::SystemTaskInit(void)
{
    LOG_INFO("System Task Init Start!");

    xTaskCreatePinnedToCore(CollectTask, "CollectTask", 4 * 1024, NULL, 6, NULL, 1);

    xTaskCreatePinnedToCore(LedPrintTask, "LedPrintTask", 4 * 1024, NULL, 5, NULL, 0);

    xTaskCreatePinnedToCore(SaveDataFileTask, "SaveFileTask", 4 * 1024, NULL, 5, NULL, 0);

    xTaskCreatePinnedToCore(PermissionTask, "PermissionTask", 4 * 1024, NULL, 5, NULL, 1);

    xTaskCreatePinnedToCore(CollectGalTask, "CollectGalTask", 4 * 1024, NULL, 5, NULL, 0);

    xTaskCreatePinnedToCore(OtaUploadTask, "OtaUploadTask", 8 * 10240, NULL, 11, NULL, 1);
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
    // ========== 启动时恢复待补传数据 ==========
    LOG_INFO("Scanning for pending packets from previous session...");
    fileRestore();
    // ========== 恢复逻辑结束 ==========

    csqInfo.mutex = xSemaphoreCreateMutex();
    volatile bool netWorkError = false;
    int countTime = 0;
    while (true)
    {
        if (countTime >= 120)
        {
            uint64_t currentTime = getCurrentTime();
            if (currentTime < 202605261200) {
                vTaskDelay(pdMS_TO_TICKS(3000));
                currentTime = getCurrentTime();
            }
            if (!updateMillisTime(currentTime))
            {
                LOG_ERROR("Failed to update milliseconds time");
            }
            LOG_DEBUG("Current Time: %llu", currentTime);
            countTime = 0;
        }
        else
        {
            countTime++;
        }

        SemaphoreHandle_t _StreamMutex = SerialManager::getInstance().getMutex(SERIAL_HJ212);
        if (xSemaphoreTake(_StreamMutex, pdMS_TO_TICKS(3000)) == pdTRUE)
        {
            int csq = DTUManager::getInstance().hj212DTUCSQ();
            if (csq == 99)
            {
                vTaskDelay(pdMS_TO_TICKS(3000));
                csq = DTUManager::getInstance().hj212DTUCSQ();
            }
            if (csq == 99)
            {
                vTaskDelay(pdMS_TO_TICKS(3000));
                csq = DTUManager::getInstance().hj212DTUCSQ();
            }
            xSemaphoreGive(_StreamMutex);
            SYSTEM_SETUP newSetup = ConfigManager::getInstance().getSetup();
            newSetup.netCsq = csq;
            ConfigManager::getInstance().updateSetup(newSetup);
            if (csqInfo.mutex == NULL)
            {
                LOG_ERROR("Failed to create mutex for CSQ info");
            }
            else if (xSemaphoreTake(csqInfo.mutex, pdMS_TO_TICKS(3000)) == pdTRUE)
            {
                csqInfo.csq = csq;
            }

            if (netWorkError && csq >= 0 && csq <= 31)
            {
                fileRestore();
                LOG_INFO("Network restored with CSQ: %d", csq);
                netWorkError = false;
            }
            else if (csq >= 0 && csq <= 31)
            {
                netWorkError = false;
                LOG_DEBUG("Updated network CSQ: %d", csq);
            }
            else
            {
                netWorkError = true;
                LOG_ERROR("Invalid CSQ value: %d, skipping update.", csq);
            }
        }
        else
        {
            LOG_ERROR("Failed to acquire mutex for HJ212 stream to update CSQ");
        }
        vTaskDelay(pdMS_TO_TICKS(60000));
    }
    vTaskDelete(NULL);
}
static void OtaUploadTask(void *pvParameters)
{
    LOG_DEBUG("OtaUploadTask Started");
    QueueHandle_t OtaUploadTaskQueue = EventBus::getInstance().createReceiverQueue(5);
    EventBus::getInstance().subscribe(EventID::UPLOAD_REQ, OtaUploadTaskQueue);

    EventMsg msg;
    while (true)
    {
        if (EventBus::waitEvent(OtaUploadTaskQueue, msg))
        {
            if (msg.id == EventID::UPLOAD_REQ)
            {
                JSONCmdData *allData = (JSONCmdData *)msg.data;

                if (allData != nullptr)
                {
                    LOG_DEBUG("upload request: %s", allData->arguments.c_str());

                    config_json uploadJson(allData->arguments.c_str());
                    int otaSize = uploadJson.isValid() ? uploadJson.getInt("size", 0) : 0;

                    int subscriberCount = EventBus::getInstance().getSubscriberCount(EventID::UPLOAD_RES);
                    if (subscriberCount > 0)
                    {
                        allData->retain();
                    }
                    EventBus::getInstance().publish(EventID::UPLOAD_RES, allData);
                    allData->release();

                    if (otaSize > 0)
                    {
                        LOG_INFO("Starting OTA upload with size: %d", otaSize);
                        TaskHandle_t CollectTaskHandle = xTaskGetHandle("CollectTask");
                        TaskHandle_t Hj212SendHandle = xTaskGetHandle("Hj2017Task");
                        TaskHandle_t SaveDataFileHandle = xTaskGetHandle("SaveFileTask");
                        TaskHandle_t LedPrintHandle = xTaskGetHandle("LedPrintTask");
                        TaskHandle_t LcdControlHandle = xTaskGetHandle("Controllcd");
                        TaskHandle_t ControldtuHandle = xTaskGetHandle("Controldtu");
                        TaskHandle_t SerialRemoteHandle = xTaskGetHandle("CollectGalTask");
                        TaskHandle_t Hj2122025SendHandle = xTaskGetHandle("Hj2025Task");
                        TaskHandle_t TempConTaskHandle = xTaskGetHandle("TempConTask");
                        TaskHandle_t udSetTaskHandle = xTaskGetHandle("udSetTask");
                        TaskHandle_t netResTaskHandle = xTaskGetHandle("netResTask");

                        if (netResTaskHandle != NULL)
                        {
                            vTaskDelete(netResTaskHandle);
                            LOG_INFO("Stopped: netResTaskHandle task");
                        }
                        if (udSetTaskHandle != NULL)
                        {
                            vTaskDelete(udSetTaskHandle);
                            LOG_INFO("Stopped: udSetTask task");
                        }
                        if (ControldtuHandle != NULL)
                        {
                            vTaskDelete(ControldtuHandle);
                            LOG_INFO("Stopped: ControldtuHandle task");
                        }
                        if (TempConTaskHandle != NULL)
                        {
                            vTaskDelete(TempConTaskHandle);
                            LOG_INFO("Stopped: TempConTask task");
                        }
                        if (CollectTaskHandle != NULL)
                        {
                            vTaskDelete(CollectTaskHandle);
                            LOG_INFO("Stopped: collect task");
                        }
                        if (Hj2122025SendHandle != NULL)
                        {
                            vTaskDelete(Hj2122025SendHandle);
                            LOG_INFO("Stopped: Hj2122025SendHandle task");
                        }
                        if (Hj212SendHandle != NULL)
                        {
                            vTaskDelete(Hj212SendHandle);
                            LOG_INFO("Stopped: LED task");
                        }
                        if (SaveDataFileHandle != NULL)
                        {
                            vTaskDelete(SaveDataFileHandle);
                            LOG_INFO("Stopped: serial task");
                        }
                        if (LedPrintHandle != NULL)
                        {
                            vTaskDelete(LedPrintHandle);
                            LOG_INFO("Stopped: calibration data task");
                        }
                        if (LcdControlHandle != NULL)
                        {
                            vTaskDelete(LcdControlHandle);
                            LOG_INFO("Stopped: calibration data task");
                        }
                        if (SerialRemoteHandle != NULL)
                        {
                            vTaskDelete(SerialRemoteHandle);
                            LOG_INFO("Stopped: calibration data task");
                        }

                        vTaskDelay(3000 / portTICK_PERIOD_MS);

                        volatile uint32_t otaTotalSize = otaSize;
                        esp_ota_handle_t ota_handle;
                        const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
                        if (esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &ota_handle) != ESP_OK)
                        {
                            LOG_ERROR("Failed to start OTA");
                            return;
                        }

#define OTA_BUFFER_SIZE 1024
                        auto &sm = SerialManager::getInstance();
                        Stream *DTU_port = sm.getStream(SERIAL_DTU);
                        SemaphoreHandle_t DTUMutex = sm.getMutex(SERIAL_DTU);
                        LOG_INFO("Attempting to lock serial mutexes...");
                        while (true)
                        {
                            if (xSemaphoreTake(DTUMutex, pdMS_TO_TICKS(3000)) == pdTRUE)
                            {
                                break;
                            }
                            vTaskDelay(500 / portTICK_PERIOD_MS);
                        }
                        while (DTU_port->available())
                        {
                            DTU_port->read();
                            vTaskDelay(1 / portTICK_PERIOD_MS);
                        }

                        DTU_port->printf("Ready to start OTA, size: %d byte, Please send the OTA upgrade package within 300 seconds\n", otaTotalSize);
                        DTU_port->printf("The single packet sent is 1024 bytes, with a sending interval of 1000ms\n");
                        LOG_INFO("Ready to start OTA");

                        uint8_t data[OTA_BUFFER_SIZE];
                        int bytes_written = 0;
                        unsigned long lastDataTime = millis();
                        int local_buf_idx = 0;

                        while (bytes_written < otaTotalSize)
                        {
                            while (DTU_port->available() > 0 && local_buf_idx < 1024)
                            {
                                if (bytes_written + local_buf_idx >= otaTotalSize)
                                {
                                    break;
                                }
                                int c = DTU_port->read();
                                if (c != -1)
                                {
                                    data[local_buf_idx++] = (uint8_t)c;
                                    lastDataTime = millis();
                                }
                            }
                            if (local_buf_idx <= 1024 && local_buf_idx > 0)
                            {
                                if (esp_ota_write(ota_handle, data, local_buf_idx) != ESP_OK)
                                {
                                    LOG_ERROR("Failed to write data to flash");
                                    break;
                                }
                                bytes_written += local_buf_idx;
                                Serial.printf("%d/%d \n", bytes_written, otaTotalSize);
                                local_buf_idx = 0;
                                lastDataTime = millis();
                            }
                            if (millis() - lastDataTime > 30000)
                            {
                                LOG_ERROR("OTA upload timeout!");
                                break;
                            }
                            vTaskDelay(1 / portTICK_PERIOD_MS);
                        }
                        if (bytes_written == otaTotalSize)
                        {
                            DTU_port->printf("OTA download complete! Finalizing...");
                            if (esp_ota_end(ota_handle) == ESP_OK)
                            {
                                DTU_port->printf("OTA success! System restarting in 3 seconds...");
                                esp_ota_set_boot_partition(update_partition);
                                vTaskDelay(3000 / portTICK_PERIOD_MS);
                                ESP.restart();
                            }
                            else
                            {
                                DTU_port->printf("Failed to end OTA (checksum error etc.), please restart");
                                ESP.restart();
                            }
                        }
                        else
                        {
                            LOG_ERROR("OTA Failed. Written %d / %d bytes. Restarting system...", bytes_written, otaTotalSize);
                            esp_ota_end(ota_handle);
                            vTaskDelay(3000 / portTICK_PERIOD_MS);
                            ESP.restart();
                        }
                    }
                    else
                    {
                        LOG_WARNING("Invalid OTA size received: %s", allData->arguments.c_str());
                    }
                }
                LOG_DEBUG("****** OtaUploadTask Test Over ******");
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    vTaskDelete(NULL);
}
static void CollectTask(void *pvParameters)
{
    LOG_INFO("CollectTask Started");

    xTaskCreatePinnedToCore(DataProcessTask, "DataProcessTask", 4 * 1024, NULL, 8, NULL, 0);

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
        digitalWrite(PUMP1_PIN, HIGH);
        vTaskDelay(pdMS_TO_TICKS(collectTime * 1000 / 2));
        collectorManager.poll();
        digitalWrite(PUMP1_PIN, LOW);
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
        xLastWakeTime = xTaskGetTickCount();
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
        tempManager.poll();
        vTaskDelay(pdMS_TO_TICKS(30000));
    }
    vTaskDelete(NULL);
}

// 发送实时数据 / 小时数据 / 天数据
static void Hj212_2017SendTask(void *pvParameters)
{
    LOG_INFO("Hj212_2017SendTask Started");
    QueueHandle_t Hj212SendTaskQueue = EventBus::getInstance().createReceiverQueue(10);
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
                        SemaphoreHandle_t _StreamMutex = SerialManager::getInstance().getMutex(SERIAL_HJ212);
                        if (xSemaphoreTake(_StreamMutex, pdMS_TO_TICKS(3000)) == pdTRUE)
                        {
                            DTUManager::getInstance().sendHJ212Packet(HJ212_str);
                            xSemaphoreGive(_StreamMutex);
                        }
                    }
                    else
                    {
                        if (allData->last_update > 20260526120000) {
                            LOG_WARNING("HJ212 Packet construction failed or empty.");
                            filesys.savePendingPacket(allData->last_update);
                            filesys.storeProcessedPacket(allData);
                        }
                    }
                }
                else
                {
                    if (allData->last_update > 20260526120000) {
                        LOG_WARNING("HJ212 Packet construction failed or empty.");
                        filesys.savePendingPacket(allData->last_update);
                        filesys.storeProcessedPacket(allData);
                    }
                }
                allData->release();
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
                    SemaphoreHandle_t _StreamMutex = SerialManager::getInstance().getMutex(SERIAL_HJ212);
                    if (xSemaphoreTake(_StreamMutex, pdMS_TO_TICKS(3000)) == pdTRUE)
                    {
                        xSemaphoreGive(_StreamMutex);
                        DTUManager::getInstance().sendHJ212Packet(HJ212_str);
                    }
                }
                allData->release();
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
    vTaskDelete(NULL);
}

static void Hj212_2025SendTask(void *pvParameters)
{
    LOG_INFO("Hj212_2017SendTask Started");
    QueueHandle_t Hj212SendTaskQueue = EventBus::getInstance().createReceiverQueue(10);
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
                        SemaphoreHandle_t _StreamMutex = SerialManager::getInstance().getMutex(SERIAL_HJ212);
                        if (xSemaphoreTake(_StreamMutex, pdMS_TO_TICKS(3000)) == pdTRUE)
                        {
                            xSemaphoreGive(_StreamMutex);
                            DTUManager::getInstance().sendHJ212Packet(HJ212_str);
                        }
                    }
                    else
                    {
                        LOG_WARNING("HJ212 Packet construction failed or empty.");
                        filesys.savePendingPacket(allData->last_update);
                        filesys.storeProcessedPacket(allData);
                    }
                }
                else
                {
                    filesys.savePendingPacket(allData->last_update);
                    filesys.storeProcessedPacket(allData);
                }
                allData->release();
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
                    SemaphoreHandle_t _StreamMutex = SerialManager::getInstance().getMutex(SERIAL_HJ212);
                    if (xSemaphoreTake(_StreamMutex, pdMS_TO_TICKS(3000)) == pdTRUE)
                    {
                        xSemaphoreGive(_StreamMutex);
                        DTUManager::getInstance().sendHJ212Packet(HJ212_str);
                    }
                }
                allData->release();
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
    vTaskDelete(NULL);
}
static void netWorkRestoreTask(void *pvParameters)
{
    ResumeData *resumeData = (ResumeData *)pvParameters;
    if (resumeData == nullptr)
    {
        LOG_ERROR("netWorkRestoreTask received null data");
        vTaskDelete(NULL);
        return;
    }

    LOG_INFO("netWorkRestoreTask started with %d pending packets", resumeData->packets.size());

    HJ212_DataCenter HJ212;
    const auto &config = ConfigManager::getInstance().getHJ212();
    auto &filesys = filesysManager::getInstance();

    for (uint64_t timestamp : resumeData->packets)
    {
        LOG_DEBUG("Resending packet for timestamp: %llu", timestamp);
        uint64_t currentTime = timestamp * 100 + 1;
        AllProcessedDataPacket *pendingData = filesys.readPendingPacket(MIN_DATA, currentTime);

        if (pendingData)
        {
            int subCount = EventBus::getInstance().getSubscriberCount(EventID::RESUME_DATA);
            for (int i = 0; i < subCount; i++)
            {
                pendingData->retain();
            }
            EventBus::getInstance().publish(EventID::RESUME_DATA, pendingData);
            pendingData->release();
        }
        vTaskDelay(pdMS_TO_TICKS(20000));
    }
    delete resumeData;
    LOG_INFO("netWorkRestoreTask completed");
    vTaskDelete(NULL);
}

static void LedPrintTask(void *pvParameters)
{
    LOG_DEBUG("LedPrintTask Started");

    QueueHandle_t LedPrintTaskQueue = EventBus::getInstance().createReceiverQueue(5);
    EventBus::getInstance().subscribe(EventID::PROCESSED_DATA_COLLECTED, LedPrintTaskQueue);

    EventMsg msg;
    while (true)
    {
        if (EventBus::waitEvent(LedPrintTaskQueue, msg))
        {
            if (msg.id == EventID::PROCESSED_DATA_COLLECTED)
            {
                AllProcessedDataPacket *allData = (AllProcessedDataPacket *)msg.data;

                if (allData != nullptr)
                {
                }
                LOG_DEBUG("****** LedPrintTask Test Over ******");
                allData->release();
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

    xTaskCreatePinnedToCore(SerialControlTask_lcd, "Controllcd", 4 * 1024, NULL, 10, NULL, 1);
    xTaskCreatePinnedToCore(SerialControlTask_dtu, "Controldtu", 4 * 1024, NULL, 10, NULL, 1);

    while (true)
    {
        permission.poll();
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

// ******************* 时间函数实现 *******************
// 202605201040
// 202605010000
void timeInit(uint64_t timestamp)
{
    rtc.init();
    if (timestamp >= 0)
    {
        Ds1302::DateTime dt;
        uint64_t temp = timestamp;
        dt.minute = temp % 100;
        temp /= 100;
        dt.hour = temp % 100;
        temp /= 100;
        dt.day = temp % 100;
        temp /= 100;
        dt.month = temp % 100;
        temp /= 100;
        dt.year = (uint8_t)(temp % 100);
        dt.second = 0;
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
    snprintf(buffer, sizeof(buffer), "%04d%02d%02d%02d%02d",
             now.year + 2000, now.month, now.day, now.hour, now.minute);
    return strtoull(buffer, nullptr, 10);
}
// 202605220939
bool updateMillisTime(uint64_t newTime)
{
    int year, month, day, hour, minute;
    if (sscanf(String(newTime).c_str(), "%4d%2d%2d%2d%2d", &year, &month, &day, &hour, &minute) == 5)
    {
        struct tm timeinfo = {};
        timeinfo.tm_year = year - 1900; // 自1900年起的年数
        timeinfo.tm_mon = month - 1;    // 0-11月
        timeinfo.tm_mday = day;
        timeinfo.tm_hour = hour;
        timeinfo.tm_min = minute;
        timeinfo.tm_sec = 0;    // 格式中无秒，默认为0
        timeinfo.tm_isdst = -1; // 自动判断夏令时

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
}

// ******************* 函数实现 *******************
void setUpInit(void)
{
    LOG_INFO("System setup init start!");
    auto &sm = SerialManager::getInstance();
    Stream *HJ212_port = sm.getStream(SERIAL_HJ212);
    Stream *DTU_port = sm.getStream(SERIAL_DTU);
    auto &DTUMg = DTUManager::getInstance();
    DTUMg.init(*DTU_port, *HJ212_port);

    SemaphoreHandle_t DTUMutex = sm.getMutex(SERIAL_DTU);
    if (xSemaphoreTake(DTUMutex, pdMS_TO_TICKS(3000)) == pdTRUE)
    {
        uint64_t realTime = DTUMg.hjSystemTime();
        if (realTime < 198001010002)
        {
            realTime = DTUMg.dtuSystemTime();
        }
        if (realTime < 202605260000)
        {
            realTime = DTUMg.dtuSystemTime();
        }
        if (realTime < 202605260000)
        {
            realTime = DTUMg.dtuSystemTime();
        }
        LOG_DEBUG("****** realTime Time: %llu ******", realTime);
        xSemaphoreGive(DTUMutex);
        timeInit(realTime);

        uint64_t currentTime1 = getCurrentTime();
        LOG_DEBUG("****** currentTime1 Time: %llu ******", currentTime1);

        if (!updateMillisTime(realTime))
        {
            LOG_ERROR("Failed to update milliseconds time");
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    HJ212CONFIG hj212Cfg = ConfigManager::getInstance().getHJ212();
    SYSTEMCONFIG systemCfg = ConfigManager::getInstance().getSystem();
    SemaphoreHandle_t DTUHj212Mutex = sm.getMutex(SERIAL_HJ212);
    if (xSemaphoreTake(DTUMutex, pdMS_TO_TICKS(3000)) == pdTRUE)
    {
        if (!DTUMg.updateHJDtuGoalIP(hj212Cfg.ip))
        {
            LOG_ERROR("Failed to update HJ212 DTU IP");
        }
        xSemaphoreGive(DTUMutex);
    }
    SemaphoreHandle_t DTUREMutex = sm.getMutex(SERIAL_DTU);
    if (xSemaphoreTake(DTUREMutex, pdMS_TO_TICKS(3000)) == pdTRUE)
    {
        if (!DTUMg.updateReDtuGoalIP(systemCfg.dtu_server))
        {
            LOG_ERROR("Failed to update Remote DTU IP");
        }
        xSemaphoreGive(DTUREMutex);
    }
}
void fileRestore(void)
{
    auto &filesys = filesysManager::getInstance();
    std::vector<uint64_t> pendingTimestamps = filesys.scanPendingTimestamps();
    if (!pendingTimestamps.empty())
    {
        LOG_INFO("Found %d pending packets, creating recovery task", pendingTimestamps.size());
        ResumeData *resumeData = new ResumeData();
        resumeData->packets = pendingTimestamps;
        xTaskCreatePinnedToCore(netWorkRestoreTask, "netResTask", 8 * 1024, resumeData, 5, NULL, 0);
    }
}
void otaUpload(int otaSize)
{

    TaskHandle_t CollectTaskHandle = xTaskGetHandle("CollectTask");
    TaskHandle_t Hj212SendHandle = xTaskGetHandle("Hj2017Task");
    TaskHandle_t SaveDataFileHandle = xTaskGetHandle("SaveFileTask");
    TaskHandle_t LedPrintHandle = xTaskGetHandle("LedPrintTask");
    TaskHandle_t LcdControlHandle = xTaskGetHandle("Controllcd");
    TaskHandle_t ControldtuHandle = xTaskGetHandle("Controldtu");
    TaskHandle_t SerialRemoteHandle = xTaskGetHandle("CollectGalTask");
    TaskHandle_t Hj2122025SendHandle = xTaskGetHandle("Hj2025Task");
    TaskHandle_t TempConTaskHandle = xTaskGetHandle("TempConTask");
    TaskHandle_t udSetTaskHandle = xTaskGetHandle("udSetTask");
    TaskHandle_t netResTaskHandle = xTaskGetHandle("netResTask");

    if (netResTaskHandle != NULL)
    {
        vTaskDelete(netResTaskHandle);
        LOG_INFO("Stopped: netResTaskHandle task");
    }
    if (udSetTaskHandle != NULL)
    {
        vTaskDelete(udSetTaskHandle);
        LOG_INFO("Stopped: udSetTask task");
    }
    if (ControldtuHandle != NULL)
    {
        vTaskDelete(ControldtuHandle);
        LOG_INFO("Stopped: ControldtuHandle task");
    }
    if (TempConTaskHandle != NULL)
    {
        vTaskDelete(TempConTaskHandle);
        LOG_INFO("Stopped: TempConTask task");
    }
    if (CollectTaskHandle != NULL)
    {
        vTaskDelete(CollectTaskHandle);
        LOG_INFO("Stopped: collect task");
    }
    if (Hj2122025SendHandle != NULL)
    {
        vTaskDelete(Hj2122025SendHandle);
        LOG_INFO("Stopped: Hj2122025SendHandle task");
    }
    if (Hj212SendHandle != NULL)
    {
        vTaskDelete(Hj212SendHandle);
        LOG_INFO("Stopped: LED task");
    }
    if (SaveDataFileHandle != NULL)
    {
        vTaskDelete(SaveDataFileHandle);
        LOG_INFO("Stopped: serial task");
    }
    if (LedPrintHandle != NULL)
    {
        vTaskDelete(LedPrintHandle);
        LOG_INFO("Stopped: calibration data task");
    }
    if (LcdControlHandle != NULL)
    {
        vTaskDelete(LcdControlHandle);
        LOG_INFO("Stopped: calibration data task");
    }
    if (SerialRemoteHandle != NULL)
    {
        vTaskDelete(SerialRemoteHandle);
        LOG_INFO("Stopped: calibration data task");
    }

    vTaskDelay(3000 / portTICK_PERIOD_MS);

    volatile uint32_t otaTotalSize = otaSize;
    esp_ota_handle_t ota_handle;
    const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
    if (esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &ota_handle) != ESP_OK)
    {
        LOG_ERROR("Failed to start OTA");
        return;
    }

#define OTA_BUFFER_SIZE 1024
    auto &sm = SerialManager::getInstance();
    Stream *DTU_port = sm.getStream(SERIAL_DTU);
    SemaphoreHandle_t DTUMutex = sm.getMutex(SERIAL_DTU);
    LOG_INFO("Attempting to lock serial mutexes...");
    while (true)
    {
        if (xSemaphoreTake(DTUMutex, pdMS_TO_TICKS(3000)) == pdTRUE)
        {
            break;
        }
        vTaskDelay(500 / portTICK_PERIOD_MS);
    }
    while (DTU_port->available())
    {
        DTU_port->read();
        vTaskDelay(1 / portTICK_PERIOD_MS);
    }

    DTU_port->printf("Ready to start OTA, size: %d byte, Please send the OTA upgrade package within 300 seconds\n", otaTotalSize);
    DTU_port->printf("The single packet sent is 1024 bytes, with a sending interval of 1000ms\n");
    LOG_INFO("Ready to start OTA");

    uint8_t data[OTA_BUFFER_SIZE];
    int bytes_written = 0;
    unsigned long lastDataTime = millis();
    int local_buf_idx = 0;

    while (bytes_written < otaTotalSize)
    {
        while (DTU_port->available() > 0 && local_buf_idx < 1024)
        {
            if (bytes_written + local_buf_idx >= otaTotalSize)
            {
                break;
            }
            int c = DTU_port->read();
            if (c != -1)
            {
                data[local_buf_idx++] = (uint8_t)c;
                lastDataTime = millis();
            }
        }
        if (local_buf_idx == 1024 || (local_buf_idx > 0 && (bytes_written + local_buf_idx >= otaTotalSize)))
        {
            if (esp_ota_write(ota_handle, data, local_buf_idx) != ESP_OK)
            {
                LOG_ERROR("Failed to write data to flash");
                break;
            }
            bytes_written += local_buf_idx;
            DTU_port->printf("%d/%d \n", bytes_written, otaTotalSize);
            local_buf_idx = 0;
            lastDataTime = millis();
        }
        if (millis() - lastDataTime > 30000)
        {
            LOG_ERROR("OTA upload timeout!");
            break;
        }
        vTaskDelay(1 / portTICK_PERIOD_MS);
    }
    if (bytes_written == otaTotalSize)
    {
        esp_err_t wdt_status = esp_task_wdt_delete(NULL); // NULL 代表当前任务
        if (wdt_status == ESP_OK) {
            LOG_DEBUG("Task WDT successfully disabled for OTA finalization.");
        } else {
            LOG_DEBUG("[DEBUG] WDT delete failed or not initialized: 0x%X\n", wdt_status);
        }
        DTU_port->printf("OTA download complete! Finalizing...");
        if (esp_ota_end(ota_handle) == ESP_OK)
        {
            DTU_port->printf("OTA success! System restarting in 3 seconds...");
            esp_ota_set_boot_partition(update_partition);
            vTaskDelay(3000 / portTICK_PERIOD_MS);
            ESP.restart();
        }
        else
        {
            DTU_port->printf("Failed to end OTA (checksum error etc.), please restart");
            ESP.restart();
        }
    }
    else
    {
        LOG_ERROR("OTA Failed. Written %d / %d bytes. Restarting system...", bytes_written, otaTotalSize);
        esp_ota_end(ota_handle);
        vTaskDelay(3000 / portTICK_PERIOD_MS);
        ESP.restart();
    }
}
