#include "Pm1Collect.h"
#include "../../../../inc/sys_init.h"
#include "../../../../module/log/log_manager.h"
#include "../../../../module/Serial/SerialManager.h"
#include "../../collectorManager.h"
#include "../SensorReadPolicy.h"
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
    unitFactor = 1.0f;
    rawUnit = "ug/m3";
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

bool Pm1Collect::modbusInit(Stream* new_port, String id, String port_name, float factor, String unit)
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
        _factor = factor;
        _mb_manager->modbus_init(_port);

        return true;
    }
    return false;
}

bool Pm1Collect::gal(int increment, int ratio) {
    auto& sm = SerialManager::getInstance();
    SemaphoreHandle_t _StreamTTLMutex = sm.getMutex("TTL");

    if (xSemaphoreTake(_StreamTTLMutex, pdMS_TO_TICKS(3000)) != pdTRUE) {
        LOG_ERROR("Failed to get TTL Mutex for getID in %s", _id.c_str());
        return false;
    }

    bool success = false;
    uint16_t writeVal[2] = {
        static_cast<uint16_t>(increment), static_cast<uint16_t>(ratio)
    };
    uint16_t raw[2] = {0};
    if (!_mb_manager->writeModbusRegs(1, 0x30, 2, writeVal)) {
        LOG_ERROR("Failed to write increment and ratio values for %s", _id.c_str());
    } else {
        LOG_DEBUG("GAL write succeeded for %s: increment=%d, ratio=%d", _id.c_str(), increment, ratio);
        vTaskDelay(pdMS_TO_TICKS(1000));
        if (!_mb_manager->readModbusRegs(1, 0x30, 2, raw)) {
            LOG_ERROR("GAL verification read failed for %s", _id.c_str());
        } else if (raw[0] == writeVal[0] && raw[1] == writeVal[1]) {
            success = true;
            LOG_DEBUG("GAL verification succeeded for %s: increment=%u, ratio=%u",
                      _id.c_str(), (unsigned)raw[0], (unsigned)raw[1]);
        } else {
            LOG_WARNING("GAL verification failed for %s: expected increment=%u, ratio=%u but got increment=%u, ratio=%u",
                        _id.c_str(), (unsigned)writeVal[0], (unsigned)writeVal[1],
                        (unsigned)raw[0], (unsigned)raw[1]);
        }
    }

    xSemaphoreGive(_StreamTTLMutex);
    return success;
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
        const bool readOk = _mb_manager->readModbusRegs(
            _slaveId, _regAddr, _regCount, raw,
            SensorReadPolicy::MAX_ATTEMPTS,
            SensorReadPolicy::RESPONSE_TIMEOUT_MS,
            SensorReadPolicy::QUERY_GAP_MS);
        vTaskDelay(pdMS_TO_TICKS(SensorReadPolicy::QUERY_GAP_MS));
        xSemaphoreGive(_StreamTTLMutex);

        if (readOk)
        {
            const uint32_t currentVal =
                (static_cast<uint32_t>(raw[0]) << 16) | raw[1];
            if (currentVal != 0xFFFFFFFFU)
            {
                const float currentValue = static_cast<float>(currentVal);
                packet->value =
                    static_cast<float>(static_cast<int>(currentValue * 100.0f + 0.5f)) /
                    100.0f * unitFactor;
                packet->is_valid = true;
            }
            else
            {
                packet->status = DataStatus::SENSOR_FAULT;
                LOG_WARNING("[DIAG] PM_COLLECT_INVALID id=%s read_failed=0 invalid_register=1",
                            _id.c_str());
            }
        }
        else
        {
            LOG_WARNING("[DIAG] PM_COLLECT_INVALID id=%s read_failed=1 invalid_register=0",
                        _id.c_str());
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
