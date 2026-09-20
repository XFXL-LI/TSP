#pragma once

#include <stdint.h>
#include "../modbus/modbus_manager.h"

namespace GasModbusAccess {

// K-7S vendor requirement: leave at least 200 ms from the end of one gas
// transaction to the start of the next. Use a 500 ms operational margin.
// Callers must hold SERIAL_485 for the complete helper call so normal
// collection, status rechecks, and calibration share one clock.
static constexpr uint32_t MIN_QUERY_INTERVAL_MS = 500;

bool readRegisters(modbus_manager &manager, uint8_t slave, uint16_t address,
                   uint16_t count, uint16_t *values, uint8_t maxAttempts,
                   uint32_t responseTimeoutMs, const char *source);

// Read K-7S status (0x6000) and concentration (0x6001). A successful Modbus
// response whose ready bit is clear is rechecked exactly once. The shared
// transaction clock above provides the 500 ms wait before that recheck.
bool readStatusAndConcentration(modbus_manager &manager, uint8_t slave,
                                uint16_t *values, uint8_t maxAttempts,
                                uint32_t responseTimeoutMs,
                                const char *sensorId, const char *source);

bool writeRegisters(modbus_manager &manager, uint8_t slave, uint16_t address,
                    uint16_t count, uint16_t *values, uint8_t maxAttempts,
                    uint32_t responseTimeoutMs, const char *source);

} // namespace GasModbusAccess
