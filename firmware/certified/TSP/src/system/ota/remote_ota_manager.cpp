#include "remote_ota_manager.h"

#include <Arduino.h>
#include <atomic>
#include <cstring>
#include <esp_err.h>
#include <esp_heap_caps.h>
#include <esp_ota_ops.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "../../inc/sys_init.h"
#include "../../app/configManager/config.h"
#include "../../app/dataManager/dataManager.h"
#include "../../app/dtuManager/dtuManager.h"
#include "../../module/Serial/SerialManager.h"
#include "../../module/json/config_json.h"
#include "../../module/log/log_manager.h"
#include "../../module/pack212/pack212.h"
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
constexpr uint32_t LCD_OTA_ACK_TIMEOUT_MS = 2000;
constexpr uint32_t LCD_OTA_FAILED_DISPLAY_MS = 3000;
constexpr uint32_t LCD_OTA_NORMAL_RETRY_INTERVAL_MS = 2000;
constexpr uint32_t LCD_OTA_NORMAL_RETRY_TIMEOUT_MS = 30000;
constexpr uint8_t LCD_OTA_PROTOCOL_VERSION = 1;
constexpr uint8_t LCD_OTA_PROGRESS_STEP = 5;
constexpr size_t LCD_OTA_MESSAGE_SIZE = 256;
constexpr uint8_t REMOTE_OTA_PROTOCOL_VERSION = 1;
constexpr uint8_t REMOTE_OTA_PROGRESS_STEP = 5;
constexpr size_t REMOTE_OTA_MESSAGE_SIZE = 256;
constexpr size_t REMOTE_OTA_SENDER_CHUNK_SIZE = 800;
constexpr uint32_t REMOTE_OTA_SENDER_INTERVAL_MS = 1000;
constexpr uint32_t REMOTE_OTA_RESTART_DELAY_MS = 3000;

enum class UploadProtocolMode : uint8_t
{
    Legacy = 0,
    ProtocolV1
};

// Firmware 2.0.6: this is the only OTA task. It is created during startup,
// before the long-running workload fragments the heap, and performs both
// request listening and flash writing. The receive buffer is static so it
// does not consume task stack during a session.
constexpr uint32_t OTA_TASK_STACK_SIZE = 6 * 1024;
uint8_t otaDataBuffer[OTA_BUFFER_SIZE];

std::atomic<bool> otaActive(false);
std::atomic<uint8_t> lcdMode(
    static_cast<uint8_t>(RemoteOtaManager::LcdInputMode::Normal));
std::atomic<uint32_t> lcdModeEpoch(1);
std::atomic<uint32_t> lcdSessionCounter(0);
std::atomic<uint32_t> currentLcdSession(0);
std::atomic<uint32_t> acknowledgedLcdSession(0);
std::atomic<bool> lcdNormalRetryActive(false);
std::atomic<bool> lcdNormalAckReceived(false);
std::atomic<bool> lcdNormalSendInProgress(false);
std::atomic<uint32_t> lcdNormalRetryStartedAt(0);
std::atomic<uint32_t> lcdNormalNextSendAt(0);
std::atomic<uint32_t> remoteSessionCounter(0);
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

