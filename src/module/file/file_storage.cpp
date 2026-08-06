#include "file_storage.h"
#include <stdio.h>
#include "esp_spiffs.h"
#include "esp_vfs_fat.h"
#include <nvs.h>
#include <nvs_flash.h>
#include "LittleFS.h"
#include "driver/sdmmc_host.h"
#include "driver/sdmmc_defs.h"
#include "driver/sdspi_host.h"
#include "../log/log_manager.h"
#include <Arduino.h>

file_storage::file_storage() : sd_card_initialized(false), ffat_initialized(false) {}
file_storage::~file_storage() {}

int file_storage::begin(){
    if(!FFatInit()){
        return 1;
    }
    if(!SDcardInit()){
        return 2;
    }
    return 0;
}

bool file_storage::FFatInit() {
    if (!FFat.begin(true)) {
        ffat_initialized = false;
        return false;
    }
    ffat_initialized = true;
    return true;
}

int file_storage::readFFAT(const char *path, String &config_content) {
    if (!ffat_initialized) return 1;

    if (!FFat.exists(path)) {
        return 2;
    }

    File file = FFat.open(path, FILE_READ);
    if (!file) {
        return 3;
    }

    config_content = file.readString();
    file.close();
    return 0;
}

int file_storage::writeFFAT(const char *path, const String &config_content) {
    if (!ffat_initialized) return 1;

    // Reject empty content before removing the existing valid config file.
    size_t expected = config_content.length();
    if (expected == 0) {
        LOG_ERROR("FFAT write rejected: empty content, path=%s", path);
        return 3;
    }

    if (FFat.exists(path)) {
        if (!FFat.remove(path)) {
            return 4;
        }
    }

    File file = FFat.open(path, FILE_WRITE);
    if (!file) {
        return 2;
    }

    size_t written = file.print(config_content);

    file.flush();
    file.close();

    if (written != expected) {
        LOG_ERROR(
            "FFAT write incomplete: path=%s expected=%u written=%u",
            path,
            (unsigned)expected,
            (unsigned)written
        );
        return 3;
    }

    LOG_INFO(
        "FFAT write success: path=%s bytes=%u",
        path,
        (unsigned)written
    );

    return 0;
}

bool file_storage::SDcardInit() {
    sdmmc_card_t *sdmmc_card = NULL;
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 8 * 1024};
    sdmmc_host_t sdmmc_host = SDMMC_HOST_DEFAULT();
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.width = 1;
    slot_config.clk = SD_CLK_IO;
    slot_config.cmd = SD_CMD_IO;
    slot_config.d0 = SD_DAT0_IO;
    slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
    esp_err_t ret =  esp_vfs_fat_sdmmc_mount(SD_MOUNT_POINT, &sdmmc_host, &slot_config, &mount_config, &sdmmc_card);

    if (ret != ESP_OK) {
        sd_card_initialized = false;
        return false;
    } else {
        sd_card_initialized = true;
        return true;
    }
}

int file_storage::writeSDCard(const char *path, const String &content, bool append) {
    if (!sd_card_initialized) return 1;

    makeDirs(path); 

    const char* mode = append ? "ab" : "wb";
    FILE *f = fopen(path, mode);
    if (f == NULL) {
        return 2;
    }
    
    size_t written = fwrite(content.c_str(), 1, content.length(), f);
    fclose(f);
    
    written == content.length();

    return 0;
}

int file_storage::readSDCard(const char *path, String &content) {
    if (!sd_card_initialized) return 1;

    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return 2;
    }

    // 获取文件大小
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *buf = (char *)malloc(size + 1);
    if (buf) {
        fread(buf, 1, size, f);
        buf[size] = '\0';
        content = String(buf);
        free(buf);
    }

    fclose(f);
    return 0;
}

int file_storage::makeDirs(const char *path) {
    if (path == nullptr || path[0] == '\0') {
        return 1;
    }

    char tmp[256];
    char *p = NULL;
    struct stat st;
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            if (stat(tmp, &st) != 0) {
                if (mkdir(tmp, S_IRWXU) != 0) {
                    return 1;
                }
            }
            *p = '/';
        }
    }
    if (stat(tmp, &st) != 0) {
        if (mkdir(tmp, S_IRWXU) != 0) {
            return 1;
        }
    }
    return 0;
}

bool file_storage::FFATremoveFile(const char *path){
    if (!ffat_initialized) {
        return false;
    }

    if (path == nullptr) return false;
    
    if (!FFat.remove(path)) {
        LOG_ERROR("Failed to delete existing config file!");
        return false;
    } else {
        return true;
    }
}
