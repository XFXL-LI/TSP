#include "modbus_manager.h"
#include "../log/log_manager.h"
#include <HardwareSerial.h>
#include <SoftwareSerial.h>
#include <arduino.h>
#include <ModbusRTU.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static SemaphoreHandle_t modbusResSem = NULL;
static SemaphoreHandle_t _busMutex = NULL; 

bool cbRes(Modbus::ResultCode event, uint16_t transactionId, void *data)
{
    if (event == Modbus::EX_SUCCESS)
    {
        if (modbusResSem != NULL)
        {
            xSemaphoreGive(modbusResSem);
        }
        return true;
    }
    else
    {
        // LOG_ERROR(("modbus read error: error code 0x" + String((uintptr_t)event, HEX) + " transactionId: " + String(transactionId) + " data: 0x" + String((uintptr_t)data, HEX)).c_str());
        return false;
    }
}

modbus_manager::modbus_manager()
    : _mb_instances(nullptr),
      _serial_ports(nullptr)
{
    _busMutex = xSemaphoreCreateMutex();
}
modbus_manager::~modbus_manager()
{
    LOG_INFO("modbus_manager Destructor");
}
void modbus_manager::modbus_init(Stream *current_port)
{
    if (modbusResSem == NULL)
    {
        modbusResSem = xSemaphoreCreateBinary();
    }
    _serial_ports = current_port;
    _mb_instances = new ModbusRTU();
    _mb_instances->begin(current_port);
    _mb_instances->master();
}

uint16_t modbus_manager::readModbusReg(uint8_t slaveId, uint16_t startAddr)
{
    if (_mb_instances == nullptr || modbusResSem == NULL)
        return 0xFFFF;
    if (xSemaphoreTake(_busMutex, pdMS_TO_TICKS(200)) != pdTRUE) {
        return 0xFFFF;
    }
    ModbusRTU *mb = _mb_instances;
    uint16_t single_val = 0;
    uint32_t res_sum = 0;
    int success_count = 0;
    int failed_count = 0;
    xSemaphoreTake(modbusResSem, 0);
    while (true)
    {
        if (success_count >= 3 || failed_count >= 3)
        {
            break;
        }
        if (!mb->slave())
        {
            mb->readHreg(slaveId, startAddr, &single_val, 1, cbRes);
            if (xSemaphoreTake(modbusResSem, pdMS_TO_TICKS(200)) == pdTRUE)
            {
                res_sum += single_val;
                success_count++;
            } else {
                failed_count++;
            }
        }
        mb->task();
        yield();
    }
    xSemaphoreGive(_busMutex);
    if (success_count == 0)
    {
        return 0xFFFF;
    }
    return (uint16_t)(res_sum / success_count);
}
bool modbus_manager::writeModbusReg(uint8_t slaveId, uint16_t startAddr, uint32_t writeValue)
{
    if (_mb_instances == nullptr || modbusResSem == NULL)
        return 0;
    if (xSemaphoreTake(_busMutex, pdMS_TO_TICKS(200)) != pdTRUE) {
        return 0;
    }
    ModbusRTU *mb = _mb_instances;
    uint16_t result_buffer[1] = {0};
    int i = 0;
    while (i < 3)
    {
        if (!mb->slave())
        {
            xSemaphoreTake(modbusResSem, 0);
            mb->writeHreg(slaveId, startAddr, writeValue, cbRes);
            if (xSemaphoreTake(modbusResSem, pdMS_TO_TICKS(200)) == pdTRUE)
            {
                return true;
            }
            i++;
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        mb->task();
        yield();
    }
    xSemaphoreGive(_busMutex);
    return false;
}
bool modbus_manager::readModbusRegs(uint8_t slaveId, uint16_t startAddr, uint16_t count, uint16_t *destBuffer)
{
    if (_mb_instances == nullptr || modbusResSem == NULL || destBuffer == nullptr)
        return false;
    if (xSemaphoreTake(_busMutex, pdMS_TO_TICKS(200)) != pdTRUE) {
        return 0;
    }
    ModbusRTU *mb = _mb_instances;
    int retry = 0;
    xSemaphoreTake(modbusResSem, 0);
    bool success = false;
    int collect_index = 0;
    while (1)
    {
        if (collect_index >= 3)
        {
            break;
        }
        if (!mb->slave())
        {
            mb->readHreg(slaveId, startAddr, destBuffer, count, cbRes);
            uint32_t startWait = millis();
            while (millis() - startWait < 1000)
            {
                if (xSemaphoreTake(modbusResSem, pdMS_TO_TICKS(200)) == pdTRUE)
                {
                    success = true;
                    break;
                }
                vTaskDelay(1);
            }
            collect_index++;
        }
        mb->task();
        yield();
    }
    xSemaphoreGive(_busMutex);
    if (success)
    {
        return true;
    }
    else
    {
        return false;
    }
}
bool modbus_manager::writeModbusRegs(uint8_t slaveId, uint16_t startAddr, uint16_t count, uint16_t *dataBuffer)
{
    if (_mb_instances == nullptr || modbusResSem == NULL || dataBuffer == nullptr)
        return false;
    if (xSemaphoreTake(_busMutex, pdMS_TO_TICKS(200)) != pdTRUE) {
        return 0;
    }
    ModbusRTU *mb = _mb_instances;
    int retry = 0;

    while (retry < 3)
    {
        if (!mb->slave())
        {
            xSemaphoreTake(modbusResSem, 0);
            mb->writeHreg(slaveId, startAddr, dataBuffer, count, cbRes);
            if (xSemaphoreTake(modbusResSem, pdMS_TO_TICKS(200)) == pdTRUE)
            {
                return true;
            }

            retry++;
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        mb->task();
        yield();
    }
    xSemaphoreGive(_busMutex);
    return false;
}