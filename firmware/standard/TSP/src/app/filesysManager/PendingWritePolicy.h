#pragma once
#include <stdint.h>
#include <stddef.h>

namespace PendingWritePolicy {
constexpr uint32_t STORAGE_QUEUE_DEPTH = 4; // One REAL/MIN/HOUR/DAY burst.
constexpr uint32_t CAPACITY = 2; // Includes the job currently writing.
constexpr size_t MAX_BYTES = 2048;
constexpr uint32_t RETRY_MS = 60000;
constexpr uint8_t MAX_ATTEMPTS = 2;
constexpr uint32_t POLL_MS = 100;
constexpr bool due(uint32_t now, uint32_t at) {
    return static_cast<int32_t>(now - at) >= 0;
}
}
