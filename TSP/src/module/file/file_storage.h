#ifndef FILE_STORAGE_H
#define FILE_STORAGE_H

#include <Arduino.h>
#include <FFat.h>
#include "driver/sdmmc_host.h"
#include "esp_vfs_fat.h"
#include <sys/stat.h>
#include <vector>

#ifndef SD_MOUNT_POINT

#define SD_MOUNT_POINT "/sdcard"

#endif

#define SD_CMD_IO               GPIO_NUM_48 
#define SD_CLK_IO               GPIO_NUM_47
#define SD_DAT0_IO              GPIO_NUM_21

struct DataRecord; 

class file_storage {
private:
    bool sd_card_initialized = false;
    bool ffat_initialized = false;

    file_storage();
    ~file_storage();

public:

    static file_storage &getInstance() {
        static file_storage instance;
        return instance;
    }

    file_storage(const file_storage &) = delete;
    file_storage &operator=(const file_storage &) = delete;

    int begin();
    bool FFatInit();
    bool isFFATReady() const { return ffat_initialized; }
    int readFFAT(const char *path, String &config_content);
    int writeFFAT(const char *path, const String &config_content);

    bool SDcardInit();
    bool isSDcardReady() const { return sd_card_initialized; }
    int readSDCard(const char *path, String &content);
    int writeSDCard(const char *path, const String &content, bool append = false);

    bool FFATsaveFile(const char *path);
    bool FFATremoveFile(const char *path);
    int makeDirs(const char *path);
};

#endif