#include "PressureCollect.h"
#include "../../../../inc/sys_init.h"
#include "../../../../module/log/log_manager.h"
#include "../../../../module/Serial/SerialManager.h"
#include "../../collectorManager.h"
#include <algorithm>
#include <math.h>
static CollectorRegistrar _registrar_pre("a01006", &PressureCollect::getInstance());

PressureCollect* PressureCollect::_instance = nullptr;

PressureCollect& PressureCollect::getInstance() {
    if (_instance == nullptr) {
        _instance = new PressureCollect();
    }
    return *_instance;
}

PressureCollect::PressureCollect() 
    : _port(nullptr), _mb_manager(nullptr) {
    _id = "";
    
    _slaveId = 2;
    _regAddr = 505;
    _factor = 1.0f;
    _regCount = 1;
    unitFactor = 1.0f;
    rawUnit = "ug/m3";
    _lastValidValue = 0.0f;
    _pendingJumpValue = 0.0f;
    _pendingJumpCount = 0;
    _hasLastValidValue = false;
    // _mb_manager = new modbus_manager();
}

PressureCollect::~PressureCollect() {
    if (_mb_manager) {
        delete _mb_manager;
        _mb_manager = nullptr;
    }
}
bool PressureCollect::begin() {
    if (_port == nullptr || _id == "") {
        return false; 
    }
    return true;
}

bool PressureCollect::modbusInit(Stream* new_port, String id, String port_name, float factor, String unit) { 
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

bool PressureCollect::gal(int increment, int ratio) {
    (void)increment;
    (void)ratio;
    return true;
}

String PressureCollect::getID() const { 
    return _id; 
}

DataPacket* PressureCollect::collect() {
    DataPacket *packet = new DataPacket();
    strncpy(packet->sensor_id, _id.c_str(), sizeof(packet->sensor_id) - 1);
    packet->is_valid = false;
    auto &sm = SerialManager::getInstance();
    SemaphoreHandle_t _StreamMutex = sm.getMutex(SERIAL_485);

    if (xSemaphoreTake(_StreamMutex, pdMS_TO_TICKS(3000)) == pdTRUE)
    {
        uint16_t samples[5] = {0};
        uint8_t validCount = 0;
        for (int i = 0; i < 5; i++)
        {
            uint16_t current_val = _mb_manager->readModbusReg(_slaveId, _regAddr);
            if (current_val != 0 && current_val != 0xFFFF)
            {
                samples[validCount++] = current_val;
                LOG_DEBUG("Pressure raw sample[%d]: %u", validCount - 1, current_val);
            }
            vTaskDelay(pdMS_TO_TICKS(30));
        }
        xSemaphoreGive(_StreamMutex);

        if (validCount >= 3)
        {
            std::sort(samples, samples + validCount);
            float pressure = 0.0f;
            if ((validCount & 1U) != 0)
            {
                pressure = samples[validCount / 2] / 10.0f;
            }
            else
            {
                pressure = (samples[validCount / 2 - 1] + samples[validCount / 2]) / 20.0f;
            }
            pressure = roundf(pressure * 100.0f) / 100.0f;
            LOG_DEBUG("Pressure median: %.2f kPa (%u valid samples)",
                      pressure, validCount);

            const bool inPhysicalRange = pressure >= 30.0f && pressure <= 120.0f;
            if (!inPhysicalRange)
            {
                LOG_WARNING("Pressure rejected outside physical range: %.2f kPa", pressure);
            }
            else if (!_hasLastValidValue || fabsf(pressure - _lastValidValue) <= 1.5f)
            {
                _lastValidValue = pressure;
                _hasLastValidValue = true;
                _pendingJumpCount = 0;
                packet->value = pressure;
                packet->is_valid = true;
            }
            else
            {
                if (_pendingJumpCount > 0 &&
                    fabsf(pressure - _pendingJumpValue) <= 0.3f)
                {
                    _pendingJumpCount++;
                }
                else
                {
                    _pendingJumpValue = pressure;
                    _pendingJumpCount = 1;
                }

                LOG_WARNING(
                    "Pressure jump rejected: last=%.2f, candidate=%.2f, confirmation=%u/3",
                    _lastValidValue, pressure, _pendingJumpCount);

                if (_pendingJumpCount >= 3)
                {
                    _lastValidValue = pressure;
                    _pendingJumpCount = 0;
                    packet->value = pressure;
                    packet->is_valid = true;
                    LOG_INFO("Pressure jump accepted after confirmation: %.2f kPa", pressure);
                }
            }
        }
    }
    else
    {
        LOG_ERROR("Failed to get TTL Mutex for %s", _id.c_str());
    }

    
    return packet;
}

void PressureCollect::setModbusConfig(uint8_t slave, uint16_t reg, uint8_t count, float factor) {
    _slaveId = slave;
    _regAddr = reg;
        _regCount = count;
    _factor = factor;
}

void PressureCollect::setFactor(float factor) { 
    _factor = factor; 
}

bool PressureCollect::updatePort(Stream* new_port) {
    if (new_port == nullptr) return false;
    _port = new_port;
    if (_mb_manager) {
        _mb_manager->modbus_init(_port);
        return true;
    }
    return false;
}

void PressureCollect::setIdentity(String id, int group) {
    _id = id;
    
}
