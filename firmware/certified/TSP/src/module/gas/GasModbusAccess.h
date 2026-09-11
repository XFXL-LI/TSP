#pragma once

#include <stdint.h>
#include "../modbus/modbus_manager.h"

namespace GasModbusAccess {

// K-7S vendor requirement: leave at least 200 ms from the end of one gas
// transaction to the start of the next. Callers must hold SERIAL_485 for the
// complete helper call so normal collection and calibration share one clock.
static constexpr uint32_t MIN_QUERY_INTERVAL_MS = 200;

bool readRegisters(modbus_manager &manager, uint8_t slave, uint16_t address,
                   uint16_t count, uint16_t *values, uint8_t maxAttempts,
                   uint32_t responseTimeoutMs, const char *source);

bool writeRegisters(modbus_manager &manager, uint8_t slave, uint16_t address,
                    uint16_t count, uint16_t *values, uint8_t maxAttempts,
                    uint32_t responseTimeoutMs, const char *source);

} // namespace GasModbusAccess
