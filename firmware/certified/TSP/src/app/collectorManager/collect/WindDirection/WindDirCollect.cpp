#include "WindDirCollect.h"
#include "../../../../inc/sys_init.h"
#include "../../../../module/log/log_manager.h"
#include "../../../../module/Serial/SerialManager.h"
#include "../../collectorManager.h"
#include "../SensorReadPolicy.h"
static CollectorRegistrar _registrar_wd("a01008", &WindDirCollect::getInstance());

WindDirCollect *WindDirCollect::_instance = nullptr;

WindDirCollect &WindDirCollect::getInstance()
{
    if (_instance == nullptr)
    {
        _instance = new WindDirCollect();
    }
    return *_instance;
}

WindDirCollect::WindDirCollect()
    : _port(nullptr), _mb_manager(nullptr)
{
    _id = "";

    _slaveId = 1;
    _regAddr = 1;
    _factor = 1.0f;
    _regCount = 1;
    unitFactor = 1.0f;
    rawUnit = "ug/m3";
    // _mb_manager = new modbus_manager();
}

WindDirCollect::~WindDirCollect()
{
    if (_mb_manager)
    {
        delete _mb_manager;
        _mb_manager = nullptr;
    }
}
bool WindDirCollect::begin()
{
    if (_port == nullptr || _id == "")
    {
        return false;
    }
    return true;
}

bool WindDirCollect::modbusInit(Stream* new_port, String id, String port_name, float factor, String unit)
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
        rawUnit = unit;

        _factor = factor;
        _mb_manager->modbus_init(_port);
        return true;
    }
    return false;
}

bool WindDirCollect::gal(int increment, int ratio) {
    (void)increment;
    (void)ratio;
    return true;
}

String WindDirCollect::getID() const
{
    return _id;
}

DataPacket *WindDirCollect::collect()
{
    DataPacket *packet = new DataPacket();
    strncpy(packet->sensor_id, _id.c_str(), sizeof(packet->sensor_id) - 1);
    packet->is_valid = false;
    auto &sm = SerialManager::getInstance();
    SemaphoreHandle_t _StreamMutex = sm.getMutex(SERIAL_485);

    if (xSemaphoreTake(_StreamMutex, pdMS_TO_TICKS(3000)) == pdTRUE)
    {
        uint16_t currentVal = 0;
        const bool readOk = _mb_manager->readModbusRegs(
            _slaveId, _regAddr, 1, &currentVal,
            SensorReadPolicy::MAX_ATTEMPTS,
            SensorReadPolicy::RESPONSE_TIMEOUT_MS,
            SensorReadPolicy::QUERY_GAP_MS);
        vTaskDelay(pdMS_TO_TICKS(SensorReadPolicy::QUERY_GAP_MS));
        xSemaphoreGive(_StreamMutex);

        if (readOk && currentVal != 0xFFFFU)
        {
            const float value = static_cast<float>(currentVal) / 100.0f;
            packet->value =
                static_cast<float>(static_cast<int>(value * 100.0f + 0.5f)) / 100.0f;
            packet->is_valid = true;
        }
        else
        {
            LOG_WARNING("Wind direction read failed for %s", _id.c_str());
        }
    }
    else
    {
        LOG_ERROR("Failed to get TTL Mutex for %s", _id.c_str());
    }

    
    return packet;
}

void WindDirCollect::setModbusConfig(uint8_t slave, uint16_t reg, uint8_t count, float factor)
{
    _slaveId = slave;
    _regAddr = reg;
    _regCount = count;
    _factor = factor;
}

void WindDirCollect::setFactor(float factor)
{
    _factor = factor;
}

bool WindDirCollect::updatePort(Stream *new_port)
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

void WindDirCollect::setIdentity(String id, int group)
{
    _id = id;
}
