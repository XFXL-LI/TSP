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
#include "../diagnostics/DeviceRuntimeStatus.h"
#include "../diagnostics/SdIoDiagnostics.h"

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
    return writeFFAT(path, config_content.c_str());
}

int file_storage::writeFFAT(const char *path, const char *config_content) {
    if (!ffat_initialized) return 1;

    // 写入前先拒绝空内容，避免删除已有的有效配置文件。
    size_t expected = config_content != nullptr ? strlen(config_content) : 0;
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

    size_t written = file.write(
        reinterpret_cast<const uint8_t *>(config_content), expected);

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

int file_storage::writeFFATAtomic(const char *path, const char *config_content) {
    if (!ffat_initialized || path == nullptr || path[0] == '\0') return 1;

    const size_t expected = config_content != nullptr ? strlen(config_content) : 0;
    if (expected == 0) {
        LOG_ERROR("FFAT atomic write rejected: empty content, path=%s", path);
        return 2;
    }

    const String tempPath = String(path) + ".tmp";
    const String backupPath = String(path) + ".bak";
    if (FFat.exists(tempPath.c_str())) FFat.remove(tempPath.c_str());

    int writeResult = writeFFAT(tempPath.c_str(), config_content);
    if (writeResult != 0) return 3;

    String tempContent;
    if (readFFAT(tempPath.c_str(), tempContent) != 0 ||
        tempContent.length() != expected ||
        memcmp(tempContent.c_str(), config_content, expected) != 0) {
        FFat.remove(tempPath.c_str());
        LOG_ERROR("FFAT atomic temp verify failed: path=%s", path);
        return 4;
    }

    if (FFat.exists(backupPath.c_str())) FFat.remove(backupPath.c_str());
    const bool hadOriginal = FFat.exists(path);
    if (hadOriginal && !FFat.rename(path, backupPath.c_str())) {
        FFat.remove(tempPath.c_str());
        LOG_ERROR("FFAT atomic backup rename failed: path=%s", path);
        return 5;
    }

    if (!FFat.rename(tempPath.c_str(), path)) {
        if (hadOriginal) FFat.rename(backupPath.c_str(), path);
        FFat.remove(tempPath.c_str());
        LOG_ERROR("FFAT atomic replace failed: path=%s", path);
        return 6;
    }

    String finalContent;
    if (readFFAT(path, finalContent) != 0 ||
        finalContent.length() != expected ||
        memcmp(finalContent.c_str(), config_content, expected) != 0) {
        FFat.remove(path);
        if (hadOriginal) FFat.rename(backupPath.c_str(), path);
        LOG_ERROR("FFAT atomic final verify failed: path=%s", path);
        return 7;
    }

    if (hadOriginal) FFat.remove(backupPath.c_str());
    LOG_INFO("FFAT atomic write success: path=%s bytes=%u",
             path, (unsigned)expected);
    return 0;
}

bool file_storage::SDcardInit() {
    if (isSDcardReady()) return true;
    SdIoDiagnostics::Guard diagnostic("mount", SdIoDiagnostics::Skip::Mount);
    if (!diagnostic.entered()) return false;
    const bool isRetry = sd_mount_attempted.load(std::memory_order_acquire);
    const uint32_t startedMs = millis();
    const uint32_t freeBefore = ESP.getFreeHeap();
    DeviceRuntimeStatus::sdMountStarted();
    const esp_err_t ret = sd_mount_session.mount(
        SD_MOUNT_POINT, SD_CLK_IO, SD_CMD_IO, SD_DAT0_IO);
    sd_mount_finished_ms.store(millis(), std::memory_order_release);
    sd_mount_attempted.store(true, std::memory_order_release);
    sd_card_initialized.store(ret == ESP_OK, std::memory_order_release);
    DeviceRuntimeStatus::sdMountFinished(ret);
    LOG_INFO("[DIAG] SD_MOUNT_RESULT source=%s ok=%d error=0x%X elapsed_ms=%u io_budget_ms=%u free_before=%u free_after=%u largest=%u",
             isRetry ? "retry" : "boot", ret == ESP_OK ? 1 : 0,
             (unsigned)ret, (unsigned)(millis() - startedMs),
             (unsigned)SdMountPolicy::IO_BUDGET_MS, (unsigned)freeBefore,
             (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
    return ret == ESP_OK;
}

bool file_storage::isSDMountRetryDue(uint32_t nowMs) const {
    return SdMountPolicy::retryDue(
        sd_mount_attempted.load(std::memory_order_acquire), isSDcardReady(),
        nowMs, sd_mount_finished_ms.load(std::memory_order_acquire));
}

int file_storage::writeSDCard(const char *path, const String &content, bool append) {
    SdIoDiagnostics::Guard diagnostic("sd_write");
    if (!diagnostic.entered()) return 4;
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
    SdIoDiagnostics::Guard diagnostic("sd_read");
    if (!diagnostic.entered()) return 4;
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
