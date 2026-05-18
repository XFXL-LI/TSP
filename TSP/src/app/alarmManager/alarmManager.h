#ifndef ALARM_MANAGER_H
#define ALARM_MANAGER_H
#include <Arduino.h>
#include "../../inc/sys_init.h"

class alarmManager{
private:
    SystemRuntimeStatus* sys_status = nullptr;
    QueueHandle_t alarmQueue = nullptr;;

    CHECK_RESULT checkHardware(void);       // ¼ì²â¸÷¸ö´«¸ÐÆ÷
    CHECK_RESULT checkDTU_HJ212(void);      // ¼ì²â212DTU
    CHECK_RESULT checkDTU_REMOTE(void);     // ¼ì²âÔ¶³ÌDTU
    CHECK_RESULT checkNetwork(void);        // ¼ì²âÍøÂç
    CHECK_RESULT checkSysTime(void);        // ¼ì²âÏµÍ³Ê±¼ä
    CHECK_RESULT checStorageInit(void);     // ¼ì²â´æ´¢

    alarmManager();
    ~alarmManager();
public:
    static alarmManager& getInstance();
    void printRestartInfo(void);
    void init(void);
    void poll(void);
};

#endif