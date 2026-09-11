#pragma once

#include <stdint.h>

struct RuntimeMemorySnapshot {
    uint32_t freeHeap;
    uint32_t largestBlock;
    uint32_t minimumFreeHeap;
    uint32_t minimumLargestBlock;
};

RuntimeMemorySnapshot observeRuntimeMemory();
