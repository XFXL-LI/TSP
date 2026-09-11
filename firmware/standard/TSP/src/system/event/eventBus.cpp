#include "eventBus.h"
#include <algorithm>
#include "../../module/log/log_manager.h"
EventCall::EventCall() : internalQueue(NULL) {
    busMutex = xSemaphoreCreateMutex();
}

EventCall& EventCall::getInstance() {
    static EventCall instance;
    return instance;
}

void EventCall::begin(uint32_t queueDepth, uint32_t stackSize) {
    if (internalQueue == NULL) {
        internalQueue = xQueueCreate(queueDepth, sizeof(EventMsg));
        xTaskCreate(eventLoopTask, "EB_Loop", stackSize, this, 5, NULL);
    }
}

void EventCall::subscribe(EventID id, EventCallback callback) {
    if (xSemaphoreTake(busMutex, portMAX_DELAY)) {
        observers[id].push_back(callback);
        xSemaphoreGive(busMutex);
    }
}
void EventBus::unsubscribe(EventID id, QueueHandle_t receiverQueue) {
    if (!receiverQueue) return;

    if (xSemaphoreTake(busMutex, portMAX_DELAY)) {
        if (subscribers.find(id) != subscribers.end()) {
            auto& queueList = subscribers[id];
            auto it = std::find(queueList.begin(), queueList.end(), receiverQueue);
            if (it != queueList.end()) {
                queueList.erase(it);
                if (queueList.empty()) {
                    subscribers.erase(id);
                }
            }
        }
        xSemaphoreGive(busMutex);
    }
}
void EventCall::publish(EventID id, void* data) {
    EventMsg msg = {id, data};
    if (xQueueSend(internalQueue, &msg, 0) != pdTRUE) {
        // Queue full: clear stale messages before retrying.
        EventMsg tempMsg;
        while (xQueueReceive(internalQueue, &tempMsg, 0) == pdTRUE) {
            // Drain every stale message.
        }
        xQueueReset(internalQueue);
        // Retry the newest message.
        xQueueSend(internalQueue, &msg, pdMS_TO_TICKS(100));
    }
}

void EventCall::eventLoopTask(void* pvParameters) {
    EventCall* bus = (EventCall*)pvParameters;
    EventMsg receivedMsg;

    while (true) {
        if (xQueueReceive(bus->internalQueue, &receivedMsg, portMAX_DELAY)) {
            if (xSemaphoreTake(bus->busMutex, portMAX_DELAY)) {
                
                if (bus->observers.count(receivedMsg.id)) {
                    for (auto& callback : bus->observers[receivedMsg.id]) {
                        callback(receivedMsg.data);
                    }
                }
                
                xSemaphoreGive(bus->busMutex);
            }
        }
    }
}

EventBus::EventBus() : droppedMessages(0) {
    busMutex = xSemaphoreCreateMutex();
}

EventBus& EventBus::getInstance() {
    static EventBus instance;
    return instance;
}

QueueHandle_t EventBus::createReceiverQueue(uint32_t queueDepth, const char* queueName) {
    QueueHandle_t queue = xQueueCreate(queueDepth, sizeof(EventMsg));
    // Firmware 2.0.2 (2026-07-30): PermissionSystem creates and deletes a
    // response queue for every LCD/DTU request. Registering those unnamed
    // handles permanently in queueNames leaked one std::map node per request.
    if (queue != nullptr && queueName != nullptr && queueName[0] != '\0') {
        if (xSemaphoreTake(busMutex, portMAX_DELAY)) {
            queueNames[queue] = String(queueName);
            xSemaphoreGive(busMutex);
        }
    }
    return queue;
}
void EventBus::clearQueue(QueueHandle_t queue) {
    if (queue == NULL) return;
    EventMsg msg;
    while (xQueueReceive(queue, &msg, 0) == pdTRUE) {
        if (msg.data != nullptr) {
        }
    }
    xQueueReset(queue);
}
void EventBus::subscribe(EventID id, QueueHandle_t receiverQueue) {
    if (!receiverQueue) return;

    if (xSemaphoreTake(busMutex, portMAX_DELAY)) {
        subscribers[id].push_back(receiverQueue);
        xSemaphoreGive(busMutex);
    }
}
int EventBus::getSubscriberCount(EventID id) {
    int count = 0;
    if (xSemaphoreTake(busMutex, portMAX_DELAY)) {
        if (subscribers.find(id) != subscribers.end()) {
            count = (int)subscribers[id].size();
        }
        xSemaphoreGive(busMutex);
    }
    return count;
}
uint32_t EventBus::getDroppedMessageCount() const {
    return droppedMessages.load(std::memory_order_relaxed);
}
void EventBus::publish(EventID id, void* data) {
    EventMsg msg = {id, data};
    if (xSemaphoreTake(busMutex, portMAX_DELAY)) {
        if (subscribers.find(id) != subscribers.end()) {
            for (QueueHandle_t q : subscribers[id]) {
                // Avoid allocating a temporary String for every published
                // event. Only long-lived, explicitly named queues are stored.
                const char* queueName = "unnamed";
                auto nameIt = queueNames.find(q);
                if (nameIt != queueNames.end()) {
                    queueName = nameIt->second.c_str();
                }

                // Firmware 2.0.1 (2026-07-30):
                // Processed monitoring data must not use the legacy
                // "queue full -> clear every old message" policy. Apply
                // backpressure so REAL/MIN/HOUR/DAY packets keep their order.
                if (id == EventID::PROCESSED_DATA_COLLECTED) {
                    UBaseType_t waiting = uxQueueMessagesWaiting(q);
                    UBaseType_t spaces = uxQueueSpacesAvailable(q);
                    if (spaces <= 2) {
                        LOG_WARNING("[DIAG] QUEUE_PRESSURE event=%d queue=%s waiting=%u spaces=%u",
                                    (int)id,
                                    queueName,
                                    (unsigned)waiting,
                                    (unsigned)spaces);
                    }
                    xQueueSend(q, &msg, portMAX_DELAY);
                    continue;
                }

                if (xQueueSend(q, &msg, 0) != pdTRUE) {
                    EventMsg tempMsg;
                    uint32_t droppedNow = 0;
                    while (xQueueReceive(q, &tempMsg, 0) == pdTRUE) {
                        ++droppedNow;
                    }
                    xQueueReset(q);
                    uint32_t total = droppedMessages.fetch_add(
                        droppedNow, std::memory_order_relaxed) + droppedNow;
                    BaseType_t resent = xQueueSend(q, &msg, pdMS_TO_TICKS(100));
                    LOG_ERROR("[DIAG] EVENT_DROP event=%d queue=%s dropped=%u total=%u resent=%d handle=%p",
                              (int)id,
                              queueName,
                              (unsigned)droppedNow,
                              (unsigned)total,
                              resent == pdTRUE ? 1 : 0,
                              q);
                }
            }
        }
        xSemaphoreGive(busMutex);
    }
}

bool EventBus::waitEvent(QueueHandle_t queue, EventMsg& outMsg, uint32_t timeoutMs) {
    if (!queue) return false;
    TickType_t ticks = (timeoutMs == portMAX_DELAY) ? portMAX_DELAY : pdMS_TO_TICKS(timeoutMs);
    return xQueueReceive(queue, &outMsg, ticks) == pdTRUE;
}
