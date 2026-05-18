#include "alarmManager.h"
#include "../../inc/sys_init.h"
#include "../../module/log/log_manager.h"
#include "../../module/file/file_storage.h"
#include "../../module/dtu/dtu_driver.h"
#include "../../system/event/eventBus.h"

#include <rom/rtc.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

alarmManager::alarmManager() {
    sys_status = new SystemRuntimeStatus(); 
    sys_status->systemErrorInfo = CHECK_RESULT::DEV_OK;
}

alarmManager::~alarmManager() {
}

// 你的单例实现
alarmManager& alarmManager::getInstance() {
    static alarmManager instance;
    return instance;
}

void alarmManager::printRestartInfo(void)
{
    RESET_REASON reason = rtc_get_reset_reason(0);
    switch (reason)
    {
    case POWERON_RESET:
        sys_status->last_reason = RESTART_REASON::POWERON_RESET;
        LOG_DEBUG("Power on and restart (normal startup)");
        break;
    case RTCWDT_RTC_RESET:
        sys_status->last_reason = RESTART_REASON::RTCWDT_RTC_RESET;
        LOG_DEBUG("RTC watchdog reset (deep sleep anomaly)");
        break;
    case DEEPSLEEP_RESET:
        sys_status->last_reason = RESTART_REASON::DEEPSLEEP_RESET;
        LOG_DEBUG("Wake up and restart (normal sleep ends)");
        break;
    case TG1WDT_CPU_RESET:
        sys_status->last_reason = RESTART_REASON::TG1WDT_CPU_RESET;
        LOG_DEBUG("Task watchdog reset (code stuck)");
        break;
    case RTC_SW_CPU_RESET:
        sys_status->last_reason = RESTART_REASON::RTC_SW_CPU_RESET;
        LOG_DEBUG("Software restart (code call restart)");
        break;
    default:
        LOG_DEBUG("Restart for other reasons");
    }
}
void alarmManager::init(void) {
    if (alarmQueue == nullptr) {
        alarmQueue = EventBus::getInstance().createReceiverQueue(5);
        EventBus::getInstance().subscribe(EventID::ALARM_TRIGGERED, alarmQueue);
    }
}

void alarmManager::poll(void) {
    if (alarmQueue == nullptr) return;

    EventMsg msg;

    if (EventBus::waitEvent(alarmQueue, msg, pdMS_TO_TICKS(1000))) 
    {
        if (msg.id == EventID::ALARM_TRIGGERED) 
        {
            SystemRuntimeStatus *status = (SystemRuntimeStatus *)msg.data;
            if (status != nullptr) {
                sys_status->systemErrorInfo = status->systemErrorInfo;
                sys_status->errorInfo = status->errorInfo;
                LOG_ERROR("Alarm triggered! System error info: %d", status->systemErrorInfo);
            }
        }
    }
}