#include "TspCollect.h"
#include "../../../../inc/sys_init.h"
#include "../../../../module/log/log_manager.h"
#include "../../../../module/Serial/SerialManager.h"
#include "../../collectorManager.h"
static CollectorRegistrar _registrar_tsp("a34001", &TspCollect::getInstance());

TspCollect *TspCollect::_instance = nullptr;

TspCollect &TspCollect::getInstance()
{
    if (_instance == nullptr)
    {
        _instance = new TspCollect();
    }
    return *_instance;
}

TspCollect::TspCollect()
    : _port(nullptr), _mb_manager(nullptr)
{
    _id = "";
    
    _slaveId = 1;
    _regAddr = 22;
    _factor = 1.0f;
    _regCount = 2;
    unitFactor = 1.0f;
    rawUnit = "ug/m3";
    // _mb_manager = new modbus_manager();
}

TspCollect::~TspCollect()
{
    if (_mb_manager)
    {
        delete _mb_manager;
        _mb_manager = nullptr;
    }
}
bool TspCollect::begin()
{
    if (_port == nullptr || _id == "")
    {
        return false;
    }
    return true;
}

bool TspCollect::modbusInit(Stream* new_port, String id, String port_name, float factor, String unit)
{
    if (new_port == nullptr)
        return false;
    _port = new_port;
    if (_mb_manager == nullptr) {
        _mb_manager = new modbus_manager();
    }
    if (_mb_manager && _port)
    {
        rawUnit = unit;
        if (rawUnit == "ug/m3") {
            unitFactor = 1.0f;
        } else if (rawUnit == "mg/m3") {
            unitFactor = 0.001f;
        } else if (rawUnit == "ng/m3") {
            unitFactor = 1000.0f;
        } else {
            unitFactor = 1.0f;
        }
        _id = id;
        _port_name = port_name;
        _factor = factor;
        _mb_manager->modbus_init(_port);
        return true;
    }
    return false;
}

bool TspCollect::gal(int increment, int ratio) {
    auto& sm = SerialManager::getInstance();
    SemaphoreHandle_t _StreamTTLMutex = sm.getMutex("TTL");

    if (xSemaphoreTake(_StreamTTLMutex, pdMS_TO_TICKS(3000)) == pdTRUE) {
        uint16_t writeVal[2] = {0};
        writeVal[0] = increment;
        writeVal[1] = ratio;
        uint16_t raw[2] = {0};
        if (!_mb_manager->writeModbusRegs(1, 0x36, 2, writeVal)) {
            LOG_ERROR("Failed to write increment and ratio values for %s", _id.c_str());
            return false;
        } else {
            LOG_DEBUG("GAL write succeeded for %s: increment=%d, ratio=%d", _id.c_str(), increment, ratio);
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
        if (_mb_manager->readModbusRegs(1, 0x36, 2, raw)) {
            if (raw[0] == increment && raw[1] == ratio) {
                LOG_DEBUG("GAL verification succeeded for %s: increment=%d, ratio=%d", _id.c_str(), raw[0], raw[1]);
                return true;
            } else {
                LOG_WARNING("GAL verification failed for %s: expected increment=%d, ratio=%d but got increment=%d, ratio=%d",
                            _id.c_str(), increment, ratio, raw[0], raw[1]);
                return false;
            }
        }
        xSemaphoreGive(_StreamTTLMutex);
    } else {
        LOG_ERROR("Failed to get TTL Mutex for getID in %s", _id.c_str());
        return false;
    }
}

String TspCollect::getID() const
{
    return _id;
}

DataPacket *TspCollect::collect()
{
    DataPacket *packet = new DataPacket();
    strncpy(packet->sensor_id, _id.c_str(), sizeof(packet->sensor_id) - 1);
    packet->is_valid = false;
    auto& sm = SerialManager::getInstance();
    SemaphoreHandle_t _StreamTTLMutex = sm.getMutex("TTL");

    if (xSemaphoreTake(_StreamTTLMutex, pdMS_TO_TICKS(3000)) == pdTRUE) {
        uint16_t raw[2] = {0};
        uint32_t valid_count = 0;
        float total_f_value = 0.0f;
        for (int i = 0; i < 2; i++) { 
            if (_mb_manager->readModbusRegs(_slaveId, _regAddr, _regCount, raw)) {
                uint32_t current_val = ((uint32_t)raw[0] << 16) | raw[1];
                float current_f = (float)current_val;
                if (current_val != 0 && current_val != 0xFFFFFFFF) {
                    total_f_value += current_f;
                    valid_count++;
                }
            }
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        xSemaphoreGive(_StreamTTLMutex);

        if (valid_count > 0) {
            float average = total_f_value / (float)valid_count;
            float c = 1.0f;
            // -------------------------------------------------------
            if (average > 900.0f && average < 1500.0f) {
                c = 0.5f;
            } else if (average >= 1500.0f && average < 2000.0f) {
                c = 0.4f;
            } else if (average >= 2000.0f && average < 3000.0f) {
                c = 0.3f;
            } else if (average >= 3000.0f && average < 4000.0f) {
                c = 0.2f;
            } else if (average >= 4000.0f && average < 5000.0f) {
                c = 0.1f;
            } else if (average >= 6000.0f && average < 8000.0f) {
                c = 0.09f;
            } else if (average >= 8000.0f && average < 10000.0f) {
                c = 0.08f;
            } else if (average >= 10000.0f && average < 12000.0f) {
                c = 0.07f;
            } else if (average >= 12000.0f && average < 15000.0f) {
                c = 0.06f;
            }
            // -------------------------------------------------------

            packet->value = (float)((int)(average * 100 + 0.5)) / 100.0f * c * unitFactor;
            packet->is_valid = true;
            // LOG_DEBUG("%s average value: %.2f (based on %d samples)", _id.c_str(), packet->value, valid_count);
        }
    } else {
        packet->value = 0.0f;
        packet->is_valid = false;
        LOG_ERROR("Failed to get TTL Mutex for %s", _id.c_str());
    }
    
    return packet;
}

void TspCollect::setModbusConfig(uint8_t slave, uint16_t reg, uint8_t count, float factor)
{
    _slaveId = slave;
    _regAddr = reg;
    _regCount = count;
    _factor = factor;
}

void TspCollect::setFactor(float factor)
{
    _factor = factor;
}

bool TspCollect::updatePort(Stream *new_port)
{
    if (new_port == nullptr)
        return false;
    _port = new_port;
    if (_mb_manager)
    {
        _mb_manager->modbus_init(_port);
        return true;
    }
    return false;
}

void TspCollect::setIdentity(String id, int group)
{
    _id = id;
    
}