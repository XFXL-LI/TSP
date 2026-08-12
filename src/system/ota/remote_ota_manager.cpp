#include "remote_ota_manager.h"

#include <Arduino.h>
#include <atomic>
#include <esp_err.h>
#include <esp_ota_ops.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <new>

#include "../../inc/sys_init.h"
#include "../../module/Serial/SerialManager.h"
#include "../../module/json/config_json.h"
#include "../../module/log/log_manager.h"
#include "../event/eventBus.h"

namespace
{
constexpr size_t OTA_BUFFER_SIZE = 1024;
constexpr uint32_t OTA_SERIAL_LOCK_TIMEOUT_MS = 15000;
constexpr uint32_t OTA_BUSINESS_QUIESCE_TIMEOUT_MS = 90000;
constexpr uint32_t OTA_INACTIVITY_TIMEOUT_MS = 90000;
constexpr uint32_t OTA_SESSION_TIMEOUT_MS = 20UL * 60UL * 1000UL;
constexpr uint32_t OTA_REQUEST_SETTLE_MS = 250;
constexpr uint32_t OTA_WRITE_IDLE_GAP_MS = 20;
constexpr uint32_t OTA_WORKER_STACK_SIZE = 8 * 1024;

struct OtaRequest
{
    size_t imageSize;
};

std::atomic<bool> otaActive(false);
bool businessPauseRequested = false;
uint32_t activeBusinessOperations = 0;
portMUX_TYPE businessStateMux = portMUX_INITIALIZER_UNLOCKED;
TaskHandle_t listenerHandle = nullptr;
UBaseType_t otaWorkerPriority = 1;

[[noreturn]] void deleteCurrentTask()
{
    vTaskDelete(nullptr);
    while (true)
    {
        taskYIELD();
    }
}

void publishUploadResponse(JSONCmdData *request)
{
    int subscriberCount =
        EventBus::getInstance().getSubscriberCount(EventID::UPLOAD_RES);
    for (int i = 0; i < subscriberCount; ++i)
    {
        request->retain();
    }
    EventBus::getInstance().publish(EventID::UPLOAD_RES, request);
    request->release();
}

void drainInput(Stream *port)
{
    while (port != nullptr && port->available() > 0)
    {
        port->read();
        taskYIELD();
    }
}

[[noreturn]] void finishWorkerWithoutBusinessPause(esp_ota_handle_t otaHandle,
                                                    bool otaHandleOpen,
                                                    const char *reason)
{
    if (otaHandleOpen)
    {
        esp_ota_abort(otaHandle);
    }
    LOG_ERROR("Remote OTA stopped before business pause: %s", reason);
    otaActive.store(false, std::memory_order_release);
    deleteCurrentTask();
}

[[noreturn]] void finishWorkerAfterPauseFailure(esp_ota_handle_t otaHandle,
                                                SemaphoreHandle_t dtuMutex,
                                                Stream *dtuPort,
                                                const char *reason)
{
    RemoteOtaManager::resumeBusiness();
    esp_ota_abort(otaHandle);
    if (dtuPort != nullptr)
    {
        dtuPort->printf("OTA rejected: %s\n", reason);
    }
    if (dtuMutex != nullptr)
    {
        xSemaphoreGive(dtuMutex);
    }
    LOG_ERROR("Remote OTA stopped: %s", reason);
    otaActive.store(false, std::memory_order_release);
    deleteCurrentTask();
}

void otaWorkerTask(void *pvParameters)
{
    OtaRequest *request = static_cast<OtaRequest *>(pvParameters);
    const size_t otaTotalSize = request != nullptr ? request->imageSize : 0;
    delete request;

    if (otaTotalSize == 0)
    {
        LOG_ERROR("Remote OTA rejected: image size is zero");
        otaActive.store(false, std::memory_order_release);
        deleteCurrentTask();
    }

    const esp_partition_t *updatePartition =
        esp_ota_get_next_update_partition(nullptr);
    if (updatePartition == nullptr || otaTotalSize > updatePartition->size)
    {
        LOG_ERROR("Remote OTA rejected: size=%u partition=%u",
                  static_cast<unsigned>(otaTotalSize),
                  updatePartition == nullptr
                      ? 0U
                      : static_cast<unsigned>(updatePartition->size));
        otaActive.store(false, std::memory_order_release);
        deleteCurrentTask();
    }

    esp_ota_handle_t otaHandle = 0;
    esp_err_t beginResult =
        esp_ota_begin(updatePartition, otaTotalSize, &otaHandle);
    if (beginResult != ESP_OK)
    {
        LOG_ERROR("Remote OTA begin failed: %s", esp_err_to_name(beginResult));
        otaActive.store(false, std::memory_order_release);
        deleteCurrentTask();
    }
    bool otaHandleOpen = true;

    auto &serialManager = SerialManager::getInstance();
    Stream *dtuPort = serialManager.getStream(SERIAL_DTU);
    SemaphoreHandle_t dtuMutex = serialManager.getMutex(SERIAL_DTU);
    if (dtuPort == nullptr || dtuMutex == nullptr)
    {
        finishWorkerWithoutBusinessPause(otaHandle, otaHandleOpen,
                                         "DTU port is unavailable");
    }

    // Allow the request parser to publish UPLOAD_RES, return from processLine
    // and leave its guarded business region before the OTA asks for quiescence.
    vTaskDelay(pdMS_TO_TICKS(OTA_REQUEST_SETTLE_MS));

    if (xSemaphoreTake(dtuMutex,
                       pdMS_TO_TICKS(OTA_SERIAL_LOCK_TIMEOUT_MS)) != pdTRUE)
    {
        finishWorkerWithoutBusinessPause(otaHandle, otaHandleOpen,
                                         "DTU mutex timeout");
    }

    // Stop new business operations, then wait for every in-flight guarded
    // operation to finish. This replaces the old fixed 250 ms assumption.
    if (!RemoteOtaManager::requestBusinessPause(
            OTA_BUSINESS_QUIESCE_TIMEOUT_MS))
    {
        finishWorkerAfterPauseFailure(otaHandle, dtuMutex, dtuPort,
                                      "business quiesce timeout");
    }

    drainInput(dtuPort);
    dtuPort->printf(
        "Ready to start OTA, size: %u byte, inactivity timeout: %u seconds\n",
        static_cast<unsigned>(otaTotalSize),
        static_cast<unsigned>(OTA_INACTIVITY_TIMEOUT_MS / 1000));
    dtuPort->printf(
        "The single packet sent is 800 bytes, with a sending interval of 1000ms\n");
    LOG_INFO("Remote OTA ready: size=%u business_pause=confirmed",
             static_cast<unsigned>(otaTotalSize));

    uint8_t data[OTA_BUFFER_SIZE];
    size_t bytesWritten = 0;
    size_t localBufferLength = 0;
    uint32_t lastDataTime = millis();
    const uint32_t sessionStartTime = lastDataTime;
    const char *failureReason = nullptr;

    while (bytesWritten < otaTotalSize)
    {
        while (dtuPort->available() > 0 &&
               localBufferLength < OTA_BUFFER_SIZE &&
               bytesWritten + localBufferLength < otaTotalSize)
        {
            int value = dtuPort->read();
            if (value >= 0)
            {
                data[localBufferLength++] = static_cast<uint8_t>(value);
                lastDataTime = millis();
            }
        }

        uint32_t now = millis();
        bool bufferReady = localBufferLength == OTA_BUFFER_SIZE ||
                           bytesWritten + localBufferLength == otaTotalSize ||
                           (localBufferLength > 0 &&
                            now - lastDataTime >= OTA_WRITE_IDLE_GAP_MS);
        if (bufferReady)
        {
            esp_err_t writeResult =
                esp_ota_write(otaHandle, data, localBufferLength);
            if (writeResult != ESP_OK)
            {
                LOG_ERROR("Remote OTA flash write failed: %s",
                          esp_err_to_name(writeResult));
                failureReason = "flash write failed";
                break;
            }

            bytesWritten += localBufferLength;
            dtuPort->printf("%u/%u\n",
                            static_cast<unsigned>(bytesWritten),
                            static_cast<unsigned>(otaTotalSize));
            LOG_DEBUG("Received OTA data: %u/%u bytes",
                      static_cast<unsigned>(bytesWritten),
                      static_cast<unsigned>(otaTotalSize));
            localBufferLength = 0;
        }

        now = millis();
        if (now - lastDataTime > OTA_INACTIVITY_TIMEOUT_MS)
        {
            failureReason = "data inactivity timeout";
            break;
        }
        if (now - sessionStartTime > OTA_SESSION_TIMEOUT_MS)
        {
            failureReason = "20 minute session timeout";
            break;
        }

        vTaskDelay(pdMS_TO_TICKS(1));
    }

    if (failureReason == nullptr && bytesWritten == otaTotalSize)
    {
        dtuPort->printf("OTA download complete! Finalizing...\n");
        esp_err_t endResult = esp_ota_end(otaHandle);
        otaHandleOpen = false;
        if (endResult == ESP_OK)
        {
            esp_err_t bootResult = esp_ota_set_boot_partition(updatePartition);
            if (bootResult == ESP_OK)
            {
                dtuPort->printf(
                    "OTA success! System restarting in 3 seconds...\n");
                LOG_INFO("Remote OTA completed: %u bytes",
                         static_cast<unsigned>(bytesWritten));
                xSemaphoreGive(dtuMutex);
                vTaskDelay(pdMS_TO_TICKS(3000));
                ESP.restart();
                deleteCurrentTask();
            }

            LOG_ERROR("Remote OTA boot partition failed: %s",
                      esp_err_to_name(bootResult));
            failureReason = "boot partition update failed";
        }
        else
        {
            LOG_ERROR("Remote OTA image validation failed: %s",
                      esp_err_to_name(endResult));
            failureReason = "image validation failed";
        }
    }
    else if (failureReason == nullptr)
    {
        failureReason = "received size mismatch";
    }

    if (otaHandleOpen)
    {
        esp_ota_abort(otaHandle);
    }

    dtuPort->printf("OTA failed: %s. Written %u/%u bytes.\n",
                    failureReason,
                    static_cast<unsigned>(bytesWritten),
                    static_cast<unsigned>(otaTotalSize));
    LOG_ERROR("Remote OTA failed: reason=%s written=%u/%u",
              failureReason,
              static_cast<unsigned>(bytesWritten),
              static_cast<unsigned>(otaTotalSize));

    drainInput(dtuPort);
    xSemaphoreGive(dtuMutex);
    RemoteOtaManager::resumeBusiness();
    otaActive.store(false, std::memory_order_release);
    deleteCurrentTask();
}

void otaListenerTask(void *pvParameters)
{
    (void)pvParameters;
    LOG_INFO("Remote OTA listener started");

    QueueHandle_t requestQueue =
        EventBus::getInstance().createReceiverQueue(5, "OTA");
    if (requestQueue == nullptr)
    {
        LOG_ERROR("Remote OTA listener queue creation failed");
        listenerHandle = nullptr;
        deleteCurrentTask();
    }

    EventBus::getInstance().subscribe(EventID::UPLOAD_REQ, requestQueue);

    EventMsg msg;
    while (true)
    {
        if (!EventBus::waitEvent(requestQueue, msg))
        {
            continue;
        }
        if (msg.id != EventID::UPLOAD_REQ || msg.data == nullptr)
        {
            continue;
        }

        JSONCmdData *request = static_cast<JSONCmdData *>(msg.data);
        LOG_DEBUG("Remote OTA request: %s", request->arguments.c_str());

        config_json uploadJson(request->arguments.c_str());
        int requestedSize =
            uploadJson.isValid() ? uploadJson.getInt("size", 0) : 0;

        publishUploadResponse(request);

        if (requestedSize <= 0)
        {
            LOG_WARNING("Remote OTA request rejected: invalid size=%d",
                        requestedSize);
            continue;
        }

        bool expected = false;
        if (!otaActive.compare_exchange_strong(expected, true,
                                               std::memory_order_acq_rel))
        {
            LOG_WARNING("Remote OTA request rejected: session already active");
            continue;
        }

        OtaRequest *workerRequest = new (std::nothrow) OtaRequest{
            static_cast<size_t>(requestedSize)};
        if (workerRequest == nullptr)
        {
            LOG_ERROR("Remote OTA request allocation failed");
            otaActive.store(false, std::memory_order_release);
            continue;
        }

        BaseType_t result = xTaskCreatePinnedToCore(
            otaWorkerTask,
            "otaUpload",
            OTA_WORKER_STACK_SIZE,
            workerRequest,
            otaWorkerPriority,
            nullptr,
            1);
        if (result != pdPASS)
        {
            delete workerRequest;
            otaActive.store(false, std::memory_order_release);
            LOG_ERROR("Remote OTA worker creation failed");
        }
    }
}
} // namespace

