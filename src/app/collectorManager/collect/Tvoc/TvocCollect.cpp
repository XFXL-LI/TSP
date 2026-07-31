#include "TvocCollect.h"
#include "../../../../inc/sys_init.h"
#include "../../../../module/log/log_manager.h"
#include "../../../../module/Serial/SerialManager.h"
#include "../../collectorManager.h"
static CollectorRegistrar _registrar_TVOC("a24035", &TvocCollect::getInstance());

TvocCollect *TvocCollect::_instance = nullptr;

volatile float vToppm_kCurve = 9.8721f;
volatile float vToppm_bCurve = 0.0531f;

TvocCollect &TvocCollect::getInstance()
{
    if (_instance == nullptr)
    {
        _instance = new TvocCollect();
    }
    return *_instance;
}

TvocCollect::TvocCollect()
    : _port(nullptr), _mb_manager(nullptr)
{
    _id = "";

    _slaveId = 3;
    _regAddr = 1;
    _factor = 1.0f;
    _regCount = 3;
    unitFactor = 1.0f;
    rawUnit = "ppm";
}

TvocCollect::~TvocCollect()
{
    if (_mb_manager)
    {
        delete _mb_manager;
        _mb_manager = nullptr;
    }
}
bool TvocCollect::begin()
{
    if (_port == nullptr || _id == "")
    {
        return false;
    }
    return true;
}

bool TvocCollect::modbusInit(Stream *new_port, String id, String port_name, float factor, String unit)
{
    if (new_port == nullptr)
        return false;
    _port = new_port;
    if (_mb_manager == nullptr)
    {
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

bool TvocCollect::gal(int increment, int ratio)
{
    (void)increment;
    (void)ratio;
    return true;
}

String TvocCollect::getID() const
{
    return _id;
}

DataPacket *TvocCollect::collect()
{
    DataPacket *packet = new DataPacket();
    strncpy(packet->sensor_id, _id.c_str(), sizeof(packet->sensor_id) - 1);
    packet->is_valid = false;
    auto &sm = SerialManager::getInstance();
    SemaphoreHandle_t _Stream485Mutex = sm.getMutex(SERIAL_485);
    if (xSemaphoreTake(_Stream485Mutex, pdMS_TO_TICKS(3000)) == pdTRUE)
    {
        uint16_t raw[3] = {0};
        uint32_t valid_count = 0;
        float total_f_value = 0.0f;
        for (int i = 0; i < 2; i++)
        {
            if (_mb_manager->readModbusRegs(_slaveId, _regAddr, _regCount, raw))
            {
                uint32_t ch0_data = (((uint32_t)(raw[0] & 0xFF)) << 16) |
                                    (((uint32_t)(raw[1] & 0xFF)) << 8) |
                                    ((uint32_t)(raw[2] & 0xFF));
                if (ch0_data != 0 && ch0_data != 0xFFFFFFFF)
                {
                    int32_t s_ch0 = (ch0_data & 0x800000) ? (int32_t)(ch0_data | 0xFF000000) : (int32_t)ch0_data;
                    float res_v = (float)s_ch0 / 8388607.0f * 1.2f;
                    float res_ppm = 0.0f;
                    if (res_v > vToppm_bCurve)
                    {
                        res_ppm = (res_v - vToppm_bCurve) * vToppm_kCurve;
                    }
                    float current_target_value = res_ppm;
                    total_f_value += current_target_value;
                    valid_count++;
                }
            }
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        xSemaphoreGive(_Stream485Mutex);
        if (valid_count > 0)
        {
            float average = total_f_value / (float)valid_count;
            packet->value = (float)((int)(average * 100 + 0.5)) / 100.0f * unitFactor;
            packet->is_valid = true;
            LOG_DEBUG("Modbus Parser -> Successfully parsed value: %.2f", packet->value);
        }
        else
        {
            LOG_ERROR("Modbus Parser -> All samples failed or data is invalid.");
        }
    }
    else
    {
        packet->value = 0.0f;
        LOG_ERROR("Failed to get TTL Mutex for %s", _id.c_str());
    }
    return packet;
}

void TvocCollect::setModbusConfig(uint8_t slave, uint16_t reg, uint8_t count, float factor)
{
    _slaveId = slave;
    _regAddr = reg;
    _regCount = count;
    _factor = factor;
}

void TvocCollect::setFactor(float factor)
{
    _factor = factor;
}

bool TvocCollect::updatePort(Stream *new_port)
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

void TvocCollect::setIdentity(String id, int group)
{
    _id = id;
}