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

class AbortableModbusRTU : public ModbusRTU
{
public:
    void abortTransaction()
    {
        if (_slaveId == 0) return;
        if (_cb != nullptr)
        {
            _cb(Modbus::EX_TIMEOUT, 0, nullptr);
            _cb = nullptr;
        }
        free(_sentFrame);
        _sentFrame = nullptr;
        _data = nullptr;
        _slaveId = 0;
    }
};

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
    if (_busMutex == NULL)
    {
        _busMutex = xSemaphoreCreateMutex();
    }
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
    _mb_instances = new AbortableModbusRTU();
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
    for (int attempt = 0; attempt < 3; attempt++)
    {
        while (xSemaphoreTake(modbusResSem, 0) == pdTRUE)
        {
        }

        single_val = 0;
        if (!mb->slave())
        {
            if (!mb->readHreg(slaveId, startAddr, &single_val, 1, cbRes))
            {
                vTaskDelay(pdMS_TO_TICKS(20));
                continue;
            }
        }

        uint32_t startedAt = millis();
        bool completed = false;
        while (millis() - startedAt < 500)
        {
            mb->task();
            if (xSemaphoreTake(modbusResSem, 0) == pdTRUE)
            {
                completed = true;
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(1));
        }

        if (completed)
        {
            res_sum += single_val;
            success_count++;
        }
        else
        {
            LOG_WARNING("Modbus read timeout: slave=%u, register=%u, attempt=%d",
                        slaveId, startAddr, attempt + 1);
        }
        vTaskDelay(pdMS_TO_TICKS(10));
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
        LOG_ERROR("Failed to take bus mutex for writeModbusReg");
        return 0;
    }
    ModbusRTU *mb = _mb_instances;
    uint16_t result_buffer[1] = {0};
    int i = 0;
    xSemaphoreTake(modbusResSem, 0);
    while (i < 3)
    {
        if (!mb->slave())
        {
            mb->writeHreg(slaveId, startAddr, writeValue, cbRes);
            if (xSemaphoreTake(modbusResSem, pdMS_TO_TICKS(200)) == pdTRUE)
            {
                xSemaphoreGive(_busMutex);
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
bool modbus_manager::readModbusRegs(uint8_t slaveId, uint16_t startAddr, uint16_t count,
                                    uint16_t *destBuffer, uint8_t maxAttempts,
                                    uint32_t responseTimeoutMs, uint32_t retryDelayMs)
{
    if (_mb_instances == nullptr || modbusResSem == NULL || destBuffer == nullptr ||
        count == 0 || maxAttempts == 0)
        return false;
    if (xSemaphoreTake(_busMutex, pdMS_TO_TICKS(200)) != pdTRUE) {
        return false;
    }

    ModbusRTU *mb = _mb_instances;
    auto *abortableMb = static_cast<AbortableModbusRTU *>(_mb_instances);
    bool success = false;

    for (uint8_t attempt = 0; attempt < maxAttempts; ++attempt)
    {
        while (xSemaphoreTake(modbusResSem, 0) == pdTRUE)
        {
        }

        while (_serial_ports != nullptr && _serial_ports->available() > 0)
        {
            _serial_ports->read();
        }

        memset(destBuffer, 0, count * sizeof(uint16_t));
        if (mb->slave())
        {
            abortableMb->abortTransaction();
        }

        if (!mb->readHreg(slaveId, startAddr, destBuffer, count, cbRes))
        {
            LOG_WARNING("Modbus bulk read could not start: slave=%u, register=%u, attempt=%u",
                        slaveId, startAddr, attempt + 1);
        }
        else
        {
            const uint32_t startedAt = millis();
            while (millis() - startedAt < responseTimeoutMs)
            {
                mb->task();
                if (xSemaphoreTake(modbusResSem, 0) == pdTRUE)
                {
                    success = true;
                    break;
                }
                if (!mb->slave())
                {
                    break;
                }
                vTaskDelay(pdMS_TO_TICKS(1));
            }
        }

        if (success)
        {
            break;
        }

        if (mb->slave())
        {
            abortableMb->abortTransaction();
        }
        LOG_WARNING("Modbus bulk read failed: slave=%u, register=%u, count=%u, attempt=%u/%u",
                    slaveId, startAddr, count, attempt + 1, maxAttempts);
        if (attempt + 1 < maxAttempts)
        {
            vTaskDelay(pdMS_TO_TICKS(retryDelayMs));
        }
    }

    xSemaphoreGive(_busMutex);
    return success;
}
bool modbus_manager::writeModbusRegs(uint8_t slaveId, uint16_t startAddr,
                                     uint16_t count, uint16_t *dataBuffer,
                                     uint8_t maxAttempts,
                                     uint32_t responseTimeoutMs,
                                     uint32_t retryDelayMs)
{
    if (_mb_instances == nullptr || modbusResSem == NULL || dataBuffer == nullptr ||
        count == 0 || maxAttempts == 0)
        return false;
    if (xSemaphoreTake(_busMutex, pdMS_TO_TICKS(200)) != pdTRUE) {
        LOG_ERROR("Failed to take bus mutex for writeModbusRegs");
        return false;
    }

    ModbusRTU *mb = _mb_instances;
    auto *abortableMb = static_cast<AbortableModbusRTU *>(_mb_instances);
    bool success = false;

    for (uint8_t attempt = 0; attempt < maxAttempts; ++attempt)
    {
        while (xSemaphoreTake(modbusResSem, 0) == pdTRUE)
        {
        }
        while (_serial_ports != nullptr && _serial_ports->available() > 0)
        {
            _serial_ports->read();
        }
        if (mb->slave()) abortableMb->abortTransaction();

        if (mb->writeHreg(slaveId, startAddr, dataBuffer, count, cbRes))
        {
            const uint32_t startedAt = millis();
            while (millis() - startedAt < responseTimeoutMs)
            {
                mb->task();
                if (xSemaphoreTake(modbusResSem, 0) == pdTRUE)
                {
                    success = true;
                    break;
                }
                if (!mb->slave()) break;
                vTaskDelay(pdMS_TO_TICKS(1));
            }
        }

        if (success) break;
        if (mb->slave()) abortableMb->abortTransaction();
        LOG_WARNING("Modbus bulk write failed: slave=%u, register=%u, count=%u, attempt=%u/%u",
                    slaveId, startAddr, count, attempt + 1, maxAttempts);
        if (attempt + 1 < maxAttempts)
        {
            vTaskDelay(pdMS_TO_TICKS(retryDelayMs));
        }
    }

    xSemaphoreGive(_busMutex);
    return success;
}