void sendHj212MaintenanceSnapshot()
{
    GLOBALCONFIG &globalConfig = ConfigManager::getInstance().getConfig();
    if (!globalConfig.systemSwitch.enable_hj212) {
        LOG_INFO("[DIAG] OTA_M_STATUS skipped=hj212_disabled");
        return;
    }

    SYSTEM_SETUP setup = ConfigManager::getInstance().getSetup();
    if (setup.netCsq < 0 || setup.netCsq > 31) {
        LOG_WARNING("[DIAG] OTA_M_STATUS skipped=network_unavailable csq=%d",
                    setup.netCsq);
        return;
    }

    AllProcessedDataPacket *snapshot =
        DataManager::getInstance().createStatusSnapshot(DataStatus::MAINTENANCE);
    if (snapshot == nullptr || snapshot->processed_data_map.empty()) {
        if (snapshot != nullptr) snapshot->release();
        LOG_WARNING("[DIAG] OTA_M_STATUS skipped=no_valid_snapshot");
        return;
    }

    HJ212_DataCenter builder;
    const HJ212CONFIG config = globalConfig.hj212;
    const String packet = config.protocol_version == "2025"
        ? builder.build2025Hj212Packet(snapshot, config)
        : builder.build2017Hj212Packet(snapshot, config);
    const bool sent = packet.length() > 0 &&
        DTUManager::getInstance().sendHJ212Packet(
            packet, 1, snapshot->trace_id,
            static_cast<int>(snapshot->dataTime), snapshot->last_update);
    LOG_INFO("[DIAG] OTA_M_STATUS sent=%d factors=%u bytes=%u",
             sent ? 1 : 0,
             static_cast<unsigned>(snapshot->processed_data_map.size()),
             static_cast<unsigned>(packet.length()));
    snapshot->release();
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

uint32_t nextRemoteSession()
{
    uint32_t session =
        remoteSessionCounter.fetch_add(1, std::memory_order_acq_rel) + 1;
    if (session == 0)
    {
        session =
            remoteSessionCounter.fetch_add(1, std::memory_order_acq_rel) + 1;
    }
    return session;
}

bool sendRemoteOtaStatus(Stream *port,
                         bool dtuMutexHeld,
                         uint32_t session,
                         const char *state,
                         size_t size = 0,
                         size_t written = 0,
                         size_t total = 0,
                         uint8_t progress = 0,
                         const char *reason = nullptr)
{
    if (state == nullptr)
    {
        return false;
    }

    char message[REMOTE_OTA_MESSAGE_SIZE] = {};
    int length = 0;
    if (strcmp(state, "rejected") == 0)
    {
        length = snprintf(
            message, sizeof(message),
            "{\"operation\":\"upload_status\",\"protocol\":%u,\"session\":0,"
            "\"state\":\"rejected\",\"reason\":\"%s\",\"size\":%u}",
            static_cast<unsigned>(REMOTE_OTA_PROTOCOL_VERSION),
            reason == nullptr ? "internal_error" : reason,
            static_cast<unsigned>(size));
    }
    else if (strcmp(state, "accepted") == 0)
    {
        length = snprintf(
            message, sizeof(message),
            "{\"operation\":\"upload_status\",\"protocol\":%u,\"session\":%u,"
            "\"state\":\"accepted\",\"size\":%u}",
            static_cast<unsigned>(REMOTE_OTA_PROTOCOL_VERSION),
            static_cast<unsigned>(session),
            static_cast<unsigned>(size));
    }
    else if (strcmp(state, "ready") == 0)
    {
        length = snprintf(
            message, sizeof(message),
            "{\"operation\":\"upload_status\",\"protocol\":%u,\"session\":%u,"
            "\"state\":\"ready\",\"size\":%u,\"chunk_size\":%u,"
            "\"interval_ms\":%u,\"inactivity_timeout_ms\":%u}",
            static_cast<unsigned>(REMOTE_OTA_PROTOCOL_VERSION),
            static_cast<unsigned>(session),
            static_cast<unsigned>(size),
            static_cast<unsigned>(REMOTE_OTA_SENDER_CHUNK_SIZE),
            static_cast<unsigned>(REMOTE_OTA_SENDER_INTERVAL_MS),
            static_cast<unsigned>(OTA_INACTIVITY_TIMEOUT_MS));
    }
    else if (strcmp(state, "success") == 0)
    {
        length = snprintf(
            message, sizeof(message),
            "{\"operation\":\"upload_status\",\"protocol\":%u,\"session\":%u,"
            "\"state\":\"success\",\"written\":%u,\"total\":%u,"
            "\"progress\":100,\"restarting_in_ms\":%u}",
            static_cast<unsigned>(REMOTE_OTA_PROTOCOL_VERSION),
            static_cast<unsigned>(session),
            static_cast<unsigned>(written),
            static_cast<unsigned>(total),
            static_cast<unsigned>(REMOTE_OTA_RESTART_DELAY_MS));
    }
    else if (strcmp(state, "failed") == 0)
    {
        length = snprintf(
            message, sizeof(message),
            "{\"operation\":\"upload_status\",\"protocol\":%u,\"session\":%u,"
            "\"state\":\"failed\",\"reason\":\"%s\",\"written\":%u,"
            "\"total\":%u,\"progress\":%u}",
            static_cast<unsigned>(REMOTE_OTA_PROTOCOL_VERSION),
            static_cast<unsigned>(session),
            reason == nullptr ? "internal_error" : reason,
            static_cast<unsigned>(written),
            static_cast<unsigned>(total),
            static_cast<unsigned>(progress));
    }
    else
    {
        length = snprintf(
            message, sizeof(message),
            "{\"operation\":\"upload_status\",\"protocol\":%u,\"session\":%u,"
            "\"state\":\"%s\",\"written\":%u,\"total\":%u,\"progress\":%u}",
            static_cast<unsigned>(REMOTE_OTA_PROTOCOL_VERSION),
            static_cast<unsigned>(session), state,
            static_cast<unsigned>(written),
            static_cast<unsigned>(total),
            static_cast<unsigned>(progress));
    }

    if (length <= 0 || static_cast<size_t>(length) >= sizeof(message))
    {
        LOG_ERROR("[DIAG] REMOTE_OTA_STATUS_BUILD_FAILED state=%s session=%u",
                  state, static_cast<unsigned>(session));
        return false;
    }

    size_t sent = 0;
    if (dtuMutexHeld && port != nullptr)
    {
        sent += port->write(reinterpret_cast<const uint8_t *>(message),
                            static_cast<size_t>(length));
        static const uint8_t newline[] = {'\r', '\n'};
        sent += port->write(newline, sizeof(newline));
        port->flush();
    }
    else
    {
        sent = SerialManager::getInstance().println(SERIAL_DTU, message);
    }

    bool success = sent == static_cast<size_t>(length) + 2;
    if (success)
    {
        LOG_INFO("[DIAG] REMOTE_OTA_STATUS state=%s session=%u progress=%u sent=1",
                 state, static_cast<unsigned>(session),
                 static_cast<unsigned>(progress));
    }
    else
    {
        LOG_WARNING(
            "[DIAG] REMOTE_OTA_STATUS_SEND_FAILED state=%s session=%u written=%u expected=%u",
            state, static_cast<unsigned>(session),
            static_cast<unsigned>(sent),
            static_cast<unsigned>(length + 2));
    }
    return success;
}

void setLcdInputMode(RemoteOtaManager::LcdInputMode mode)
{
    lcdMode.store(static_cast<uint8_t>(mode), std::memory_order_release);
    lcdModeEpoch.fetch_add(1, std::memory_order_acq_rel);
}

bool sendLcdOtaStatus(uint32_t session,
                      const char *state,
                      uint8_t progress,
                      size_t total = 0,
                      const char *reason = nullptr,
                      const char *version = nullptr)
{
    char message[LCD_OTA_MESSAGE_SIZE] = {};
    int length = 0;
    if (reason != nullptr)
    {
        length = snprintf(
            message, sizeof(message),
            "{\"operation\":\"ota_status\",\"protocol\":%u,\"session\":%u,"
            "\"state\":\"%s\",\"progress\":%u,\"reason\":\"%s\"}",
            static_cast<unsigned>(LCD_OTA_PROTOCOL_VERSION),
            static_cast<unsigned>(session), state,
            static_cast<unsigned>(progress), reason);
    }
    else if (version != nullptr)
    {
        length = snprintf(
            message, sizeof(message),
            "{\"operation\":\"ota_status\",\"protocol\":%u,\"session\":%u,"
            "\"state\":\"%s\",\"progress\":%u,\"version\":\"%s\"}",
            static_cast<unsigned>(LCD_OTA_PROTOCOL_VERSION),
            static_cast<unsigned>(session), state,
            static_cast<unsigned>(progress), version);
    }
    else if (total > 0)
    {
        length = snprintf(
            message, sizeof(message),
            "{\"operation\":\"ota_status\",\"protocol\":%u,\"session\":%u,"
            "\"state\":\"%s\",\"progress\":%u,\"total\":%u}",
            static_cast<unsigned>(LCD_OTA_PROTOCOL_VERSION),
            static_cast<unsigned>(session), state,
            static_cast<unsigned>(progress),
            static_cast<unsigned>(total));
    }
    else
    {
        length = snprintf(
            message, sizeof(message),
            "{\"operation\":\"ota_status\",\"protocol\":%u,\"session\":%u,"
            "\"state\":\"%s\",\"progress\":%u}",
            static_cast<unsigned>(LCD_OTA_PROTOCOL_VERSION),
            static_cast<unsigned>(session), state,
            static_cast<unsigned>(progress));
    }

    if (length <= 0 || static_cast<size_t>(length) >= sizeof(message))
    {
        LOG_ERROR("[DIAG] LCD_OTA_STATUS_BUILD_FAILED state=%s session=%u",
                  state, static_cast<unsigned>(session));
        return false;
    }

    size_t written =
        SerialManager::getInstance().println(SERIAL_LCD, message);
    bool sent = written == static_cast<size_t>(length) + 2;
    if (sent)
    {
        LOG_INFO("[DIAG] LCD_OTA_STATUS state=%s session=%u progress=%u sent=1",
                 state, static_cast<unsigned>(session),
                 static_cast<unsigned>(progress));
    }
    else
    {
        LOG_WARNING(
            "[DIAG] LCD_OTA_STATUS_SEND_FAILED state=%s session=%u progress=%u written=%u expected=%u",
            state, static_cast<unsigned>(session),
            static_cast<unsigned>(progress),
            static_cast<unsigned>(written),
            static_cast<unsigned>(length + 2));
    }
    return sent;
}

bool sendLcdNormalIfCurrent()
{
    bool expected = false;
    if (!lcdNormalSendInProgress.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel))
    {
        return false;
    }

    bool shouldSend =
        lcdNormalRetryActive.load(std::memory_order_acquire) &&
        !lcdNormalAckReceived.load(std::memory_order_acquire) &&
        static_cast<RemoteOtaManager::LcdInputMode>(
            lcdMode.load(std::memory_order_acquire)) ==
            RemoteOtaManager::LcdInputMode::Normal;
    bool sent = shouldSend &&
                sendLcdOtaStatus(0, "normal", 0, 0, nullptr, VERSION2);
    lcdNormalSendInProgress.store(false, std::memory_order_release);
    return sent;
}

