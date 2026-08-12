#include "remote_ota_manager.h"

#include <Arduino.h>
#include <atomic>
#include <esp_err.h>
#include <esp_heap_caps.h>
#include <esp_ota_ops.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

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

// Firmware 2.0.6: this is the only OTA task. It is created during startup,
// before the long-running workload fragments the heap, and performs both
// request listening and flash writing. The receive buffer is static so it
// does not consume task stack during a session.
constexpr uint32_t OTA_TASK_STACK_SIZE = 6 * 1024;
uint8_t otaDataBuffer[OTA_BUFFER_SIZE];

std::atomic<bool> otaActive(false);
bool businessPauseRequested = false;
uint32_t activeBusinessOperations = 0;
portMUX_TYPE businessStateMux = portMUX_INITIALIZER_UNLOCKED;
TaskHandle_t listenerHandle = nullptr;
UBaseType_t otaIdlePriority = 1;
UBaseType_t otaActivePriority = 1;

uint32_t freeInternalHeap()
{
    return static_cast<uint32_t>(
        heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
}

uint32_t largestInternalBlock()
{
    return static_cast<uint32_t>(
        heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
}

void updateSessionMinimum(uint32_t &minimumFree)
{
    uint32_t currentFree = freeInternalHeap();
    if (currentFree < minimumFree)
    {
        minimumFree = currentFree;
    }
}

void logOtaMemory(const char *phase, uint32_t sessionMinimumFree = 0)
{
    LOG_INFO(
        "[DIAG] OTA_MEMORY phase=%s free=%u largest=%u stack_high_water=%u session_min_free=%u",
        phase,
        static_cast<unsigned>(freeInternalHeap()),
        static_cast<unsigned>(largestInternalBlock()),
        static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)),
        static_cast<unsigned>(sessionMinimumFree));
}

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

bool finishFailedSession(esp_ota_handle_t otaHandle,
                         bool otaHandleOpen,
                         SemaphoreHandle_t dtuMutex,
                         bool dtuMutexHeld,
                         Stream *dtuPort,
                         bool businessPaused,
                         const char *reason,
                         size_t bytesWritten,
                         size_t otaTotalSize,
                         uint32_t sessionMinimumFree)
{
    if (otaHandleOpen)
    {
        esp_ota_abort(otaHandle);
    }

    if (dtuPort != nullptr && dtuMutexHeld)
    {
        dtuPort->printf("OTA failed: %s. Written %u/%u bytes.\n",
                        reason,
                        static_cast<unsigned>(bytesWritten),
                        static_cast<unsigned>(otaTotalSize));
        drainInput(dtuPort);
    }
    if (dtuMutexHeld && dtuMutex != nullptr)
    {
        xSemaphoreGive(dtuMutex);
    }
    if (businessPaused)
    {
        RemoteOtaManager::resumeBusiness();
    }

    LOG_ERROR("Remote OTA failed: reason=%s written=%u/%u",
              reason,
              static_cast<unsigned>(bytesWritten),
              static_cast<unsigned>(otaTotalSize));
    logOtaMemory("failed", sessionMinimumFree);
    return false;
}

