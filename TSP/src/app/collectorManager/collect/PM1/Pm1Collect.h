#pragma once
#ifndef PM1_COLLECT_H
#define PM1_COLLECT_H

#include "../../../../module/modbus/modbus_manager.h"
#include "../../../../system/call/base_call.h"
#include "../../../../inc/sys_init.h"

class Pm1Collect : public BaseCollector {
private:
    Pm1Collect();
    virtual ~Pm1Collect();
    Pm1Collect(const Pm1Collect&) = delete;
    Pm1Collect& operator=(const Pm1Collect&) = delete;
    static Pm1Collect* _instance;

protected:
    String _id;
    String _port_name;
    
    uint8_t _slaveId;
    uint16_t _regAddr;
    uint8_t _regCount;
    float _factor;
    Stream *_port;
    modbus_manager* _mb_manager;
    float unitFactor;
    String rawUnit;

public:
    static Pm1Collect& getInstance();
    bool begin() override;
    bool modbusInit(Stream* new_port, String id, String port_name, float factor, String unit) override;
    bool gal(int increment, int ratio) override;
    String getID() const override;
    virtual DataPacket* collect() override;

    void setModbusConfig(uint8_t slave, uint16_t reg, uint8_t count, float factor);
    void setFactor(float factor);
    bool updatePort(Stream* new_port);
    void setIdentity(String id, int group);
};


#endif