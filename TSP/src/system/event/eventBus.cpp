#include "eventBus.h"
#include <algorithm>
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
        // 队列满，清理旧消息后重新发送
        EventMsg tempMsg;
        while (xQueueReceive(internalQueue, &tempMsg, 0) == pdTRUE) {
            // 清空所有旧消息
        }
        xQueueReset(internalQueue);
        // 重新发送新消息
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

EventBus::EventBus() {
    busMutex = xSemaphoreCreateMutex();
}

EventBus& EventBus::getInstance() {
    static EventBus instance;
    return instance;
}

QueueHandle_t EventBus::createReceiverQueue(uint32_t queueDepth) {
    return xQueueCreate(queueDepth, sizeof(EventMsg));
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
void EventBus::publish(EventID id, void* data) {
    EventMsg msg = {id, data};
    if (xSemaphoreTake(busMutex, portMAX_DELAY)) {
        if (subscribers.find(id) != subscribers.end()) {
            for (QueueHandle_t q : subscribers[id]) {
                if (xQueueSend(q, &msg, 0) != pdTRUE) {
                    EventMsg tempMsg;
                    while (xQueueReceive(q, &tempMsg, 0) == pdTRUE) {
                    }
                    xQueueReset(q);
                    xQueueSend(q, &msg, pdMS_TO_TICKS(100));
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