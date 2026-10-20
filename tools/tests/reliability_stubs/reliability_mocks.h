#pragma once
#define MODBUS_COM_H
#define LOG_MANAGER_H
#define LOG_INFO(...) ((void)0)
#define LOG_DEBUG(...) ((void)0)
#define LOG_WARNING(...) ((void)0)
#define LOG_ERROR(...) ((void)0)
#include "Arduino.h"

class modbus_manager {
public:
    bool readModbusRegs(uint8_t, uint16_t, uint16_t, uint16_t *,
                        uint8_t, uint32_t, uint32_t);
    bool writeModbusRegs(uint8_t, uint16_t, uint16_t, uint16_t *,
                         uint8_t, uint32_t, uint32_t);
};