namespace RemoteOtaManager
{
bool begin(UBaseType_t listenerPriority, UBaseType_t workerPriority)
{
    if (listenerHandle != nullptr)
    {
        return true;
    }

    otaWorkerPriority = workerPriority;
    BaseType_t result = xTaskCreatePinnedToCore(
        otaListenerTask,
        "OtaUploadTask",
        4 * 1024,
        nullptr,
        listenerPriority,
        &listenerHandle,
        1);

    if (result != pdPASS)
    {
        listenerHandle = nullptr;
        LOG_ERROR("Remote OTA listener creation failed");
        return false;
    }
    return true;
}

bool isActive()
{
    return otaActive.load(std::memory_order_acquire);
}

void beginBusinessActivity()
{
    while (true)
    {
        bool entered = false;
        portENTER_CRITICAL(&businessStateMux);
        if (!businessPauseRequested)
        {
            ++activeBusinessOperations;
            entered = true;
        }
        portEXIT_CRITICAL(&businessStateMux);

        if (entered)
        {
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void endBusinessActivity()
{
    portENTER_CRITICAL(&businessStateMux);
    if (activeBusinessOperations > 0)
    {
        --activeBusinessOperations;
    }
    portEXIT_CRITICAL(&businessStateMux);
}

bool requestBusinessPause(uint32_t timeoutMs)
{
    portENTER_CRITICAL(&businessStateMux);
    businessPauseRequested = true;
    portEXIT_CRITICAL(&businessStateMux);

    const uint32_t startedAt = millis();
    while (true)
    {
        uint32_t active = activeBusinessCount();
        if (active == 0)
        {
            LOG_INFO("Remote OTA business pause confirmed");
            return true;
        }
        if (millis() - startedAt >= timeoutMs)
        {
            LOG_ERROR("Remote OTA business pause timeout: active=%u",
                      static_cast<unsigned>(active));
            resumeBusiness();
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void resumeBusiness()
{
    portENTER_CRITICAL(&businessStateMux);
    businessPauseRequested = false;
    portEXIT_CRITICAL(&businessStateMux);
}

uint32_t activeBusinessCount()
{
    portENTER_CRITICAL(&businessStateMux);
    uint32_t active = activeBusinessOperations;
    portEXIT_CRITICAL(&businessStateMux);
    return active;
}
} // namespace RemoteOtaManager
