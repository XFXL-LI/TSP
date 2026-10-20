#include "SdMountSession.h"
#include "../diagnostics/ReadDeadline.h"
#include <Arduino.h>
#include <driver/sdmmc_host.h>
#include <diskio_impl.h>
#include <diskio_sdmmc.h>
#include <ff.h>
#include <stdlib.h>
#include <string.h>

namespace {
// Only active during the serialized startup/retry attempt. Normal file I/O
// retains the SDK's original transaction function and timeout behavior.
ReadDeadline mountIoDeadline;

esp_err_t mountTransaction(int slot, sdmmc_command_t *command) {
    const uint32_t remaining = mountIoDeadline.remainingMs(millis());
    if (remaining == 0) return ESP_ERR_TIMEOUT;
    const uint32_t originalTimeout = command->timeout_ms;
    const uint32_t commandLimit = originalTimeout != 0 &&
                                  originalTimeout < SdMountPolicy::COMMAND_TIMEOUT_MS
        ? originalTimeout : SdMountPolicy::COMMAND_TIMEOUT_MS;
    command->timeout_ms = remaining < commandLimit ? remaining : commandLimit;
    const esp_err_t result = sdmmc_host_do_transaction(slot, command);
    command->timeout_ms = originalTimeout;
    return result;
}
}

esp_err_t SdMountSession::mount(const char *basePath, gpio_num_t clk,
                               gpio_num_t cmd, gpio_num_t data0) {
    if (mounted_) return ESP_OK;

    mountIoDeadline = ReadDeadline::at(millis() + SdMountPolicy::IO_BUDGET_MS);
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.do_transaction = mountTransaction;
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 1;
    slot.clk = clk;
    slot.cmd = cmd;
    slot.d0 = data0;
    slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    bool hostInitialized = false;
    bool slotInitialized = false;
    bool diskRegistered = false;
    bool vfsRegistered = false;
    BYTE drive = FF_DRV_NOT_USED;
    char drivePath[3] = {};
    FATFS *filesystem = nullptr;
    memset(&card_, 0, sizeof(card_));

    esp_err_t result = ff_diskio_get_drive(&drive);
    if (result == ESP_OK) {
        drivePath[0] = static_cast<char>('0' + drive);
        drivePath[1] = ':';
        result = host.init();
        hostInitialized = result == ESP_OK;
    }
    if (result == ESP_OK) {
        result = sdmmc_host_init_slot(host.slot, &slot);
        slotInitialized = result == ESP_OK;
    }
    if (result == ESP_OK) {
        result = sdmmc_card_init(&host, &card_);
    }
    if (result == ESP_OK && !mountIoDeadline.fits(millis(), 1)) {
        result = ESP_ERR_TIMEOUT;
    }
    if (result == ESP_OK) {
        ff_diskio_register_sdmmc(drive, &card_);
        diskRegistered = true;
        ff_sdmmc_set_disk_status_check(drive, false);
        result = esp_vfs_fat_register(basePath, drivePath, 5, &filesystem);
        vfsRegistered = result == ESP_OK;
    }
    if (result == ESP_OK) {
        // Never format a missing, damaged or unformatted card.
        result = f_mount(filesystem, drivePath, 1) == FR_OK ? ESP_OK : ESP_FAIL;
    }
    if (result == ESP_OK && !mountIoDeadline.fits(millis(), 1)) {
        result = ESP_ERR_TIMEOUT;
    }
    if (result == ESP_OK) {
        // Do not apply the initialization budget to later measurement/pending
        // writes. Their existing SDK timeouts are intentionally unchanged.
        card_.host.do_transaction = sdmmc_host_do_transaction;
        mounted_ = true;
    } else {
        if (vfsRegistered) {
            f_mount(nullptr, drivePath, 0);
            esp_vfs_fat_unregister_path(basePath);
        }
        if (diskRegistered) ff_diskio_unregister(drive);
        if (hostInitialized) {
            // The SDK increments its slot reference count only after a
            // successful slot init. Do not decrement it on an early failure.
            if (slotInitialized) host.deinit_p(host.slot);
            else sdmmc_host_deinit();
        }
        if (card_.host.dma_aligned_buffer != nullptr) {
            free(card_.host.dma_aligned_buffer);
            card_.host.dma_aligned_buffer = nullptr;
        }
        memset(&card_, 0, sizeof(card_));
    }
    mountIoDeadline = {};
    return result;
}
