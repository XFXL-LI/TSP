#include "TvocCollect.h"
#include "../../../../inc/sys_init.h"
#include "../../../../module/log/log_manager.h"
#include "../../../../module/Serial/SerialManager.h"
#include "../../collectorManager.h"
static CollectorRegistrar _registrar_TVOC("a24035", &TvocCollect::getInstance());

TvocCollect* TvocCollect::_instance = nullptr;

TvocCollect& TvocCollect::getInstance() {
    if (_instance == nullptr) {
        _instance = new TvocCollect();
    }
    return *_instance;
}

TvocCollect::TvocCollect() 
    : _port(nullptr), _mb_manager(nullptr) {
    _id = "";
    
    _slaveId = 7;
    _regAddr = 2;
    _factor = 1.0f;
    _regCount = 1;
    // _mb_manager = new modbus_manager();
}

TvocCollect::~TvocCollect() {
    if (_mb_manager) {
        delete _mb_manager;
        _mb_manager = nullptr;
    }
}
bool TvocCollect::begin() {
    if (_port == nullptr || _id == "") {
        return false; 
    }
    return true;
}

bool TvocCollect::modbusInit(Stream* new_port, String id, String port_name, float factor) { 
    if (new_port == nullptr) return false;
    _port = new_port;
    if (_mb_manager == nullptr) {
        _mb_manager = new modbus_manager();
    }
    if (_mb_manager && _port) {
        _id = id;
        
        
        _factor = factor;
        _mb_manager->modbus_init(_port);
        return true;
    }
    return false;
}

bool TvocCollect::gal(int increment, int ratio) {
    (void)increment;
    (void)ratio;
    return true;
}

String TvocCollect::getID() const { 
    return _id; 
}

DataPacket* TvocCollect::collect() {
    DataPacket *packet = new DataPacket();
    strncpy(packet->sensor_id, _id.c_str(), sizeof(packet->sensor_id) - 1);
    packet->is_valid = false;
    auto &sm = SerialManager::getInstance();
    SemaphoreHandle_t _StreamMutex = sm.getMutex(SERIAL_485);

    if (xSemaphoreTake(_StreamMutex, pdMS_TO_TICKS(3000)) == pdTRUE)
    {
        uint16_t raw[2] = {0};
        uint32_t valid_count = 0;
        float total_f_value = 0.0f;
        for (int i = 0; i < 1; i++)
        {
            uint16_t current_val = _mb_manager->readModbusReg(_slaveId, _regAddr);
            if (current_val != 0 && current_val != 0xFFFF)
            {
                float current_f = (float)current_val;
                total_f_value += current_f;
                valid_count++;
            }
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        xSemaphoreGive(_StreamMutex);

        if (valid_count > 0)
        {
            float average = total_f_value / (float)valid_count;
            packet->value = (float)((int)(average * 100 + 0.5)) / 100.0f;
            packet->is_valid = true;
            // LOG_DEBUG("%s average value: %.2f (based on %d samples)", _id.c_str(), packet->value, valid_count);
        }
    }
    else
    {
        LOG_ERROR("Failed to get TTL Mutex for %s", _id.c_str());
    }

    
    return packet;
}

void TvocCollect::setModbusConfig(uint8_t slave, uint16_t reg, uint8_t count, float factor) {
    _slaveId = slave;
    _regAddr = reg;
        _regCount = count;
    _factor = factor;
}

void TvocCollect::setFactor(float factor) { 
    _factor = factor; 
}

bool TvocCollect::updatePort(Stream* new_port) {
    if (new_port == nullptr) return false;
    _port = new_port;
    if (_mb_manager) {
        _mb_manager->modbus_init(_port);
        return true;
    }
    return false;
}

void TvocCollect::setIdentity(String id, int group) {
    _id = id;
    
}