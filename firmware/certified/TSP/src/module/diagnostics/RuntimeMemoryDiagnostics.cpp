#include "RuntimeMemoryDiagnostics.h"
#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"

namespace {

portMUX_TYPE memoryMinimumMux = portMUX_INITIALIZER_UNLOCKED;
uint32_t minimumFreeHeap = UINT32_MAX;
uint32_t minimumLargestBlock = UINT32_MAX;

} // namespace

RuntimeMemorySnapshot observeRuntimeMemory()
{
    RuntimeMemorySnapshot snapshot = {
        ESP.getFreeHeap(), ESP.getMaxAllocHeap(), 0, 0
    };
    taskENTER_CRITICAL(&memoryMinimumMux);
    if (snapshot.freeHeap < minimumFreeHeap) {
        minimumFreeHeap = snapshot.freeHeap;
    }
    if (snapshot.largestBlock < minimumLargestBlock) {
        minimumLargestBlock = snapshot.largestBlock;
    }
    snapshot.minimumFreeHeap = minimumFreeHeap;
    snapshot.minimumLargestBlock = minimumLargestBlock;
    taskEXIT_CRITICAL(&memoryMinimumMux);
    return snapshot;
}
