#include "No2Collect.h"
#include "../../../../inc/sys_init.h"
#include "../../../../module/log/log_manager.h"
#include "../../../../module/Serial/SerialManager.h"
#include "../../../../module/gas/GasModbusAccess.h"
#include "../../../../module/gas/GasStatusPolicy.h"
#include "../../collectorManager.h"
static CollectorRegistrar _registrar_no2("a21004", &No2Collect::getInstance());

No2Collect* No2Collect::_instance = nullptr;

No2Collect& No2Collect::getInstance() {
    if (_instance == nullptr) {
        _instance = new No2Collect();
    }
    return *_instance;
}

No2Collect::No2Collect() 
    : _port(nullptr), _mb_manager(nullptr) {
    _id = "";
    
    _slaveId = 4;
    _regAddr = 0x6001;
    _factor = 1.0f;
    _regCount = 1;
    unitFactor = 1.0f;
    rawUnit = "ppb";
    // _mb_manager = new modbus_manager();
}

No2Collect::~No2Collect() {
    if (_mb_manager) {
        delete _mb_manager;
        _mb_manager = nullptr;
    }
}
bool No2Collect::begin() {
    if (_port == nullptr || _id == "") {
        return false; 
    }
    return true;
}

bool No2Collect::modbusInit(Stream* new_port, String id, String port_name, float factor, String unit) { 
    if (new_port == nullptr) return false;
    _port = new_port;
    if (_mb_manager == nullptr) {
        _mb_manager = new modbus_manager();
    }
    if (_mb_manager && _port) {
        _id = id;
        rawUnit = "ppb";
        
        
        _factor = factor;
        _mb_manager->modbus_init(_port);
        return true;
    }
    return false;
}

bool No2Collect::gal(int increment, int ratio) {
    if (_port == nullptr) {
        LOG_ERROR("Gal fail: _port is null for %s", _id.c_str());
        return false;
    }
    auto &sm = SerialManager::getInstance();
    SemaphoreHandle_t _StreamMutex = sm.getMutex(SERIAL_485);
    if (xSemaphoreTake(_StreamMutex, pdMS_TO_TICKS(3000)) == pdTRUE) {
        LOG_INFO("Start NO2 gal calibration for %s", _id.c_str());
        uint8_t rx_buf[8];
        uint8_t modbus_frame1[] = {0x04, 0x10, 0x4F, 0xFF, 0x00, 0x01, 0x02, 0x55, 0xAA, 0x8C, 0xE4};
        while(_port->available()) _port->read(); 
        _port->write(modbus_frame1, sizeof(modbus_frame1));
        vTaskDelay(pdMS_TO_TICKS(100)); 
        if (_port->available() >= 8) {
            _port->readBytes(rx_buf, 8);
            if (rx_buf[0] != 0x04 || rx_buf[1] != 0x10 || rx_buf[2] != 0x4F || rx_buf[3] != 0xFF) {
                LOG_ERROR("NO2 Gal Frame1 response content mismatch!");
                xSemaphoreGive(_StreamMutex);
                return false;
            }
        } else {
            LOG_ERROR("NO2 Gal Frame1 timeout!");
            xSemaphoreGive(_StreamMutex);
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
        uint8_t modbus_frame2[] = {0x04, 0x10, 0x60, 0x06, 0x00, 0x02, 0x04, 0x10, 0x00, 0x00, 0x00, 0xCE, 0x8B};
        while(_port->available()) _port->read(); 
        _port->write(modbus_frame2, sizeof(modbus_frame2));
        vTaskDelay(pdMS_TO_TICKS(100)); 
        if (_port->available() >= 8) {
            _port->readBytes(rx_buf, 8);
            if (rx_buf[0] != 0x04 || rx_buf[1] != 0x10 || rx_buf[2] != 0x60 || rx_buf[3] != 0x06) {
                LOG_ERROR("NO2 Gal Frame2 response content mismatch!");
                xSemaphoreGive(_StreamMutex);
                return false;
            }
        } else {
            LOG_ERROR("NO2 Gal Frame2 timeout!");
            xSemaphoreGive(_StreamMutex);
            return false;
        }
        xSemaphoreGive(_StreamMutex);
        LOG_INFO("NO2 Gal calibration SUCCESS for %s", _id.c_str());
        return true;
    } else {
        LOG_ERROR("Failed to get TTL Mutex for NO2 gal calibration of %s", _id.c_str());
        return false;
    }
}

String No2Collect::getID() const { 
    return _id; 
}

DataPacket* No2Collect::collect() {
    DataPacket *packet = new DataPacket();
    strncpy(packet->sensor_id, _id.c_str(), sizeof(packet->sensor_id) - 1);
    packet->is_valid = false;
    if (_mb_manager == nullptr) {
        LOG_ERROR("K-7S NO2 read failed: Modbus manager is null");
        return packet;
    }

    auto &sm = SerialManager::getInstance();
    SemaphoreHandle_t _StreamMutex = sm.getMutex(SERIAL_485);
    uint16_t gasRegisters[2] = {0, 0};
    bool readOk = false;

    if (xSemaphoreTake(_StreamMutex, pdMS_TO_TICKS(3000)) == pdTRUE)
    {
        readOk = GasModbusAccess::readRegisters(
            *_mb_manager, _slaveId, 0x6000, 2, gasRegisters,
            3, 500, "normal");
        xSemaphoreGive(_StreamMutex);
    }
    else
    {
        LOG_ERROR("Failed to get 485 mutex for %s", _id.c_str());
        return packet;
    }

    if (!readOk) {
        LOG_ERROR("K-7S NO2 read failed after retries: slave=%u", _slaveId);
        return packet;
    }

    packet->value = static_cast<float>(gasRegisters[1]);
    packet->status = GasStatusPolicy::classify(gasRegisters[0]);
    packet->is_valid = packet->status == DataStatus::NORMAL;
    LOG_DEBUG("K-7S NO2 status=0x%04X raw=%u ppb flag=%c",
              gasRegisters[0], gasRegisters[1], dataStatusCode(packet->status));
    return packet;
}

void No2Collect::setModbusConfig(uint8_t slave, uint16_t reg, uint8_t count, float factor) {
    _slaveId = slave;
    _regAddr = reg;
        _regCount = count;
    _factor = factor;
}

void No2Collect::setFactor(float factor) { 
    _factor = factor; 
}

bool No2Collect::updatePort(Stream* new_port) {
    if (new_port == nullptr) return false;
    _port = new_port;
    if (_mb_manager) {
        _mb_manager->modbus_init(_port);
        return true;
    }
    return false;
}

void No2Collect::setIdentity(String id, int group) {
    _id = id;
    
}