uint32_t beginLcdOtaSession(size_t otaTotalSize)
{
    uint32_t session =
        lcdSessionCounter.fetch_add(1, std::memory_order_acq_rel) + 1;
    if (session == 0)
    {
        session =
            lcdSessionCounter.fetch_add(1, std::memory_order_acq_rel) + 1;
    }

    // A new OTA session supersedes any boot/failure normal recovery retry.
    // Otherwise a scheduled normal frame could incorrectly unlock the LCD
    // after preparing has already started.
    lcdNormalRetryActive.store(false, std::memory_order_release);
    lcdNormalAckReceived.store(false, std::memory_order_release);
    while (lcdNormalSendInProgress.load(std::memory_order_acquire))
    {
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    currentLcdSession.store(session, std::memory_order_release);
    acknowledgedLcdSession.store(0, std::memory_order_release);
    setLcdInputMode(RemoteOtaManager::LcdInputMode::WaitAck);
    sendLcdOtaStatus(session, "preparing", 0, otaTotalSize);

    const uint32_t startedAt = millis();
    while (millis() - startedAt < LCD_OTA_ACK_TIMEOUT_MS)
    {
        if (acknowledgedLcdSession.load(std::memory_order_acquire) == session)
        {
            LOG_INFO("[DIAG] LCD_OTA_ACK session=%u result=ok",
                     static_cast<unsigned>(session));
            setLcdInputMode(RemoteOtaManager::LcdInputMode::Drain);
            return session;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    LOG_WARNING("[DIAG] LCD_OTA_ACK session=%u result=lcd_ack_timeout",
                static_cast<unsigned>(session));
    setLcdInputMode(RemoteOtaManager::LcdInputMode::Drain);
    return session;
}

void finishLcdFailedSession(const char *reason, uint8_t progress)
{
    uint32_t session =
        currentLcdSession.load(std::memory_order_acquire);
    if (session == 0)
    {
        return;
    }

    sendLcdOtaStatus(session, "failed", progress, 0, reason);
    vTaskDelay(pdMS_TO_TICKS(LCD_OTA_FAILED_DISPLAY_MS));
    RemoteOtaManager::notifyLcdNormal();
}

uint8_t calculateOtaProgress(size_t bytesWritten, size_t otaTotalSize)
{
    if (otaTotalSize == 0)
    {
        return 0;
    }
    size_t progress = (bytesWritten * 100U) / otaTotalSize;
    return static_cast<uint8_t>(progress > 100U ? 100U : progress);
}

bool finishFailedSession(esp_ota_handle_t otaHandle,
                         bool otaHandleOpen,
                         SemaphoreHandle_t dtuMutex,
                         bool dtuMutexHeld,
                         Stream *dtuPort,
                         bool businessPaused,
                         UploadProtocolMode uploadMode,
                         uint32_t remoteSession,
                         const char *reason,
                         size_t bytesWritten,
                         size_t otaTotalSize,
                         uint32_t sessionMinimumFree)
{
    if (otaHandleOpen)
    {
        esp_ota_abort(otaHandle);
    }

    uint8_t progress = calculateOtaProgress(bytesWritten, otaTotalSize);
    if (uploadMode == UploadProtocolMode::ProtocolV1)
    {
        sendRemoteOtaStatus(dtuPort, dtuMutexHeld, remoteSession, "failed",
                            0, bytesWritten, otaTotalSize, progress, reason);
    }
    else if (dtuPort != nullptr && dtuMutexHeld)
    {
        dtuPort->printf("OTA failed: %s. Written %u/%u bytes.\n",
                        reason,
                        static_cast<unsigned>(bytesWritten),
                        static_cast<unsigned>(otaTotalSize));
    }
    if (dtuPort != nullptr && dtuMutexHeld)
    {
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
    finishLcdFailedSession(reason, progress);
    return false;
}

bool runOtaSession(size_t otaTotalSize,
                   UploadProtocolMode uploadMode,
                   uint32_t remoteSession,
                   const esp_partition_t *updatePartition,
                   Stream *dtuPort,
                   SemaphoreHandle_t dtuMutex)
{
    logOtaMemory("request");

    // UPLOAD_RES has already released the synchronous command route. Give the
    // parser time to leave its guarded region before announcing acceptance or
    // requesting business quiescence.
    vTaskDelay(pdMS_TO_TICKS(OTA_REQUEST_SETTLE_MS));
    if (uploadMode == UploadProtocolMode::ProtocolV1)
    {
        sendRemoteOtaStatus(dtuPort, false, remoteSession, "accepted",
                            otaTotalSize);
    }

    const uint32_t lcdSession = beginLcdOtaSession(otaTotalSize);

    // Report the latest valid values once with factor status M before normal
    // business is paused. Failure is diagnostic only and never cancels OTA.
    sendHj212MaintenanceSnapshot();

    // First prevent new guarded work and wait for every active operation to
    // leave its safe region. Taking the DTU mutex only after this avoids
    // holding the mutex while an in-flight guarded command still needs it.
    if (!RemoteOtaManager::requestBusinessPause(
            OTA_BUSINESS_QUIESCE_TIMEOUT_MS))
    {
        LOG_ERROR("Remote OTA stopped: business quiesce timeout");
        if (uploadMode == UploadProtocolMode::ProtocolV1)
        {
            sendRemoteOtaStatus(dtuPort, false, remoteSession, "failed", 0,
                                0, otaTotalSize, 0,
                                "business_quiesce_timeout");
        }
        finishLcdFailedSession("business_quiesce_timeout", 0);
        return false;
    }
    bool businessPaused = true;

    if (xSemaphoreTake(dtuMutex,
                       pdMS_TO_TICKS(OTA_SERIAL_LOCK_TIMEOUT_MS)) != pdTRUE)
    {
        return finishFailedSession(0, false, dtuMutex, false, dtuPort,
                                   businessPaused, uploadMode, remoteSession,
                                   "dtu_mutex_timeout",
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
                                   dtuPort, businessPaused, uploadMode,
                                   remoteSession,
                                   "flash_begin_failed", 0, otaTotalSize,
                                   freeInternalHeap());
    }
    bool otaHandleOpen = true;
    uint32_t sessionMinimumFree = freeInternalHeap();

    drainInput(dtuPort);
    if (uploadMode == UploadProtocolMode::ProtocolV1)
    {
        sendRemoteOtaStatus(dtuPort, true, remoteSession, "ready",
                            otaTotalSize);
        sendRemoteOtaStatus(dtuPort, true, remoteSession, "transferring", 0,
                            0, otaTotalSize, 0);
    }
    else
    {
        dtuPort->printf(
            "Ready to start OTA, size: %u byte, inactivity timeout: %u seconds\n",
            static_cast<unsigned>(otaTotalSize),
            static_cast<unsigned>(OTA_INACTIVITY_TIMEOUT_MS / 1000));
        dtuPort->printf(
            "The single packet sent is %u bytes, with a sending interval of %ums\n",
            static_cast<unsigned>(REMOTE_OTA_SENDER_CHUNK_SIZE),
            static_cast<unsigned>(REMOTE_OTA_SENDER_INTERVAL_MS));
    }
    LOG_INFO("Remote OTA ready: mode=%s session=%u size=%u business_pause=confirmed",
             uploadMode == UploadProtocolMode::ProtocolV1
                 ? "protocol-v1"
                 : "legacy",
             static_cast<unsigned>(remoteSession),
             static_cast<unsigned>(otaTotalSize));
    logOtaMemory("ready", sessionMinimumFree);
    sendLcdOtaStatus(lcdSession, "transferring", 0, otaTotalSize);

    size_t bytesWritten = 0;
    size_t localBufferLength = 0;
    uint32_t lastDataTime = millis();
    const uint32_t sessionStartTime = lastDataTime;
    const char *failureReason = nullptr;
    uint8_t nextLcdProgress = LCD_OTA_PROGRESS_STEP;
    uint8_t nextRemoteProgress = REMOTE_OTA_PROGRESS_STEP;

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
                failureReason = "flash_write_failed";
                break;
            }

            bytesWritten += localBufferLength;
            if (uploadMode == UploadProtocolMode::Legacy)
            {
                dtuPort->printf("%u/%u\n",
                                static_cast<unsigned>(bytesWritten),
                                static_cast<unsigned>(otaTotalSize));
            }
            LOG_DEBUG("Received OTA data: %u/%u bytes",
                      static_cast<unsigned>(bytesWritten),
                      static_cast<unsigned>(otaTotalSize));
            localBufferLength = 0;
            updateSessionMinimum(sessionMinimumFree);

            uint8_t progress =
                calculateOtaProgress(bytesWritten, otaTotalSize);
            if (uploadMode == UploadProtocolMode::ProtocolV1 &&
                progress >= nextRemoteProgress)
            {
                uint8_t reportedProgress = static_cast<uint8_t>(
                    (progress / REMOTE_OTA_PROGRESS_STEP) *
                    REMOTE_OTA_PROGRESS_STEP);
                sendRemoteOtaStatus(dtuPort, true, remoteSession,
                                    "transferring", 0, bytesWritten,
                                    otaTotalSize, reportedProgress);
                nextRemoteProgress = static_cast<uint8_t>(
                    reportedProgress >= 100
                        ? 101
                        : reportedProgress + REMOTE_OTA_PROGRESS_STEP);
            }
            if (progress >= nextLcdProgress)
            {
                uint8_t reportedProgress = static_cast<uint8_t>(
                    (progress / LCD_OTA_PROGRESS_STEP) *
                    LCD_OTA_PROGRESS_STEP);
                sendLcdOtaStatus(lcdSession, "transferring",
                                 reportedProgress, otaTotalSize);
                nextLcdProgress = static_cast<uint8_t>(
                    reportedProgress >= 100
                        ? 101
                        : reportedProgress + LCD_OTA_PROGRESS_STEP);
            }
        }

        now = millis();
        if (now - lastDataTime > OTA_INACTIVITY_TIMEOUT_MS)
        {
            failureReason = "data_inactivity_timeout";
            break;
        }
        if (now - sessionStartTime > OTA_SESSION_TIMEOUT_MS)
        {
            failureReason = "session_timeout";
            break;
        }

        vTaskDelay(pdMS_TO_TICKS(1));
    }

    if (failureReason == nullptr && bytesWritten == otaTotalSize)
    {
        if (uploadMode == UploadProtocolMode::ProtocolV1)
        {
            sendRemoteOtaStatus(dtuPort, true, remoteSession, "verifying", 0,
                                bytesWritten, otaTotalSize, 100);
        }
        else
        {
            dtuPort->printf("OTA download complete! Finalizing...\n");
        }
        sendLcdOtaStatus(lcdSession, "verifying", 100, otaTotalSize);
        esp_err_t endResult = esp_ota_end(otaHandle);
        otaHandleOpen = false;
        updateSessionMinimum(sessionMinimumFree);
        if (endResult == ESP_OK)
        {
            esp_err_t bootResult =
                esp_ota_set_boot_partition(updatePartition);
            if (bootResult == ESP_OK)
            {
                if (uploadMode == UploadProtocolMode::ProtocolV1)
                {
                    sendRemoteOtaStatus(dtuPort, true, remoteSession,
                                        "success", 0, bytesWritten,
                                        otaTotalSize, 100);
                }
                else
                {
                    dtuPort->printf(
                        "OTA success! System restarting in 3 seconds...\n");
                }
                sendLcdOtaStatus(lcdSession, "restarting", 100, 0,
                                 nullptr, VERSION2);
                LOG_INFO("Remote OTA completed: %u bytes",
                         static_cast<unsigned>(bytesWritten));
                logOtaMemory("complete", sessionMinimumFree);
                xSemaphoreGive(dtuMutex);
                dtuMutexHeld = false;
                vTaskDelay(pdMS_TO_TICKS(REMOTE_OTA_RESTART_DELAY_MS));
                ESP.restart();
                while (true)
                {
                    vTaskDelay(portMAX_DELAY);
                }
            }

            LOG_ERROR("Remote OTA boot partition failed: %s",
                      esp_err_to_name(bootResult));
            failureReason = "boot_partition_failed";
        }
        else
        {
            LOG_ERROR("Remote OTA image validation failed: %s",
                      esp_err_to_name(endResult));
            failureReason = "image_validation_failed";
        }
    }
    else if (failureReason == nullptr)
    {
        failureReason = "received_size_mismatch";
    }

    return finishFailedSession(otaHandle, otaHandleOpen, dtuMutex,
                               dtuMutexHeld, dtuPort, businessPaused,
                               uploadMode, remoteSession, failureReason,
                               bytesWritten, otaTotalSize,
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

        bool requestJsonValid = false;
        bool protocolSpecified = false;
        bool protocolFieldValid = true;
        bool sizeFieldValid = false;
        int requestedProtocol = 0;
        int requestedSize = 0;
        {
            config_json uploadJson(request->arguments.c_str());
            requestJsonValid = uploadJson.isValid();
            if (requestJsonValid)
            {
                cJSON *root = uploadJson.getJsonObject();
                cJSON *protocolItem = cJSON_GetObjectItemCaseSensitive(
                    root, "protocol");
                protocolSpecified = protocolItem != nullptr;
                if (protocolSpecified)
                {
                    protocolFieldValid =
                        cJSON_IsNumber(protocolItem) &&
                        protocolItem->valuedouble ==
                            static_cast<double>(protocolItem->valueint);
                    requestedProtocol = protocolFieldValid
                                            ? protocolItem->valueint
                                            : -1;
                }

                cJSON *sizeItem =
                    cJSON_GetObjectItemCaseSensitive(root, "size");
                sizeFieldValid =
                    cJSON_IsNumber(sizeItem) &&
                    sizeItem->valueint > 0 &&
                    sizeItem->valuedouble ==
                        static_cast<double>(sizeItem->valueint);
                requestedSize = sizeFieldValid ? sizeItem->valueint : 0;
            }
        }

        UploadProtocolMode uploadMode =
            protocolSpecified
                ? UploadProtocolMode::ProtocolV1
                : UploadProtocolMode::Legacy;
        size_t statusSize = requestedSize > 0
                                ? static_cast<size_t>(requestedSize)
                                : 0;

        if (!requestJsonValid)
        {
            publishUploadResponse(request);
            LOG_WARNING("Remote OTA request rejected: invalid JSON");
            continue;
        }

        if (protocolSpecified &&
            (!protocolFieldValid ||
             requestedProtocol != REMOTE_OTA_PROTOCOL_VERSION))
        {
            publishUploadResponse(request);
            sendRemoteOtaStatus(nullptr, false, 0, "rejected", statusSize,
                                0, 0, 0, "unsupported_protocol");
            LOG_WARNING("Remote OTA request rejected: unsupported protocol=%d",
                        requestedProtocol);
            continue;
        }

        if (!sizeFieldValid || requestedSize <= 0)
        {
            publishUploadResponse(request);
            if (uploadMode == UploadProtocolMode::ProtocolV1)
            {
                sendRemoteOtaStatus(nullptr, false, 0, "rejected", 0,
                                    0, 0, 0, "invalid_size");
            }
            LOG_WARNING("Remote OTA request rejected: invalid size=%d",
                        requestedSize);
            continue;
        }

        const esp_partition_t *updatePartition =
            esp_ota_get_next_update_partition(nullptr);
        if (updatePartition == nullptr)
        {
            publishUploadResponse(request);
            if (uploadMode == UploadProtocolMode::ProtocolV1)
            {
                sendRemoteOtaStatus(nullptr, false, 0, "rejected", statusSize,
                                    0, 0, 0, "partition_unavailable");
            }
            LOG_WARNING("Remote OTA request rejected: partition unavailable");
            continue;
        }
        if (statusSize > updatePartition->size)
        {
            publishUploadResponse(request);
            if (uploadMode == UploadProtocolMode::ProtocolV1)
            {
                sendRemoteOtaStatus(nullptr, false, 0, "rejected", statusSize,
                                    0, 0, 0, "image_too_large");
            }
            LOG_WARNING("Remote OTA request rejected: size=%u partition=%u",
                        static_cast<unsigned>(statusSize),
                        static_cast<unsigned>(updatePartition->size));
            continue;
        }

        auto &serialManager = SerialManager::getInstance();
        Stream *dtuPort = serialManager.getStream(SERIAL_DTU);
        SemaphoreHandle_t dtuMutex = serialManager.getMutex(SERIAL_DTU);
        if (dtuPort == nullptr || dtuMutex == nullptr)
        {
            publishUploadResponse(request);
            if (uploadMode == UploadProtocolMode::ProtocolV1)
            {
                sendRemoteOtaStatus(nullptr, false, 0, "rejected", statusSize,
                                    0, 0, 0, "dtu_unavailable");
            }
            LOG_WARNING("Remote OTA request rejected: DTU unavailable");
            continue;
        }

        bool expected = false;
        if (!otaActive.compare_exchange_strong(expected, true,
                                               std::memory_order_acq_rel))
        {
            publishUploadResponse(request);
            if (uploadMode == UploadProtocolMode::ProtocolV1)
            {
                sendRemoteOtaStatus(dtuPort, false, 0, "rejected", statusSize,
                                    0, 0, 0, "session_busy");
            }
            LOG_WARNING("Remote OTA request rejected: session already active");
            continue;
        }

        uint32_t remoteSession =
            uploadMode == UploadProtocolMode::ProtocolV1
                ? nextRemoteSession()
                : 0;
        publishUploadResponse(request);
        LOG_INFO("[DIAG] REMOTE_OTA_REQUEST mode=%s session=%u size=%u",
                 uploadMode == UploadProtocolMode::ProtocolV1
                     ? "protocol-v1"
                     : "legacy",
                 static_cast<unsigned>(remoteSession),
                 static_cast<unsigned>(statusSize));
        vTaskPrioritySet(nullptr, otaActivePriority);
        runOtaSession(statusSize, uploadMode, remoteSession, updatePartition,
                      dtuPort, dtuMutex);
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

LcdInputMode lcdInputMode()
{
    return static_cast<LcdInputMode>(
        lcdMode.load(std::memory_order_acquire));
}

uint32_t lcdParserEpoch()
{
    return lcdModeEpoch.load(std::memory_order_acquire);
}

bool handleLcdOtaControlMessage(const char *json)
{
    if (json == nullptr)
    {
        return false;
    }

    config_json message(json);
    if (!message.isValid() ||
        message.getString("operation", "") != "ota_status_ack")
    {
        return false;
    }

    uint32_t receivedSession =
        static_cast<uint32_t>(message.getInt("session", 0));
    String state = message.getString("state", "");
    String code = message.getString("code", "");
    int protocol = message.getInt("protocol", 0);

    // Firmware 2.0.8: normal recovery ACK is accepted in normal input mode,
    // independently of an OTA session number. Duplicate ACKs are harmless.
    if (state == "normal")
    {
        bool validNormalAck =
            protocol == LCD_OTA_PROTOCOL_VERSION &&
            receivedSession == 0 &&
            code == "OK";
        if (!validNormalAck)
        {
            LOG_WARNING(
                "[DIAG] LCD_OTA_NORMAL_ACK result=rejected protocol=%d session=%u code=%s",
                protocol, static_cast<unsigned>(receivedSession),
                code.c_str());
            return true;
        }

        bool firstAck =
            !lcdNormalAckReceived.exchange(true, std::memory_order_acq_rel);
        lcdNormalRetryActive.store(false, std::memory_order_release);
        if (firstAck)
        {
            LOG_INFO("[DIAG] LCD_OTA_NORMAL_ACK result=ok");
        }
        else
        {
            LOG_DEBUG("[DIAG] LCD_OTA_NORMAL_ACK result=duplicate");
        }
        return true;
    }

    if (lcdInputMode() != LcdInputMode::WaitAck)
    {
        LOG_WARNING(
            "[DIAG] LCD_OTA_ACK_REJECTED reason=not_waiting state=%s session=%u",
            state.c_str(), static_cast<unsigned>(receivedSession));
        return true;
    }

    uint32_t expectedSession =
        currentLcdSession.load(std::memory_order_acquire);
    bool valid =
        protocol == LCD_OTA_PROTOCOL_VERSION &&
        receivedSession == expectedSession &&
        state == "preparing" &&
        code == "OK";

    if (!valid)
    {
        LOG_WARNING(
            "[DIAG] LCD_OTA_ACK_REJECTED expected_session=%u received_session=%u",
            static_cast<unsigned>(expectedSession),
            static_cast<unsigned>(receivedSession));
        return true;
    }

    acknowledgedLcdSession.store(receivedSession,
                                 std::memory_order_release);
    return true;
}

void notifyLcdNormal()
{
    currentLcdSession.store(0, std::memory_order_release);
    acknowledgedLcdSession.store(0, std::memory_order_release);
    setLcdInputMode(LcdInputMode::Normal);

    const uint32_t now = millis();
    lcdNormalAckReceived.store(false, std::memory_order_release);
    lcdNormalRetryStartedAt.store(now, std::memory_order_release);
    lcdNormalNextSendAt.store(
        now + LCD_OTA_NORMAL_RETRY_INTERVAL_MS,
        std::memory_order_release);
    lcdNormalRetryActive.store(true, std::memory_order_release);

    // Send the first frame immediately. Further retries are serviced by the
    // existing LCD receive task so startup and business work are not blocked.
    sendLcdNormalIfCurrent();
}

void serviceLcdNormalRetry()
{
    if (!lcdNormalRetryActive.load(std::memory_order_acquire) ||
        lcdNormalAckReceived.load(std::memory_order_acquire) ||
        lcdInputMode() != LcdInputMode::Normal)
    {
        return;
    }

    const uint32_t now = millis();
    const uint32_t startedAt =
        lcdNormalRetryStartedAt.load(std::memory_order_acquire);
    if (now - startedAt >= LCD_OTA_NORMAL_RETRY_TIMEOUT_MS)
    {
        bool expected = true;
        if (lcdNormalRetryActive.compare_exchange_strong(
                expected, false, std::memory_order_acq_rel))
        {
            LOG_WARNING(
                "[DIAG] LCD_OTA_NORMAL_ACK result=timeout elapsed_ms=%u",
                static_cast<unsigned>(now - startedAt));
        }
        return;
    }

    const uint32_t nextSendAt =
        lcdNormalNextSendAt.load(std::memory_order_acquire);
    if (static_cast<int32_t>(now - nextSendAt) < 0)
    {
        return;
    }

    lcdNormalNextSendAt.store(
        now + LCD_OTA_NORMAL_RETRY_INTERVAL_MS,
        std::memory_order_release);
    sendLcdNormalIfCurrent();
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
