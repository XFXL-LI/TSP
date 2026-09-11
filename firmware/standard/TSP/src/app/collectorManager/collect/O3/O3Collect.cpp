#include "O3Collect.h"
#include "../../../../inc/sys_init.h"
#include "../../../../module/log/log_manager.h"
#include "../../../../module/Serial/SerialManager.h"
#include "../../../../module/gas/GasModbusAccess.h"
#include "../../../../module/gas/GasStatusPolicy.h"
#include "../../collectorManager.h"
static CollectorRegistrar _registrar_o3("w34011", &O3Collect::getInstance());

O3Collect* O3Collect::_instance = nullptr;

O3Collect& O3Collect::getInstance() {
    if (_instance == nullptr) {
        _instance = new O3Collect();
    }
    return *_instance;
}

O3Collect::O3Collect() 
    : _port(nullptr), _mb_manager(nullptr) {
    _id = "";
    
    _slaveId = 3;
    _regAddr = 0x6001;
    _factor = 1.0f;
    _regCount = 1;
    unitFactor = 1.0f;
    rawUnit = "ppb";
    // _mb_manager = new modbus_manager();
}

O3Collect::~O3Collect() {
    if (_mb_manager) {
        delete _mb_manager;
        _mb_manager = nullptr;
    }
}
bool O3Collect::begin() {
    if (_port == nullptr || _id == "") {
        return false; 
    }
    return true;
}

bool O3Collect::modbusInit(Stream* new_port, String id, String port_name, float factor, String unit) { 
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

bool O3Collect::gal(int increment, int ratio) {
    if (_port == nullptr) {
        LOG_ERROR("Gal fail: _port is null for %s", _id.c_str());
        return false;
    }
    auto &sm = SerialManager::getInstance();
    SemaphoreHandle_t _StreamMutex = sm.getMutex(SERIAL_485);
    if (xSemaphoreTake(_StreamMutex, pdMS_TO_TICKS(3000)) == pdTRUE) {
        LOG_INFO("Start O3 gal calibration for %s", _id.c_str());
        uint8_t rx_buf[8];
        uint8_t modbus_frame1[] = {0x03, 0x10, 0x4F, 0xFF, 0x00, 0x01, 0x02, 0x55, 0xAA, 0xAA, 0xD4};
        while(_port->available()) _port->read();
        _port->write(modbus_frame1, sizeof(modbus_frame1));
        vTaskDelay(pdMS_TO_TICKS(100));
        if (_port->available() >= 8) {
            _port->readBytes(rx_buf, 8);
            if (rx_buf[0] != 0x03 || rx_buf[1] != 0x10 || rx_buf[2] != 0x4F || rx_buf[3] != 0xFF) {
                LOG_ERROR("O3 Gal Frame1 response content mismatch!");
                xSemaphoreGive(_StreamMutex);
                return false;
            }
        } else {
            LOG_ERROR("O3 Gal Frame1 timeout!");
            xSemaphoreGive(_StreamMutex);
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
        uint8_t modbus_frame2[] = {0x03, 0x10, 0x60, 0x06, 0x00, 0x02, 0x04, 0x10, 0x00, 0x00, 0x00, 0xD4, 0xFF};
        while(_port->available()) _port->read();
        _port->write(modbus_frame2, sizeof(modbus_frame2));
        vTaskDelay(pdMS_TO_TICKS(100)); 
        if (_port->available() >= 8) {
            _port->readBytes(rx_buf, 8);
            if (rx_buf[0] != 0x03 || rx_buf[1] != 0x10 || rx_buf[2] != 0x60 || rx_buf[3] != 0x06) {
                LOG_ERROR("O3 Gal Frame2 response content mismatch!");
                xSemaphoreGive(_StreamMutex);
                return false;
            }
        } else {
            LOG_ERROR("O3 Gal Frame2 timeout!");
            xSemaphoreGive(_StreamMutex);
            return false;
        }
        xSemaphoreGive(_StreamMutex);
        LOG_INFO("O3 Gal calibration SUCCESS for %s", _id.c_str());
        return true;
    } else {
        LOG_ERROR("Failed to get TTL Mutex for O3 gal calibration of %s", _id.c_str());
        return false;
    }
}

String O3Collect::getID() const { 
    return _id; 
}

DataPacket* O3Collect::collect() {
    DataPacket *packet = new DataPacket();
    strncpy(packet->sensor_id, _id.c_str(), sizeof(packet->sensor_id) - 1);
    packet->is_valid = false;
    if (_mb_manager == nullptr) {
        LOG_ERROR("K-7S O3 read failed: Modbus manager is null");
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
        LOG_ERROR("K-7S O3 read failed after retries: slave=%u", _slaveId);
        return packet;
    }

    packet->value = static_cast<float>(gasRegisters[1]);
    packet->status = GasStatusPolicy::classify(gasRegisters[0]);
    packet->is_valid = packet->status == DataStatus::NORMAL;
    LOG_DEBUG("K-7S O3 status=0x%04X raw=%u ppb flag=%c",
              gasRegisters[0], gasRegisters[1], dataStatusCode(packet->status));
    return packet;
}

void O3Collect::setModbusConfig(uint8_t slave, uint16_t reg, uint8_t count, float factor) {
    _slaveId = slave;
    _regAddr = reg;
        _regCount = count;
    _factor = factor;
}

void O3Collect::setFactor(float factor) { 
    _factor = factor; 
}

bool O3Collect::updatePort(Stream* new_port) {
    if (new_port == nullptr) return false;
    _port = new_port;
    if (_mb_manager) {
        _mb_manager->modbus_init(_port);
        return true;
    }
    return false;
}

void O3Collect::setIdentity(String id, int group) {
    _id = id;
    
}
