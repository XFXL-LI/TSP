#pragma once
#include <sdmmc_cmd.h>
#include <driver/sdmmc_host.h>
#include <esp_vfs_fat.h>

namespace SdMountPolicy {
constexpr uint32_t RETRY_INTERVAL_MS = 60000;
constexpr uint32_t IO_BUDGET_MS = 3000;
constexpr uint32_t COMMAND_TIMEOUT_MS = 1000;

constexpr bool retryDue(bool attempted, bool mounted, uint32_t now,
                        uint32_t lastAttemptFinished) {
    return attempted && !mounted &&
           static_cast<uint32_t>(now - lastAttemptFinished) >= RETRY_INTERVAL_MS;
}
}

// One persistent card descriptor; failed attempts release only resources they
// acquired. Caller serializes mount/read/write access with the existing SD lock.
class SdMountSession {
public:
    esp_err_t mount(const char *basePath, gpio_num_t clk,
                    gpio_num_t cmd, gpio_num_t data0);

private:
    sdmmc_card_t card_ = {};
    bool mounted_ = false;
};
