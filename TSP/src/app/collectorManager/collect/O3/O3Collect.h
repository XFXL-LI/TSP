#pragma once
#ifndef O3_COLLECT_H
#define O3_COLLECT_H

#include "../../../../module/modbus/modbus_manager.h"
#include "../../../../system/call/base_call.h"
#include "../../../../inc/sys_init.h"

class O3Collect : public BaseCollector {
private:
    O3Collect();
    virtual ~O3Collect();
    O3Collect(const O3Collect&) = delete;
    O3Collect& operator=(const O3Collect&) = delete;
    static O3Collect* _instance;

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
    static O3Collect& getInstance();
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