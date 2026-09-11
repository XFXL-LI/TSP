#include "TempCollect.h"
#include "../../../../inc/sys_init.h"
#include "../../../../module/log/log_manager.h"
#include "../../../../module/Serial/SerialManager.h"
#include "../../collectorManager.h"
#include "../SensorReadPolicy.h"
#include <math.h>
static CollectorRegistrar _registrar_temp("a01001", &TempCollect::getInstance());

TempCollect* TempCollect::_instance = nullptr;

TempCollect& TempCollect::getInstance() {
    if (_instance == nullptr) {
        _instance = new TempCollect();
    }
    return *_instance;
}

TempCollect::TempCollect() 
    : _port(nullptr), _mb_manager(nullptr) {
    _id = "";
    
    _slaveId = 2;
    _regAddr = 501;
    _factor = 1.0f;
    _regCount = 1;
    unitFactor = 1.0f;
    rawUnit = "ug/m3";
    // _mb_manager = new modbus_manager();
}

TempCollect::~TempCollect() {
    if (_mb_manager) {
        delete _mb_manager;
        _mb_manager = nullptr;
    }
}
bool TempCollect::begin() {
    if (_port == nullptr || _id == "") {
        return false; 
    }
    return true;
}

bool TempCollect::modbusInit(Stream* new_port, String id, String port_name, float factor, String unit) { 
    if (new_port == nullptr) return false;
    _port = new_port;
    if (_mb_manager == nullptr) {
        _mb_manager = new modbus_manager();
    }
    if (_mb_manager && _port) {
        _id = id;
        rawUnit = unit;
        
        
        _factor = factor;
        _mb_manager->modbus_init(_port);
        return true;
    }
    return false;
}

bool TempCollect::gal(int increment, int ratio) {
    (void)increment;
    (void)ratio;
    return true;
}

String TempCollect::getID() const { 
    return _id; 
}

DataPacket* TempCollect::collect() {
    DataPacket *packet = new DataPacket();
    strncpy(packet->sensor_id, _id.c_str(), sizeof(packet->sensor_id) - 1);
    packet->is_valid = false;
    auto &sm = SerialManager::getInstance();
    SemaphoreHandle_t _StreamMutex = sm.getMutex(SERIAL_485);

    if (xSemaphoreTake(_StreamMutex, pdMS_TO_TICKS(3000)) == pdTRUE)
    {
        uint16_t rawValue = 0;
        const bool readOk = _mb_manager->readModbusRegs(
            _slaveId, _regAddr, 1, &rawValue,
            SensorReadPolicy::MAX_ATTEMPTS,
            SensorReadPolicy::RESPONSE_TIMEOUT_MS,
            SensorReadPolicy::QUERY_GAP_MS);
        vTaskDelay(pdMS_TO_TICKS(SensorReadPolicy::QUERY_GAP_MS));
        xSemaphoreGive(_StreamMutex);

        if (readOk)
        {
            const int16_t signedValue = static_cast<int16_t>(rawValue);
            if (signedValue >= -400 && signedValue <= 1200)
            {
                const float value = static_cast<float>(signedValue) / 10.0f;
                packet->value = roundf(value * 100.0f) / 100.0f;
                packet->is_valid = true;
                LOG_DEBUG("Temperature raw: 0x%04X, signed=%d, value=%.1f C",
                          rawValue, signedValue, packet->value);
            }
            else
            {
                LOG_WARNING("Temperature raw value out of range: 0x%04X (%d)",
                            rawValue, signedValue);
            }
        }
        else
        {
            LOG_WARNING("Temperature read failed for %s", _id.c_str());
        }
    }
    else
    {
        LOG_ERROR("Failed to get TTL Mutex for %s", _id.c_str());
    }

    
    return packet;
}

void TempCollect::setModbusConfig(uint8_t slave, uint16_t reg, uint8_t count, float factor) {
    _slaveId = slave;
    _regAddr = reg;
        _regCount = count;
    _factor = factor;
}

void TempCollect::setFactor(float factor) { 
    _factor = factor; 
}

bool TempCollect::updatePort(Stream* new_port) {
    if (new_port == nullptr) return false;
    _port = new_port;
    if (_mb_manager) {
        _mb_manager->modbus_init(_port);
        return true;
    }
    return false;
}

void TempCollect::setIdentity(String id, int group) {
    _id = id;
    
}
