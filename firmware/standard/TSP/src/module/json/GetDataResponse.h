#pragma once
#include <stddef.h>
#include <stdio.h>
#include <stdarg.h>
#include <math.h>
#include "../diagnostics/DeviceRuntimeStatus.h"
#include "../diagnostics/RealtimeSnapshotInfo.h"

namespace GetDataResponse {
constexpr size_t MAX_IDS = 16;
constexpr size_t BUFFER_CAPACITY = 1024;
constexpr size_t MAX_STATUS_WIRE_BYTES = 800;

inline bool append(char* buffer, size_t capacity, size_t& used,
                   const char* format, ...) {
    if (used >= capacity) return false;
    va_list args;
    va_start(args, format);
    const int written = vsnprintf(buffer + used, capacity - used, format, args);
    va_end(args);
    if (written < 0 || static_cast<size_t>(written) >= capacity - used) return false;
    used += static_cast<size_t>(written);
    return true;
}

// IDs are validated by the opt-in request path. This writes into the existing
// caller-owned buffer; no per-query JSON tree or complete factor map is cloned.
inline bool build(char* buffer, size_t capacity, size_t& used,
                  const char* const* ids, const float* values,
                  const uint8_t* decimals, size_t count, uint64_t timestamp,
                  int csq, float temp, float mete,
                  const RealtimeSnapshotInfo& sample,
                  const DeviceStatusSnapshot* device) {
    used = 0;
    if (count > MAX_IDS) return false;
    uint16_t mask = sample.validMask;
    if (device != nullptr && sample.seq == 0) mask = 0;
    bool ok = append(buffer, capacity, used,
        "{\"operation\":\"get_data\",\"code\":\"OK\","
        "\"message\":\"get data success\",\"timestamp\":%llu,"
        "\"csq\":%d,\"temp\":%.2f,\"mete\":%.2f,\"params\":[",
        static_cast<unsigned long long>(timestamp), csq,
        isfinite(temp) ? temp : 0.0f, isfinite(mete) ? mete : 0.0f);
    for (size_t i = 0; ok && i < count; ++i) {
        ok = append(buffer, capacity, used, "%s\"%s\"", i == 0 ? "" : ",", ids[i]);
    }
    ok = ok && append(buffer, capacity, used, "],\"values\":[");
    for (size_t i = 0; ok && i < count; ++i) {
        const bool finite = isfinite(values[i]);
        if (!finite) mask &= ~(1U << i);
        const bool usable = finite && (device == nullptr || (mask & (1U << i)));
        ok = append(buffer, capacity, used, "%s%.*f", i == 0 ? "" : ",",
                    decimals[i], usable ? values[i] : 0.0f);
    }
    ok = ok && append(buffer, capacity, used, "]");
    if (device != nullptr) {
        mask &= count == MAX_IDS ? UINT16_MAX : ((1U << count) - 1U);
        if (sample.seq == 0) mask = 0;
        ok = ok && append(buffer, capacity, used,
            ",\"ds\":{\"v\":1,\"init\":%u,\"sd\":%u,\"write\":%u,\"time\":%u,"
            "\"seq\":%u,\"age_ms\":%lld,\"valid_mask\":%u",
            static_cast<unsigned>(device->init), static_cast<unsigned>(device->sd),
            static_cast<unsigned>(device->write), static_cast<unsigned>(device->time),
            static_cast<unsigned>(sample.seq), static_cast<long long>(sample.ageMs),
            static_cast<unsigned>(mask));
        if (device->sd == 3 && device->hasSdError) {
            ok = ok && append(buffer, capacity, used, ",\"sd_error\":%ld",
                              static_cast<long>(device->sdError));
        }
        ok = ok && append(buffer, capacity, used, "}");
    }
    ok = ok && append(buffer, capacity, used, "}");
    // println adds CRLF outside the buffer; both bytes count toward the limit.
    return ok && (device == nullptr || used + 2 <= MAX_STATUS_WIRE_BYTES);
}
}