bool runOtaSession(size_t otaTotalSize)
{
    logOtaMemory("request");

    if (otaTotalSize == 0)
    {
        LOG_ERROR("Remote OTA rejected: image size is zero");
        return false;
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
        return false;
    }

    auto &serialManager = SerialManager::getInstance();
    Stream *dtuPort = serialManager.getStream(SERIAL_DTU);
    SemaphoreHandle_t dtuMutex = serialManager.getMutex(SERIAL_DTU);
    if (dtuPort == nullptr || dtuMutex == nullptr)
    {
        LOG_ERROR("Remote OTA stopped: DTU port is unavailable");
        return false;
    }

    // Allow the request parser to publish UPLOAD_RES, return from processLine
    // and leave its guarded business region before requesting quiescence.
    vTaskDelay(pdMS_TO_TICKS(OTA_REQUEST_SETTLE_MS));

    // First prevent new guarded work and wait for every active operation to
    // leave its safe region. Taking the DTU mutex only after this avoids
    // holding the mutex while an in-flight guarded command still needs it.
    if (!RemoteOtaManager::requestBusinessPause(
            OTA_BUSINESS_QUIESCE_TIMEOUT_MS))
    {
        LOG_ERROR("Remote OTA stopped: business quiesce timeout");
        return false;
    }
    bool businessPaused = true;

    if (xSemaphoreTake(dtuMutex,
                       pdMS_TO_TICKS(OTA_SERIAL_LOCK_TIMEOUT_MS)) != pdTRUE)
    {
        return finishFailedSession(0, false, dtuMutex, false, dtuPort,
                                   businessPaused, "DTU mutex timeout",
                                   0, otaTotalSize, freeInternalHeap());
    }
    bool dtuMutexHeld = true;

    esp_ota_handle_t otaHandle = 0;
    esp_err_t beginResult =
        esp_ota_begin(updatePartition, otaTotalSize, &otaHandle);
    if (beginResult != ESP_OK)
    {
        LOG_ERROR("Remote OTA begin failed: %s", esp_err_to_name(beginResult));
        return finishFailedSession(otaHandle, false, dtuMutex, dtuMutexHeld,
                                   dtuPort, businessPaused,
                                   "flash begin failed", 0, otaTotalSize,
                                   freeInternalHeap());
    }
    bool otaHandleOpen = true;
    uint32_t sessionMinimumFree = freeInternalHeap();

    drainInput(dtuPort);
    dtuPort->printf(
        "Ready to start OTA, size: %u byte, inactivity timeout: %u seconds\n",
        static_cast<unsigned>(otaTotalSize),
        static_cast<unsigned>(OTA_INACTIVITY_TIMEOUT_MS / 1000));
    dtuPort->printf(
        "The single packet sent is 800 bytes, with a sending interval of 1000ms\n");
    LOG_INFO("Remote OTA ready: size=%u business_pause=confirmed",
             static_cast<unsigned>(otaTotalSize));
    logOtaMemory("ready", sessionMinimumFree);

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
                otaDataBuffer[localBufferLength++] =
                    static_cast<uint8_t>(value);
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
                esp_ota_write(otaHandle, otaDataBuffer, localBufferLength);
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
            updateSessionMinimum(sessionMinimumFree);
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
        updateSessionMinimum(sessionMinimumFree);
        if (endResult == ESP_OK)
        {
            esp_err_t bootResult =
                esp_ota_set_boot_partition(updatePartition);
            if (bootResult == ESP_OK)
            {
                dtuPort->printf(
                    "OTA success! System restarting in 3 seconds...\n");
                LOG_INFO("Remote OTA completed: %u bytes",
                         static_cast<unsigned>(bytesWritten));
                logOtaMemory("complete", sessionMinimumFree);
                xSemaphoreGive(dtuMutex);
                dtuMutexHeld = false;
                vTaskDelay(pdMS_TO_TICKS(3000));
                ESP.restart();
                while (true)
                {
                    vTaskDelay(portMAX_DELAY);
                }
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

    return finishFailedSession(otaHandle, otaHandleOpen, dtuMutex,
                               dtuMutexHeld, dtuPort, businessPaused,
                               failureReason, bytesWritten, otaTotalSize,
                               sessionMinimumFree);
}

void otaListenerTask(void *pvParameters)
{
    (void)pvParameters;
    LOG_INFO("Remote OTA single task started: stack=%u idle_priority=%u active_priority=%u",
             static_cast<unsigned>(OTA_TASK_STACK_SIZE),
             static_cast<unsigned>(otaIdlePriority),
             static_cast<unsigned>(otaActivePriority));
    logOtaMemory("listener_started");

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

        int requestedSize = 0;
        {
            config_json uploadJson(request->arguments.c_str());
            requestedSize =
                uploadJson.isValid() ? uploadJson.getInt("size", 0) : 0;
        }

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

        vTaskPrioritySet(nullptr, otaActivePriority);
        runOtaSession(static_cast<size_t>(requestedSize));
        otaActive.store(false, std::memory_order_release);
        vTaskPrioritySet(nullptr, otaIdlePriority);
        LOG_INFO("Remote OTA listener resumed after failed session");
        logOtaMemory("listener_resumed");
    }
}
} // namespace

namespace RemoteOtaManager
{
bool begin(UBaseType_t listenerPriority, UBaseType_t activePriority)
{
    if (listenerHandle != nullptr)
    {
        return true;
    }

    otaIdlePriority = listenerPriority;
    otaActivePriority = activePriority;
    BaseType_t result = xTaskCreatePinnedToCore(
        otaListenerTask,
        "OtaUploadTask",
        OTA_TASK_STACK_SIZE,
        nullptr,
        otaIdlePriority,
        &listenerHandle,
        1);

    if (result != pdPASS)
    {
        listenerHandle = nullptr;
        LOG_ERROR("Remote OTA task creation failed: free=%u largest=%u",
                  static_cast<unsigned>(freeInternalHeap()),
                  static_cast<unsigned>(largestInternalBlock()));
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
