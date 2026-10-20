#pragma once

#include <stdint.h>
#include "../modbus/modbus_manager.h"
#include "../diagnostics/ReadDeadline.h"

namespace GasModbusAccess {

// K-7S vendor requirement: leave at least 200 ms from the end of one gas
// transaction to the start of the next. Use a 500 ms operational margin.
// Callers must hold SERIAL_485 for the complete helper call so normal
// collection, status rechecks, and calibration share one clock.
static constexpr uint32_t MIN_QUERY_INTERVAL_MS = 500;
static constexpr uint8_t MAX_STATUS_READS = 3;
// Includes the Modbus manager's 200 ms mutex wait plus request wire time.
static constexpr uint32_t TRANSACTION_OVERHEAD_MS = 220;

bool readRegisters(modbus_manager &manager, uint8_t slave, uint16_t address,
                   uint16_t count, uint16_t *values, uint8_t maxAttempts,
                   uint32_t responseTimeoutMs, const char *source,
                   ReadDeadline deadline = {});

// Read K-7S status (0x6000) and concentration (0x6001). A successful Modbus
// response whose ready bit is clear is rechecked at most twice. Each recheck
// is one communication attempt; all normal reads must fit the pump deadline.
bool readStatusAndConcentration(modbus_manager &manager, uint8_t slave,
                                uint16_t *values, uint8_t maxAttempts,
                                uint32_t responseTimeoutMs,
                                const char *sensorId, const char *source,
                                ReadDeadline deadline = {});

bool writeRegisters(modbus_manager &manager, uint8_t slave, uint16_t address,
                    uint16_t count, uint16_t *values, uint8_t maxAttempts,
                    uint32_t responseTimeoutMs, const char *source);

} // namespace GasModbusAccess
