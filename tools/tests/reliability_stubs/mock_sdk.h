#pragma once
#include <stdint.h>
#include <stddef.h>

using esp_err_t = int;
constexpr esp_err_t ESP_OK = 0;
constexpr esp_err_t ESP_FAIL = -1;
constexpr esp_err_t ESP_ERR_TIMEOUT = 0x107;
constexpr esp_err_t ESP_ERR_NO_MEM = 0x101;
enum gpio_num_t { GPIO_NUM_21 = 21, GPIO_NUM_47 = 47, GPIO_NUM_48 = 48 };
struct sdmmc_command_t { uint32_t timeout_ms = 1000; };
struct sdmmc_host_t {
    int slot = 1;
    esp_err_t (*init)() = nullptr;
    esp_err_t (*deinit_p)(int) = nullptr;
    esp_err_t (*do_transaction)(int, sdmmc_command_t *) = nullptr;
    void *dma_aligned_buffer = nullptr;
};
struct sdmmc_card_t { sdmmc_host_t host; };
struct sdmmc_slot_config_t {
    int width = 4;
    gpio_num_t clk = GPIO_NUM_47, cmd = GPIO_NUM_48, d0 = GPIO_NUM_21;
    uint32_t flags = 0;
};
constexpr uint32_t SDMMC_SLOT_FLAG_INTERNAL_PULLUP = 1;
using BYTE = unsigned char;
constexpr BYTE FF_DRV_NOT_USED = 0xFF;
struct FATFS { int marker = 1; };
enum FRESULT { FR_OK, FR_DISK_ERR };

sdmmc_host_t mockDefaultHost();
#define SDMMC_HOST_DEFAULT() mockDefaultHost()
#define SDMMC_SLOT_CONFIG_DEFAULT() sdmmc_slot_config_t{}
esp_err_t sdmmc_host_do_transaction(int, sdmmc_command_t *);
esp_err_t sdmmc_host_deinit();
esp_err_t sdmmc_host_init_slot(int, const sdmmc_slot_config_t *);
esp_err_t sdmmc_card_init(const sdmmc_host_t *, sdmmc_card_t *);
esp_err_t ff_diskio_get_drive(BYTE *);
void ff_diskio_register_sdmmc(BYTE, sdmmc_card_t *);
void ff_diskio_unregister(BYTE);
void ff_sdmmc_set_disk_status_check(BYTE, bool);
esp_err_t esp_vfs_fat_register(const char *, const char *, size_t, FATFS **);
esp_err_t esp_vfs_fat_unregister_path(const char *);
FRESULT f_mount(FATFS *, const char *, BYTE);
