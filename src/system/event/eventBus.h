#ifndef EVENTBUS_H
#define EVENTBUS_H

#include <Arduino.h>
#include <vector>
#include <map>
#include <functional>
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

enum class EventID {
    RAW_DATA_COLLECTED,     // 原始数据
    SERIAL_DATA,
    PROCESSED_DATA_COLLECTED,
    ALARM_TRIGGERED,
    NETWORK_RESTORED,
    RESUME_DATA,

    CONFIG_SET_REQ,
    CONFIG_SET_RES,
    DATA_QUERY_REQ,
    DATA_QUERY_RES,
    CONFIG_QUERY_REQ,
    CONFIG_QUERY_RES,
    RECORD_QUERY_REQ,
    RECORD_QUERY_RES,
    GAL_REQ,
    GAL_RES,
    UPLOAD_REQ,
    UPLOAD_RES,

    DTU_COMMAND_REQ,
    DTU_COMMAND_RES,
};

struct EventMsg {
    EventID id;
    void* data;
};

// 定义回调函数类型
using EventCallback = std::function<void(void*)>;

class EventCall {
private:
    std::map<EventID, std::vector<EventCallback>> observers;
    
    QueueHandle_t internalQueue;
    SemaphoreHandle_t busMutex; 

    EventCall();
    ~EventCall();
    
    EventCall(const EventCall&) = delete;
    EventCall& operator=(const EventCall&) = delete;

    static void eventLoopTask(void* pvParameters);

public:
    static EventCall& getInstance();

    void subscribe(EventID id, EventCallback callback);

    void publish(EventID id, void* data = nullptr);
    
    void begin(uint32_t queueDepth = 20, uint32_t stackSize = 3072);
};

class EventBus {
private:
    // 核心存储：EventID 对应 一组订阅者的队列句柄
    std::map<EventID, std::vector<QueueHandle_t>> subscribers;
    SemaphoreHandle_t busMutex;
    std::atomic<uint32_t> droppedMessages;

    EventBus(); // 构造函数中初始化信号量
    ~EventBus() = default;

public:
    static EventBus& getInstance();

    EventBus(const EventBus&) = delete;
    EventBus& operator=(const EventBus&) = delete;

    QueueHandle_t createReceiverQueue(uint32_t queueDepth = 10);

    int getSubscriberCount(EventID id);
    uint32_t getDroppedMessageCount() const;
    static void clearQueue(QueueHandle_t queue);
    
    void subscribe(EventID id, QueueHandle_t receiverQueue);
    void unsubscribe(EventID id, QueueHandle_t receiverQueue);

    void publish(EventID id, void* data);

    static bool waitEvent(QueueHandle_t queue, EventMsg& outMsg, uint32_t timeoutMs = portMAX_DELAY);
};

#endif
