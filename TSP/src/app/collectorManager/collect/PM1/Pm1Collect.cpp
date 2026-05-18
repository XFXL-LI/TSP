#include "Pm1Collect.h"
#include "../../../../inc/sys_init.h"
#include "../../../../module/log/log_manager.h"
#include "../../../../module/Serial/SerialManager.h"
#include "../../collectorManager.h"
static CollectorRegistrar _registrar_pm1("a34005", &Pm1Collect::getInstance());

Pm1Collect *Pm1Collect::_instance = nullptr;

Pm1Collect &Pm1Collect::getInstance()
{
    if (_instance == nullptr)
    {
        _instance = new Pm1Collect();
    }
    return *_instance;
}

Pm1Collect::Pm1Collect()
    : _port(nullptr), _mb_manager(nullptr)
{
    _id = "";

    _slaveId = 1;
    _regAddr = 16;
    _factor = 1.0f;
    _regCount = 2;
    // _mb_manager = new modbus_manager();
}

Pm1Collect::~Pm1Collect()
{
    if (_mb_manager)
    {
        delete _mb_manager;
        _mb_manager = nullptr;
    }
}
bool Pm1Collect::begin()
{
    if (_port == nullptr || _id == "")
    {
        return false;
    }
    return true;
}

bool Pm1Collect::modbusInit(Stream *new_port, String id, String port_name, float factor)
{
    if (new_port == nullptr)
        return false;
    _port = new_port;
    if (_mb_manager == nullptr) {
        _mb_manager = new modbus_manager();
    }
    if (_mb_manager && _port)
    {
        _id = id;

        _factor = factor;
        _mb_manager->modbus_init(_port);
        return true;
    }
    return false;
}

bool Pm1Collect::gal(int increment, int ratio) {
    (void)increment;
    (void)ratio;
    return true;
}

String Pm1Collect::getID() const
{
    return _id;
}

DataPacket *Pm1Collect::collect()
{
    DataPacket *packet = new DataPacket();
    strncpy(packet->sensor_id, _id.c_str(), sizeof(packet->sensor_id) - 1);
    packet->is_valid = false;
    auto &sm = SerialManager::getInstance();
    SemaphoreHandle_t _StreamTTLMutex = sm.getMutex("TTL");

    if (xSemaphoreTake(_StreamTTLMutex, pdMS_TO_TICKS(3000)) == pdTRUE)
    {
        uint16_t raw[2] = {0};
        uint32_t valid_count = 0;
        float total_f_value = 0.0f;
        for (int i = 0; i < 2; i++)
        {
            if (_mb_manager->readModbusRegs(_slaveId, _regAddr, _regCount, raw))
            {
                uint32_t current_val = ((uint32_t)raw[0] << 16) | raw[1];
                float current_f = (float)current_val;
                if (current_val != 0 && current_val != 0xFFFFFFFF)
                {
                    total_f_value += current_f;
                    valid_count++;
                }
            }
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        xSemaphoreGive(_StreamTTLMutex);

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

void Pm1Collect::setModbusConfig(uint8_t slave, uint16_t reg, uint8_t count, float factor)
{
    _slaveId = slave;
    _regAddr = reg;
    _regCount = count;
    _factor = factor;
}

void Pm1Collect::setFactor(float factor)
{
    _factor = factor;
}

bool Pm1Collect::updatePort(Stream *new_port)
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

void Pm1Collect::setIdentity(String id, int group)
{
    _id = id;
}