#pragma once
#ifndef WINDDIR_COLLECT_H
#define WINDDIR_COLLECT_H

#include "../../../../module/modbus/modbus_manager.h"
#include "../../../../system/call/base_call.h"
#include "../../../../inc/sys_init.h"

class WindDirCollect : public BaseCollector {
private:
    WindDirCollect();
    virtual ~WindDirCollect();
    WindDirCollect(const WindDirCollect&) = delete;
    WindDirCollect& operator=(const WindDirCollect&) = delete;
    static WindDirCollect* _instance;

protected:
    String _id;
String _port_name;
    
    
    uint8_t _slaveId;
    uint16_t _regAddr;
    uint8_t _regCount;
    float _factor;
    Stream *_port;
    modbus_manager* _mb_manager;

public:
    static WindDirCollect& getInstance();
    bool begin() override;
    bool modbusInit(Stream* new_port, String id, String port_name, float factor) override;
    bool gal(int increment, int ratio) override;
    String getID() const override;
    virtual DataPacket* collect() override;

    void setModbusConfig(uint8_t slave, uint16_t reg, uint8_t count, float factor);
    void setFactor(float factor);
    bool updatePort(Stream* new_port);
    void setIdentity(String id, int group);
};


#endif