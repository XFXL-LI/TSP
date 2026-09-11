#include "GasModbusAccess.h"
#include "../log/log_manager.h"
#include <Arduino.h>

namespace {

uint32_t lastGasTransactionEndMs = 0;
bool hasCompletedGasTransaction = false;

uint32_t waitForGasInterval(uint32_t &waitedMs, bool &hasPrevious)
{
    hasPrevious = hasCompletedGasTransaction;
    waitedMs = 0;
    if (hasCompletedGasTransaction) {
        const uint32_t waitStartedAt = millis();
        uint32_t elapsed = waitStartedAt - lastGasTransactionEndMs;
        while (elapsed < GasModbusAccess::MIN_QUERY_INTERVAL_MS) {
            const uint32_t remaining =
                GasModbusAccess::MIN_QUERY_INTERVAL_MS - elapsed;
            TickType_t ticks = pdMS_TO_TICKS(remaining);
            if (ticks == 0) ticks = 1;
            vTaskDelay(ticks);
            elapsed = millis() - lastGasTransactionEndMs;
        }
        waitedMs = millis() - waitStartedAt;
    }
    return hasCompletedGasTransaction
        ? millis() - lastGasTransactionEndMs : 0;
}

void finishGasTransaction(const char *source, const char *operation,
                          uint8_t slave, uint16_t address,
                          uint8_t attempt, uint8_t maxAttempts,
                          uint32_t startedAt, uint32_t gapMs,
                          uint32_t waitedMs, bool hasPrevious, bool ok)
{
    const uint32_t endedAt = millis();
    lastGasTransactionEndMs = endedAt;
    hasCompletedGasTransaction = true;
    LOG_DEBUG("[DIAG] GAS_QUERY source=%s op=%s slave=%u reg=0x%04X attempt=%u/%u first=%d gap_ms=%u wait_ms=%u duration_ms=%u ok=%d",
              source != nullptr ? source : "unknown", operation,
              (unsigned)slave, (unsigned)address,
              (unsigned)attempt, (unsigned)maxAttempts,
              hasPrevious ? 0 : 1, (unsigned)gapMs, (unsigned)waitedMs,
              (unsigned)(endedAt - startedAt), ok ? 1 : 0);
}

} // namespace

namespace GasModbusAccess {

bool readRegisters(modbus_manager &manager, uint8_t slave, uint16_t address,
                   uint16_t count, uint16_t *values, uint8_t maxAttempts,
                   uint32_t responseTimeoutMs, const char *source)
{
    if (values == nullptr || count == 0 || maxAttempts == 0) return false;
    for (uint8_t attempt = 1; attempt <= maxAttempts; ++attempt) {
        uint32_t waitedMs = 0;
        bool hasPrevious = false;
        const uint32_t gapMs = waitForGasInterval(waitedMs, hasPrevious);
        const uint32_t startedAt = millis();
        const bool ok = manager.readModbusRegs(
            slave, address, count, values, 1, responseTimeoutMs, 0);
        finishGasTransaction(source, "read", slave, address, attempt,
                             maxAttempts, startedAt, gapMs, waitedMs,
                             hasPrevious, ok);
        if (ok) return true;
    }
    return false;
}

bool writeRegisters(modbus_manager &manager, uint8_t slave, uint16_t address,
                    uint16_t count, uint16_t *values, uint8_t maxAttempts,
                    uint32_t responseTimeoutMs, const char *source)
{
    if (values == nullptr || count == 0 || maxAttempts == 0) return false;
    for (uint8_t attempt = 1; attempt <= maxAttempts; ++attempt) {
        uint32_t waitedMs = 0;
        bool hasPrevious = false;
        const uint32_t gapMs = waitForGasInterval(waitedMs, hasPrevious);
        const uint32_t startedAt = millis();
        const bool ok = manager.writeModbusRegs(
            slave, address, count, values, 1, responseTimeoutMs, 0);
        finishGasTransaction(source, "write", slave, address, attempt,
                             maxAttempts, startedAt, gapMs, waitedMs,
                             hasPrevious, ok);
        if (ok) return true;
    }
    return false;
}

} // namespace GasModbusAccess
