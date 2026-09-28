#pragma once
#include <stdint.h>

struct RealtimeSnapshotInfo {
    uint32_t seq = 0;
    int64_t ageMs = -1;
    uint16_t validMask = 0;
};

constexpr uint32_t nextRealtimeSequence(uint32_t seq) {
    return seq == UINT32_MAX ? 1 : seq + 1;
}

constexpr int64_t realtimeAgeMs(uint32_t seq, uint64_t sampledUs, uint64_t nowUs) {
    if (seq == 0) return -1;
    const uint64_t age = nowUs >= sampledUs ? (nowUs - sampledUs) / 1000ULL : 0;
    return age > UINT32_MAX ? UINT32_MAX : static_cast<int64_t>(age);
}
