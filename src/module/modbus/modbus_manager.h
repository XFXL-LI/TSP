#pragma once
#ifndef MODBUS_COM_H
#define MODBUS_COM_H
#include <stdint.h>
#include <HardwareSerial.h>
#include <SoftwareSerial.h>
#include <ModbusRTU.h>

class modbus_manager
{
private:
    ModbusRTU* _mb_instances;;
    Stream* _serial_ports;

public:

    modbus_manager();
    ~modbus_manager();

    modbus_manager(const modbus_manager&) = delete;
    modbus_manager& operator=(const modbus_manager&) = delete;

    void modbus_init(Stream *current_port);
    uint16_t readModbusReg(uint8_t slave, uint16_t addr);
    bool writeModbusReg(uint8_t slaveId, uint16_t startAddr, uint32_t writeValue);

    bool readModbusRegs(uint8_t slaveId, uint16_t startAddr, uint16_t count, uint16_t* destBuffer);
    bool writeModbusRegs(uint8_t slaveId, uint16_t startAddr, uint16_t count, uint16_t* dataBuffer);
};

#endif
