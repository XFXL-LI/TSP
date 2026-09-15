#include "filesysManager.h"
#include "../../system/ota/remote_ota_manager.h"
#include "../../module/log/log_manager.h"
#include "../../module/pack212/pack212.h"
#include "../../module/diagnostics/RuntimeMemoryDiagnostics.h"
#include "../../system/event/eventBus.h"
#include "../../module/json/config_json.h"
#include <sys/dirent.h>
#include <sys/types.h>
#include <time.h>
#include <utility>
#include <new>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <atomic>

static constexpr char PENDING_REBUILD_MARKER[] = "REBUILD_FROM_SD";

namespace {

std::atomic<uint32_t> pendingWriteFailureIncidents{0};
std::atomic<uint32_t> pendingZeroPrefixIncidents{0};

static constexpr size_t PENDING_DIAG_PREFIX_BYTES = 16;
static constexpr size_t PENDING_DIRECT_SECTOR_BYTES = 512;
static constexpr size_t PENDING_POSIX_CHUNK_BYTES = 256;
static constexpr int PENDING_STAGE_NOT_RUN = -2;

uint32_t pendingCrc32Update(uint32_t crc, const uint8_t* data, size_t length)
{
    while (length-- > 0) {
        crc ^= *data++;
        for (uint8_t bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1U) ^ ((crc & 1U) != 0U ? 0xEDB88320UL : 0UL);
        }
    }
    return crc;
}

uint32_t pendingCrc32(const uint8_t* data, size_t length)
{
    return pendingCrc32Update(0xFFFFFFFFUL, data, length) ^ 0xFFFFFFFFUL;
}

struct PendingVerifyDetails {
    bool ok = false;
    size_t fileSize = 0;
    size_t mismatchOffset = static_cast<size_t>(-1);
    int expectedByte = -1;
    int actualByte = -1;
    size_t zeroCountFirst128 = 0;
    size_t zeroCountFirst512 = 0;
    uint32_t memoryCrc = 0;
    uint32_t fileCrc = 0;
    uint32_t memoryFirst512Crc = 0;
    uint32_t fileFirst512Crc = 0;
    uint8_t filePrefix[PENDING_DIAG_PREFIX_BYTES] = {0};
    size_t filePrefixLength = 0;
    int openError = 0;
    int readError = 0;
};

struct PendingWriteDetails {
    bool opened = false;
    size_t written = 0;
    unsigned writeCalls = 0;
    int openError = 0;
    int writeError = 0;
    int flushResult = PENDING_STAGE_NOT_RUN;
    int flushError = 0;
    int syncResult = PENDING_STAGE_NOT_RUN;
    int syncError = 0;
    int closeResult = PENDING_STAGE_NOT_RUN;
    int closeError = 0;
};

void formatPendingHexPrefix(const uint8_t* data, size_t length,
                            char output[PENDING_DIAG_PREFIX_BYTES * 2 + 1])
{
    static constexpr char HEX_DIGITS[] = "0123456789ABCDEF";
    const size_t count = length < PENDING_DIAG_PREFIX_BYTES
        ? length : PENDING_DIAG_PREFIX_BYTES;
    for (size_t index = 0; index < count; ++index) {
        output[index * 2] = HEX_DIGITS[(data[index] >> 4U) & 0x0FU];
        output[index * 2 + 1] = HEX_DIGITS[data[index] & 0x0FU];
    }
    output[count * 2] = '\0';
}

void logPendingWriteDetails(int attempt, const char* method,
                            const PendingWriteDetails& details,
                            size_t expectedLength)
{
    LOG_ERROR("[DIAG] PENDING_IO attempt=%d method=%s open=%d open_errno=%d written=%u/%u calls=%u write_errno=%d",
              attempt, method, details.opened ? 1 : 0, details.openError,
              (unsigned)details.written, (unsigned)expectedLength,
              details.writeCalls, details.writeError);
    LOG_ERROR("[DIAG] PENDING_IO_STAGE attempt=%d flush=%d errno=%d fsync=%d errno=%d close=%d errno=%d",
              attempt, details.flushResult, details.flushError,
              details.syncResult, details.syncError,
              details.closeResult, details.closeError);
}

bool removePendingTempFile(const String& path, const char* phase)
{
    errno = 0;
    if (remove(path.c_str()) == 0) return true;
    const int removeError = errno;
    if (removeError == ENOENT) return true;
    LOG_ERROR("[DIAG] PENDING_TEMP_REMOVE_FAIL phase=%s errno=%d path=%s",
              phase, removeError, path.c_str());
    return false;
}

PendingVerifyDetails verifyPendingFile(const String& path,
                                       const uint8_t* expected,
                                       size_t expectedLength)
{
    PendingVerifyDetails details;
    details.memoryCrc = pendingCrc32(expected, expectedLength);
    const size_t memoryFirst512Length = expectedLength < PENDING_DIRECT_SECTOR_BYTES
        ? expectedLength : PENDING_DIRECT_SECTOR_BYTES;
    details.memoryFirst512Crc = pendingCrc32(expected, memoryFirst512Length);
    errno = 0;
    FILE* file = fopen(path.c_str(), "rb");
    if (file == nullptr) {
        details.openError = errno;
        return details;
    }

    uint32_t fileCrc = 0xFFFFFFFFUL;
    uint32_t fileFirst512Crc = 0xFFFFFFFFUL;
    uint8_t buffer[128];
    while (true) {
        const size_t got = fread(buffer, 1, sizeof(buffer), file);
        if (got == 0) break;
        fileCrc = pendingCrc32Update(fileCrc, buffer, got);
        if (details.fileSize < PENDING_DIRECT_SECTOR_BYTES) {
            const size_t first512Remaining =
                PENDING_DIRECT_SECTOR_BYTES - details.fileSize;
            const size_t first512Bytes = got < first512Remaining
                ? got : first512Remaining;
            fileFirst512Crc = pendingCrc32Update(
                fileFirst512Crc, buffer, first512Bytes);
        }
        for (size_t index = 0; index < got; ++index) {
            const size_t absolute = details.fileSize + index;
            if (absolute < PENDING_DIAG_PREFIX_BYTES) {
                details.filePrefix[absolute] = buffer[index];
                details.filePrefixLength = absolute + 1;
            }
            if (absolute < 128U && buffer[index] == 0U) {
                ++details.zeroCountFirst128;
            }
            if (absolute < PENDING_DIRECT_SECTOR_BYTES && buffer[index] == 0U) {
                ++details.zeroCountFirst512;
            }
            if (details.mismatchOffset == static_cast<size_t>(-1) &&
                (absolute >= expectedLength || buffer[index] != expected[absolute])) {
                details.mismatchOffset = absolute;
                details.expectedByte = absolute < expectedLength
                    ? static_cast<int>(expected[absolute]) : -1;
                details.actualByte = static_cast<int>(buffer[index]);
            }
        }
        details.fileSize += got;
    }
    details.readError = ferror(file) != 0 ? errno : 0;
    fclose(file);
    details.fileCrc = fileCrc ^ 0xFFFFFFFFUL;
    details.fileFirst512Crc = fileFirst512Crc ^ 0xFFFFFFFFUL;

    if (details.mismatchOffset == static_cast<size_t>(-1) &&
        details.fileSize < expectedLength) {
        details.mismatchOffset = details.fileSize;
        details.expectedByte = static_cast<int>(expected[details.fileSize]);
        details.actualByte = -1;
    }
    details.ok = details.readError == 0 &&
                 details.fileSize == expectedLength &&
                 details.mismatchOffset == static_cast<size_t>(-1) &&
                 details.memoryCrc == details.fileCrc;
    return details;
}

bool writePendingStdio(const String& path, const uint8_t* data, size_t length,
                       PendingWriteDetails& details)
{
    errno = 0;
    FILE* file = fopen(path.c_str(), "wb");
    if (file == nullptr) {
        details.openError = errno;
        return false;
    }
    details.opened = true;
    ++details.writeCalls;
    errno = 0;
    details.written = fwrite(data, 1, length, file);
    const bool writeOk = details.written == length && ferror(file) == 0;
    if (!writeOk) details.writeError = errno;

    if (writeOk) {
        errno = 0;
        details.flushResult = fflush(file);
        if (details.flushResult != 0) details.flushError = errno;
    }
    if (details.flushResult == 0) {
        errno = 0;
        const int descriptor = fileno(file);
        details.syncResult = descriptor >= 0 ? fsync(descriptor) : -1;
        if (details.syncResult != 0) details.syncError = errno;
    }
    errno = 0;
    details.closeResult = fclose(file);
    if (details.closeResult != 0) details.closeError = errno;
    return writeOk && details.flushResult == 0 &&
           details.syncResult == 0 && details.closeResult == 0;
}

bool writePendingPosix(const String& path, const uint8_t* data, size_t length,
                       PendingWriteDetails& details)
{
    errno = 0;
    const int descriptor = open(
        path.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0666);
    if (descriptor < 0) {
        details.openError = errno;
        return false;
    }
    details.opened = true;
    while (details.written < length) {
        const size_t remaining = length - details.written;
        const size_t request = remaining < PENDING_POSIX_CHUNK_BYTES
            ? remaining : PENDING_POSIX_CHUNK_BYTES;
        errno = 0;
        ++details.writeCalls;
        const ssize_t result = write(
            descriptor, data + details.written, request);
        if (result > 0) {
            details.written += static_cast<size_t>(result);
            continue;
        }
        if (result < 0 && errno == EINTR) continue;
        details.writeError = errno;
        break;
    }
    if (details.written == length) {
        errno = 0;
        details.syncResult = fsync(descriptor);
        if (details.syncResult != 0) details.syncError = errno;
    }
    errno = 0;
    details.closeResult = close(descriptor);
    if (details.closeResult != 0) details.closeError = errno;
    return details.written == length && details.syncResult == 0 &&
           details.closeResult == 0;
}

} // namespace

static void setPendingReadStatus(PendingReadStatus* status,
                                 PendingReadStatus value) {
    if (status != nullptr) {
        *status = value;
    }
}

class ScopedSdLock {
public:
    explicit ScopedSdLock(SemaphoreHandle_t mutex, TickType_t timeout = pdMS_TO_TICKS(3000))
        : _mutex(mutex), _locked(mutex != nullptr && xSemaphoreTake(mutex, timeout) == pdTRUE) {}
    ~ScopedSdLock() { if (_locked) xSemaphoreGive(_mutex); }
    bool locked() const { return _locked; }
private:
    SemaphoreHandle_t _mutex;
    bool _locked;
};

filesysManager& filesysManager::getInstance() {
    static filesysManager instance;
    return instance;
}

filesysManager::filesysManager() {
    _sdMutex = xSemaphoreCreateMutex();
    if (SaveDataFileTaskQueue == nullptr) {
        SaveDataFileTaskQueue = EventBus::getInstance().createReceiverQueue(20, "STORAGE");
    }
    EventBus::getInstance().subscribe(EventID::PROCESSED_DATA_COLLECTED, SaveDataFileTaskQueue);
    EventBus::getInstance().subscribe(EventID::RECORD_QUERY_REQ, SaveDataFileTaskQueue);
}
void filesysManager::parseTimestamp(uint64_t ts, char* date, char* hour, char* min) {
    uint64_t ymd = ts / 1000000; 
    uint8_t h = (ts / 10000) % 100;
    uint8_t m = (ts / 100) % 100;

    sprintf(date, "%llu", ymd); 
    sprintf(hour, "%02d", h);
    sprintf(min, "%02d", m);
}
// 20260515115800
String filesysManager::getFilePath(int type, uint64_t ts) {
    char dateStr[10], hourStr[5], minStr[5];
    parseTimestamp(ts, dateStr, hourStr, minStr);

    String basePath = "/sdcard/" + String(dateStr);
    String finalPath;

    switch(type) {
        case 0: // ʵʱ: /sdcard/20260427/raw/14/01.dat
            finalPath = basePath + "/raw/" + hourStr + "/" + minStr + ".dat";
            break;
        case 1: // ����: /sdcard/20260427/min/14/01.dat
            finalPath = basePath + "/min/" + hourStr + "/" + minStr + ".dat";
            break;
        case 2: // Сʱ: /sdcard/20260427/hour/14.dat
            finalPath = basePath + "/hour/" + hourStr + ".dat";
            break;
        case 3: // ��:   /sdcard/20260427/day/day.dat
            finalPath = basePath + "/day/day.dat";
            break;
        default:
            finalPath = "/sdcard/history/other.dat";
            break;
    }
    return finalPath;
}

void filesysManager::storeProcessedPacket(AllProcessedDataPacket* pkg) {
    if (!pkg) return;
    // Firmware 2.0.3: serialize FAT access across storage, LCD history reads,
    // and pending recovery. Concurrent stdio calls produced zero-filled files.
    ScopedSdLock sdLock(_sdMutex);
    if (!sdLock.locked()) {
        LOG_WARNING("[DIAG] SD_LOCK_BUSY operation=store");
        return;
    }
    if (!file_storage::getInstance().isSDcardReady()) {
        LOG_ERROR("[DIAG] STORE trace=%u type=%u timestamp=%llu skipped=sd_not_ready",
                  (unsigned)pkg->trace_id,
                  (unsigned)pkg->dataTime,
                  pkg->last_update);
        return;
    }
    String path = getFilePath((int)pkg->dataTime, pkg->last_update);
    historyData.clear();
    for (auto const& [id, data] : pkg->processed_data_map) {
        fileStorage rec;
        rec.timestamp = pkg->last_update;
        memset(rec.sensor_id, 0, sizeof(rec.sensor_id));
        strncpy(rec.sensor_id, id.c_str(), sizeof(rec.sensor_id) - 1);
        
        rec.value = data.value;
        rec.min_val = data.min_val;
        rec.max_val = data.max_val;
        rec.cou_val = data.cou_val;
        rec.is_valid = data.is_valid ? 1 : 0;
        historyData.push_back(rec);
    }
    bool ok = writeToFile(path, historyData);
    LOG_INFO("[DIAG] STORE trace=%u type=%u timestamp=%llu records=%u ok=%d path=%s",
             (unsigned)pkg->trace_id,
             (unsigned)pkg->dataTime,
             pkg->last_update,
             (unsigned)historyData.size(),
             ok ? 1 : 0,
             path.c_str());
}

bool filesysManager::writeToFile(const String& path, const std::vector<fileStorage>& rec) {
    if (rec.empty()) return false;

    int lastSlash = path.lastIndexOf('/');
    if (lastSlash != -1) {
        String dirPath = path.substring(0, lastSlash);
        int res = file_storage::getInstance().makeDirs(dirPath.c_str());
        
        if (res == 0) {
            FILE* f = fopen(path.c_str(), "ab");
            if (f) {
                size_t written = fwrite(rec.data(), sizeof(fileStorage), rec.size(), f);
                
                fclose(f);

                if (written == rec.size()) {
                    LOG_DEBUG("Successfully saved %d records to: %s", (int)written, path.c_str());
                    return true;
                } else {
                    LOG_ERROR("Write size mismatch! Expected %d, wrote %d", rec.size(), written);
                }
            } else {
                LOG_ERROR("Failed to open file: %s", path.c_str());
            }
        } else {
            LOG_ERROR("Failed to make file path: %s", path.c_str());
        }
    }
    return false;
}

String filesysManager::getPendingFilePath(DataTime type, uint64_t ts) {
    return "/sdcard/pending/" + String((int)type) + "/" + String(ts) + ".pkt";
}

bool filesysManager::savePendingPacket(const AllProcessedDataPacket* data, const String& packet) {
    if (data == nullptr || packet.length() == 0 ||
        !file_storage::getInstance().isSDcardReady()) {
        return false;
    }
    ScopedSdLock sdLock(_sdMutex);
    if (!sdLock.locked()) {
        LOG_WARNING("[DIAG] SD_LOCK_BUSY operation=save_pending");
        return false;
    }
    // Firmware 2.0.2 (2026-07-30): never persist truncated/OOM packets such
    // as the observed 14-byte "##0002&&..." frame.
    if (!HJ212_DataCenter::isValidPacket(packet)) {
        LOG_ERROR("[DIAG] PENDING_REJECT type=%u timestamp=%llu bytes=%u reason=invalid_hj212",
                  (unsigned)data->dataTime, data->last_update,
                  (unsigned)packet.length());
        return false;
    }

    String path = getPendingFilePath(data->dataTime, data->last_update);
    int lastSlash = path.lastIndexOf('/');
    if (lastSlash != -1) {
        String dirPath = path.substring(0, lastSlash);
        if (file_storage::getInstance().makeDirs(dirPath.c_str()) != 0) {
            LOG_ERROR("Failed to create pending directory: %s", dirPath.c_str());
            return false;
        }

        // Keep the first failed file allocated while the second path is
        // written. The POSIX fallback uses sub-sector chunks so that FatFs
        // assembles the 512-byte sector in its own file cache.
        String tempPath1 = path + ".tmp1";
        String tempPath2 = path + ".tmp2";
        const bool temp1Ready = removePendingTempFile(tempPath1, "startup");
        const bool temp2Ready = removePendingTempFile(tempPath2, "startup");
        if (!temp1Ready || !temp2Ready) return false;

        bool verified = false;
        bool zeroPrefixObserved = false;
        int verifiedAttempt = 0;
        String verifiedTempPath;
        for (int attempt = 1; attempt <= 2 && !verified; ++attempt) {
            const String& tempPath = attempt == 1 ? tempPath1 : tempPath2;
            PendingWriteDetails writeDetails;
            const bool writeOk = attempt == 1
                ? writePendingStdio(
                    tempPath,
                    reinterpret_cast<const uint8_t*>(packet.c_str()),
                    packet.length(), writeDetails)
                : writePendingPosix(
                    tempPath,
                    reinterpret_cast<const uint8_t*>(packet.c_str()),
                    packet.length(), writeDetails);
            const char* method = attempt == 1 ? "stdio" : "posix_chunked";
            if (!writeOk) {
                LOG_ERROR("[DIAG] PENDING_WRITE_FAIL attempt=%d method=%s stage=commit bytes=%u temp=%s",
                          attempt, method, (unsigned)packet.length(),
                          tempPath.c_str());
                logPendingWriteDetails(
                    attempt, method, writeDetails, packet.length());
            } else {
                const PendingVerifyDetails details = verifyPendingFile(
                    tempPath,
                    reinterpret_cast<const uint8_t*>(packet.c_str()),
                    packet.length());
                const size_t inspectedPrefix = packet.length() < 128U
                    ? packet.length() : 128U;
                if (inspectedPrefix > 0 &&
                    details.fileSize >= inspectedPrefix &&
                    details.zeroCountFirst128 == inspectedPrefix) {
                    zeroPrefixObserved = true;
                }
                if (details.ok) {
                    verified = true;
                    verifiedAttempt = attempt;
                    verifiedTempPath = tempPath;
                    LOG_INFO("[DIAG] PENDING_WRITE_VERIFIED attempt=%d method=%s bytes=%u crc32=%08X",
                             attempt, method, (unsigned)details.fileSize,
                             (unsigned)details.fileCrc);
                    break;
                }

                const unsigned mismatch =
                    details.mismatchOffset == static_cast<size_t>(-1)
                    ? 0xFFFFFFFFU
                    : static_cast<unsigned>(details.mismatchOffset);
                char memoryPrefix[PENDING_DIAG_PREFIX_BYTES * 2 + 1];
                char filePrefix[PENDING_DIAG_PREFIX_BYTES * 2 + 1];
                formatPendingHexPrefix(
                    reinterpret_cast<const uint8_t*>(packet.c_str()),
                    packet.length(), memoryPrefix);
                formatPendingHexPrefix(
                    details.filePrefix, details.filePrefixLength, filePrefix);
                LOG_ERROR("[DIAG] PENDING_VERIFY_FAIL attempt=%d method=%s expected=%u file=%u mismatch=%u expected_byte=%d actual_byte=%d",
                          attempt, method, (unsigned)packet.length(),
                          (unsigned)details.fileSize, mismatch,
                          details.expectedByte, details.actualByte);
                LOG_ERROR("[DIAG] PENDING_VERIFY_CRC attempt=%d memory=%08X file=%08X first512_memory=%08X first512_file=%08X",
                          attempt, (unsigned)details.memoryCrc,
                          (unsigned)details.fileCrc,
                          (unsigned)details.memoryFirst512Crc,
                          (unsigned)details.fileFirst512Crc);
                LOG_ERROR("[DIAG] PENDING_VERIFY_ZERO attempt=%d zero_first128=%u zero_first512=%u open_errno=%d read_errno=%d",
                          attempt, (unsigned)details.zeroCountFirst128,
                          (unsigned)details.zeroCountFirst512,
                          details.openError, details.readError);
                LOG_ERROR("[DIAG] PENDING_VERIFY_HEAD attempt=%d source=memory hex=%s",
                          attempt, memoryPrefix);
                LOG_ERROR("[DIAG] PENDING_VERIFY_HEAD attempt=%d source=file hex=%s",
                          attempt, filePrefix);
                LOG_ERROR("[DIAG] PENDING_VERIFY_PATH attempt=%d temp=%s",
                          attempt, tempPath.c_str());
                logPendingWriteDetails(
                    attempt, method, writeDetails, packet.length());
            }

            if (attempt == 1) {
                LOG_WARNING("[DIAG] PENDING_WRITE_FALLBACK from=%s to=%s chunk=%u",
                            tempPath1.c_str(), tempPath2.c_str(),
                            (unsigned)PENDING_POSIX_CHUNK_BYTES);
                vTaskDelay(pdMS_TO_TICKS(20));
            }
        }
        if (!verified) {
            removePendingTempFile(tempPath1, "both_failed");
            removePendingTempFile(tempPath2, "both_failed");
            const uint32_t failures =
                pendingWriteFailureIncidents.fetch_add(
                    1, std::memory_order_acq_rel) + 1;
            uint32_t zeroPrefixes =
                pendingZeroPrefixIncidents.load(std::memory_order_acquire);
            if (zeroPrefixObserved) {
                zeroPrefixes = pendingZeroPrefixIncidents.fetch_add(
                    1, std::memory_order_acq_rel) + 1;
            }
            const RuntimeMemorySnapshot memory = observeRuntimeMemory();
            LOG_ERROR("[DIAG] PENDING_WRITE_HEALTH failures=%u zero_prefix=%u type=%u timestamp=%llu free=%u largest=%u min_free=%u min_largest=%u",
                      (unsigned)failures, (unsigned)zeroPrefixes,
                      (unsigned)data->dataTime, data->last_update,
                      (unsigned)memory.freeHeap, (unsigned)memory.largestBlock,
                      (unsigned)memory.minimumFreeHeap,
                      (unsigned)memory.minimumLargestBlock);
            return false;
        }

        // Do not replace a prior final packet blindly. The caller will fall
        // back to the rebuild-marker path, which preserves any complete file.
        FILE* existing = fopen(path.c_str(), "rb");
        if (existing != nullptr) {
            fclose(existing);
            removePendingTempFile(tempPath1, "destination_exists");
            removePendingTempFile(tempPath2, "destination_exists");
            LOG_WARNING("[DIAG] PENDING_WRITE_FAIL stage=destination_exists path=%s",
                        path.c_str());
            return false;
        }

        errno = 0;
        if (rename(verifiedTempPath.c_str(), path.c_str()) != 0) {
            const int renameError = errno;
            removePendingTempFile(tempPath1, "rename_failed");
            removePendingTempFile(tempPath2, "rename_failed");
            LOG_ERROR("[DIAG] PENDING_WRITE_FAIL stage=rename path=%s temp=%s errno=%d",
                      path.c_str(), verifiedTempPath.c_str(), renameError);
            return false;
        }

        const String& unusedTempPath = verifiedAttempt == 1
            ? tempPath2 : tempPath1;
        removePendingTempFile(unusedTempPath, "post_success");

        LOG_INFO("Saved and verified pending HJ212 packet: %s", path.c_str());
        return true;
    }
    return false;
}

bool filesysManager::savePendingRebuildMarker(const AllProcessedDataPacket* data) {
    if (data == nullptr || !file_storage::getInstance().isSDcardReady()) {
        return false;
    }
    ScopedSdLock sdLock(_sdMutex);
    if (!sdLock.locked()) {
        LOG_WARNING("[DIAG] SD_LOCK_BUSY operation=save_rebuild_marker");
        return false;
    }

    String path = getPendingFilePath(data->dataTime, data->last_update);
    int lastSlash = path.lastIndexOf('/');
    if (lastSlash == -1) return false;

    String dirPath = path.substring(0, lastSlash);
    if (file_storage::getInstance().makeDirs(dirPath.c_str()) != 0) {
        LOG_ERROR("Failed to create pending rebuild directory: %s", dirPath.c_str());
        return false;
    }

    // Never overwrite a complete pending HJ212 packet for the same timestamp.
    // A short non-marker file is a failed prior write and is replaced by the
    // rebuild marker so that the already-saved SD record remains recoverable.
    FILE* existing = fopen(path.c_str(), "rb");
    if (existing != nullptr) {
        long existingSize = -1;
        if (fseek(existing, 0, SEEK_END) == 0) {
            existingSize = ftell(existing);
        }
        bool markerMatches = false;
        bool completePacketMatches = false;
        bool existingReadable = true;
        if (existingSize == (long)(sizeof(PENDING_REBUILD_MARKER) - 1) &&
            fseek(existing, 0, SEEK_SET) == 0) {
            char existingMarker[sizeof(PENDING_REBUILD_MARKER)] = {0};
            size_t got = fread(existingMarker, 1,
                               sizeof(PENDING_REBUILD_MARKER) - 1, existing);
            markerMatches =
                got == sizeof(PENDING_REBUILD_MARKER) - 1 &&
                memcmp(existingMarker, PENDING_REBUILD_MARKER,
                       sizeof(PENDING_REBUILD_MARKER) - 1) == 0;
        }
        if (!markerMatches && existingSize > 0 && existingSize <= 10011L &&
            fseek(existing, 0, SEEK_SET) == 0) {
            char *existingBytes = new (std::nothrow) char[existingSize + 1];
            if (existingBytes == nullptr) {
                existingReadable = false;
            } else {
                const size_t got = fread(existingBytes, 1,
                                         static_cast<size_t>(existingSize),
                                         existing);
                existingBytes[got] = '\0';
                if (got == static_cast<size_t>(existingSize) &&
                    ferror(existing) == 0) {
                    String existingPacket(existingBytes);
                    existingReadable = existingPacket.length() == got;
                    if (existingReadable) {
                        completePacketMatches =
                            HJ212_DataCenter::isValidPacket(existingPacket);
                    }
                } else {
                    existingReadable = false;
                }
                delete[] existingBytes;
            }
        }
        fclose(existing);
        if (markerMatches || completePacketMatches) {
            LOG_INFO("[DIAG] PENDING_MARKER_EXISTS type=%u timestamp=%llu bytes=%ld path=%s",
                     (unsigned)data->dataTime, data->last_update,
                     existingSize, path.c_str());
            return true;
        }
        if (!existingReadable) {
            LOG_ERROR("[DIAG] PENDING_EXISTING_CHECK_FAIL type=%u timestamp=%llu bytes=%ld path=%s",
                      (unsigned)data->dataTime, data->last_update,
                      existingSize, path.c_str());
            return false;
        }
        LOG_WARNING("[DIAG] PENDING_EXISTING_INVALID type=%u timestamp=%llu bytes=%ld action=replace_with_marker path=%s",
                    (unsigned)data->dataTime, data->last_update,
                    existingSize, path.c_str());
        if (remove(path.c_str()) != 0) {
            LOG_ERROR("Failed to replace incomplete pending marker: %s",
                      path.c_str());
            return false;
        }
    }

    FILE* f = fopen(path.c_str(), "wb");
    if (f == nullptr) {
        LOG_ERROR("Failed to create pending rebuild marker: %s", path.c_str());
        return false;
    }
    size_t written = fwrite(PENDING_REBUILD_MARKER, 1,
                            sizeof(PENDING_REBUILD_MARKER) - 1, f);
    fclose(f);
    if (written != sizeof(PENDING_REBUILD_MARKER) - 1) {
        remove(path.c_str());
        LOG_ERROR("Pending rebuild marker write size mismatch: %s", path.c_str());
        return false;
    }

    FILE* verify = fopen(path.c_str(), "rb");
    bool matches = verify != nullptr;
    char markerBuffer[sizeof(PENDING_REBUILD_MARKER)] = {0};
    if (matches) {
        size_t got = fread(markerBuffer, 1,
                           sizeof(PENDING_REBUILD_MARKER) - 1, verify);
        int trailing = fgetc(verify);
        matches = got == sizeof(PENDING_REBUILD_MARKER) - 1 &&
                  trailing == EOF &&
                  memcmp(markerBuffer, PENDING_REBUILD_MARKER,
                         sizeof(PENDING_REBUILD_MARKER) - 1) == 0;
    }
    if (verify != nullptr) fclose(verify);
    if (!matches) {
        remove(path.c_str());
        LOG_ERROR("[DIAG] PENDING_REBUILD_VERIFY_FAIL type=%u timestamp=%llu path=%s",
                  (unsigned)data->dataTime, data->last_update, path.c_str());
        return false;
    }

    LOG_INFO("[DIAG] PENDING_REBUILD_MARKER type=%u timestamp=%llu path=%s",
             (unsigned)data->dataTime, data->last_update, path.c_str());
    return true;
}

PendingProtectResult filesysManager::protectPendingPacket(
    const AllProcessedDataPacket* data,
    const String& packet) {
    if (data == nullptr) {
        LOG_ERROR("[DIAG] PENDING_PROTECT outcome=unprotected reason=null_data");
        return PendingProtectResult::UNPROTECTED;
    }

    if (packet.length() > 0 && savePendingPacket(data, packet)) {
        LOG_INFO("[DIAG] PENDING_PROTECT outcome=full_saved type=%u timestamp=%llu bytes=%u",
                 (unsigned)data->dataTime, data->last_update,
                 (unsigned)packet.length());
        return PendingProtectResult::FULL_PACKET_SAVED;
    }

    if (savePendingRebuildMarker(data)) {
        LOG_WARNING("[DIAG] PENDING_PROTECT outcome=rebuild_marker_saved type=%u timestamp=%llu",
                    (unsigned)data->dataTime, data->last_update);
        return PendingProtectResult::REBUILD_MARKER_SAVED;
    }

    LOG_ERROR("[DIAG] PENDING_PROTECT outcome=unprotected type=%u timestamp=%llu packet_bytes=%u",
              (unsigned)data->dataTime, data->last_update,
              (unsigned)packet.length());
    return PendingProtectResult::UNPROTECTED;
}

AllProcessedDataPacket* filesysManager::readPendingPacket(
    int type,
    uint64_t timestamp,
    PendingReadStatus* status) {
    setPendingReadStatus(status, PendingReadStatus::TEMPORARY_ERROR);
    ScopedSdLock sdLock(_sdMutex);
    if (!sdLock.locked()) {
        LOG_WARNING("[DIAG] SD_LOCK_BUSY operation=read_record");
        return nullptr;
    }
    if (!file_storage::getInstance().isSDcardReady()) {
        LOG_ERROR("SD card not ready");
        return nullptr;
    }
    String path = getFilePath(type, timestamp);
    errno = 0;
    FILE* f = fopen(path.c_str(), "rb");
    
    if (!f) {
        if (errno == ENOENT) {
            setPendingReadStatus(status, PendingReadStatus::NOT_FOUND);
            LOG_DEBUG("File not found for type %d: %s", type, path.c_str());
        } else {
            LOG_WARNING("Unable to open source file for type %d: %s errno=%d",
                        type, path.c_str(), errno);
        }
        return nullptr;
    }
    LOG_DEBUG("Found file for pending packet: %s", path.c_str());

    if (fseek(f, 0, SEEK_END) != 0) {
        LOG_WARNING("Unable to seek source record file: type=%d path=%s",
                    type, path.c_str());
        fclose(f);
        return nullptr;
    }
    long fileSize = ftell(f);
    if (fileSize < 0) {
        LOG_WARNING("Unable to determine source record size: type=%d path=%s",
                    type, path.c_str());
        fclose(f);
        return nullptr;
    }
    if (fileSize == 0 ||
        fileSize % static_cast<long>(sizeof(fileStorage)) != 0) {
        setPendingReadStatus(status, PendingReadStatus::INVALID_DATA);
        LOG_WARNING("Invalid source record file: type=%d bytes=%ld path=%s",
                    type, fileSize, path.c_str());
        fclose(f);
        return nullptr;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        LOG_WARNING("Unable to rewind source record file: type=%d path=%s",
                    type, path.c_str());
        fclose(f);
        return nullptr;
    }

    AllProcessedDataPacket* pkg = new (std::nothrow) AllProcessedDataPacket();
    if (pkg == nullptr) {
        LOG_ERROR("Failed to allocate AllProcessedDataPacket");
        fclose(f);
        return nullptr;
    }
    
    pkg->dataTime = (DataTime)type;

    fileStorage rec;
    int recordCount = 0;
    
    uint64_t matchedTime = 0;
    while (fread(&rec, sizeof(fileStorage), 1, f) == 1) {
        ProcessedDataPacket processedData;
        processedData.value = rec.value;
        processedData.min_val = rec.min_val;
        processedData.max_val = rec.max_val;
        processedData.is_valid = (rec.is_valid != 0);
        // The on-card record layout is frozen for compatibility. Historical
        // records therefore retain their legacy N/D interpretation.
        processedData.status = processedData.is_valid
            ? DataStatus::NORMAL : DataStatus::SENSOR_FAULT;
        processedData.cou_val = rec.cou_val;
        
        char sensorIdBuffer[sizeof(rec.sensor_id) + 1] = {0};
        memcpy(sensorIdBuffer, rec.sensor_id, sizeof(rec.sensor_id));
        String sensorId(sensorIdBuffer);
        if (sensorId.length() == 0) {
            setPendingReadStatus(status, PendingReadStatus::INVALID_DATA);
            pkg->release();
            fclose(f);
            LOG_WARNING("Invalid empty sensor ID in source record: %s",
                        path.c_str());
            return nullptr;
        }
        pkg->processed_data_map[sensorId] = processedData;
        matchedTime = rec.timestamp;
        recordCount++;
    }
    bool readError = ferror(f) != 0;
    pkg->last_update = matchedTime;
    fclose(f);
    
    int expectedRecords = static_cast<int>(
        fileSize / static_cast<long>(sizeof(fileStorage)));
    if (!readError && recordCount == expectedRecords && recordCount > 0) {
        setPendingReadStatus(status, PendingReadStatus::OK);
        LOG_INFO("Successfully loaded pending packet: timestamp=%llu, type=%d, records=%d", 
                 matchedTime, type, recordCount);
        return pkg;
    }

    setPendingReadStatus(status, readError
        ? PendingReadStatus::TEMPORARY_ERROR
        : PendingReadStatus::INVALID_DATA);
    LOG_WARNING("Unable to load complete source records: timestamp=%llu type=%d records=%d expected=%d read_error=%d",
                timestamp, type, recordCount, expectedRecords,
                readError ? 1 : 0);
    pkg->release();
    LOG_ERROR("Failed to load pending packet for timestamp: %llu", timestamp);
    return nullptr;
}
static bool pendingPacketComesBefore(const PendingPacketInfo& left,
                                     const PendingPacketInfo& right) {
    if (left.dataTime != right.dataTime) {
        return static_cast<uint8_t>(left.dataTime) <
               static_cast<uint8_t>(right.dataTime);
    }
    if (left.timestamp != right.timestamp) {
        return left.timestamp < right.timestamp;
    }
    return left.filePath.compareTo(right.filePath) < 0;
}

static void insertPendingCandidate(
    std::vector<PendingPacketInfo>& result,
    PendingPacketInfo pending,
    size_t maxPackets) {
    auto position = result.begin();
    while (position != result.end() &&
           pendingPacketComesBefore(*position, pending)) {
        ++position;
    }
    result.insert(position, std::move(pending));
    if (result.size() > maxPackets) {
        result.pop_back();
    }
}

void filesysManager::traversePendingDirectory(
    const char* dirPath,
    std::vector<PendingPacketInfo>& afterResult,
    std::vector<PendingPacketInfo>& wrapResult,
    size_t maxPackets,
    const PendingPacketInfo* after) {

    DIR* dir = opendir(dirPath);
    if (!dir) return;
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        String name = String(entry->d_name);
        String fullPath = String(dirPath) + "/" + name;

        if (entry->d_type == DT_DIR) {
            traversePendingDirectory(fullPath.c_str(), afterResult,
                                     wrapResult, maxPackets, after);
        } 
        else if (entry->d_type == DT_REG &&
                 (name.endsWith(".pkt") || name.endsWith(".flag"))) {
            PendingPacketInfo pending;
            pending.filePath = fullPath;

            if (name.endsWith(".pkt")) {
                int dotIndex = name.lastIndexOf('.');
                pending.timestamp = strtoull(name.substring(0, dotIndex).c_str(), nullptr, 10);

                int lastSlash = fullPath.lastIndexOf('/');
                int parentSlash = fullPath.lastIndexOf('/', lastSlash - 1);
                if (parentSlash >= 0) {
                    int type = fullPath.substring(parentSlash + 1, lastSlash).toInt();
                    if (type >= (int)DataTime::REAL_DATA && type <= (int)DataTime::DAY_DATA) {
                        pending.dataTime = (DataTime)type;
                    }
                }

            } else {
                // ���ݾɰ汾���� .flag �ļ�����ֻ������ʱ���������Ĭ��Ϊ���ӡ�
                FILE* f = fopen(fullPath.c_str(), "rb");
                if (!f) {
                    LOG_WARNING("Unable to open pending file: %s", fullPath.c_str());
                    continue;
                }
                char timestampBuffer[32] = {0};
                if (fgets(timestampBuffer, sizeof(timestampBuffer), f) != nullptr) {
                    pending.timestamp = strtoull(timestampBuffer, nullptr, 10);
                    pending.dataTime = DataTime::MIN_DATA;
                }
                fclose(f);
            }

            if (pending.timestamp > 0) {
                if (after == nullptr ||
                    pendingPacketComesBefore(*after, pending)) {
                    insertPendingCandidate(afterResult, std::move(pending),
                                           maxPackets);
                } else {
                    insertPendingCandidate(wrapResult, std::move(pending),
                                           maxPackets);
                }
            } else {
                LOG_WARNING("Invalid pending file: %s", fullPath.c_str());
            }
        }
    }
    closedir(dir);
}

std::vector<PendingPacketInfo> filesysManager::scanPendingPackets(
    size_t maxPackets,
    const PendingPacketInfo* after) {
    std::vector<PendingPacketInfo> result;
    if (maxPackets == 0) return result;
    ScopedSdLock sdLock(_sdMutex);
    if (!sdLock.locked()) {
        LOG_WARNING("[DIAG] SD_LOCK_BUSY operation=scan_pending");
        return result;
    }

    if (!file_storage::getInstance().isSDcardReady()) {
        LOG_ERROR("SD card not ready for scanning");
        return result;
    }
    std::vector<PendingPacketInfo> afterResult;
    std::vector<PendingPacketInfo> wrapResult;
    afterResult.reserve(maxPackets);
    wrapResult.reserve(maxPackets);
    traversePendingDirectory("/sdcard/pending", afterResult, wrapResult,
                             maxPackets, after);
    result.reserve(maxPackets);
    for (auto& pending : afterResult) {
        if (result.size() >= maxPackets) break;
        result.push_back(std::move(pending));
    }
    for (auto& pending : wrapResult) {
        if (result.size() >= maxPackets) break;
        result.push_back(std::move(pending));
    }
    for (const auto& pending : result) {
        LOG_DEBUG("Found pending packet: type=%d, timestamp=%llu, path=%s",
                  (int)pending.dataTime, pending.timestamp, pending.filePath.c_str());
    }
    LOG_INFO("Scanned %d pending packets (batch limit %d, cursor=%s)",
             result.size(), maxPackets,
             after == nullptr ? "none" : after->filePath.c_str());
    return result;
}

String filesysManager::loadPendingPacketContent(
    const PendingPacketInfo& pending,
    PendingReadStatus* status) {
    setPendingReadStatus(status, PendingReadStatus::TEMPORARY_ERROR);
    if (!pending.filePath.endsWith(".pkt")) {
        setPendingReadStatus(status, PendingReadStatus::INVALID_DATA);
        return String();
    }
    ScopedSdLock sdLock(_sdMutex);
    if (!sdLock.locked()) {
        LOG_WARNING("[DIAG] SD_LOCK_BUSY operation=load_pending");
        return String();
    }

    errno = 0;
    FILE* f = fopen(pending.filePath.c_str(), "rb");
    if (!f) {
        if (errno == ENOENT) {
            setPendingReadStatus(status, PendingReadStatus::NOT_FOUND);
        }
        LOG_WARNING("Unable to open pending packet: %s errno=%d",
                    pending.filePath.c_str(), errno);
        return String();
    }

    if (fseek(f, 0, SEEK_END) != 0) {
        LOG_WARNING("Unable to seek pending packet: %s",
                    pending.filePath.c_str());
        fclose(f);
        return String();
    }
    long fileSize = ftell(f);
    if (fileSize < 0) {
        LOG_WARNING("Unable to determine pending packet size: %s",
                    pending.filePath.c_str());
        fclose(f);
        return String();
    }
    if (fileSize == 0 || fileSize > 4096) {
        setPendingReadStatus(status, PendingReadStatus::INVALID_DATA);
        LOG_WARNING("Invalid pending packet size %ld: %s",
                    fileSize, pending.filePath.c_str());
        fclose(f);
        return String();
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        LOG_WARNING("Unable to rewind pending packet: %s",
                    pending.filePath.c_str());
        fclose(f);
        return String();
    }

    String packet;
    if (!packet.reserve((unsigned int)fileSize + 1)) {
        LOG_ERROR("Insufficient heap to load pending packet: %s",
                  pending.filePath.c_str());
        fclose(f);
        return String();
    }

    char buffer[129];
    size_t readSize;
    size_t totalRead = 0;
    bool concatOk = true;
    while ((readSize = fread(buffer, 1, sizeof(buffer) - 1, f)) > 0) {
        buffer[readSize] = '\0';
        if (!packet.concat(buffer, readSize)) {
            concatOk = false;
            break;
        }
        totalRead += readSize;
    }
    bool readError = ferror(f) != 0;
    fclose(f);

    if (!concatOk || readError || totalRead != static_cast<size_t>(fileSize)) {
        setPendingReadStatus(status, (readError || !concatOk)
            ? PendingReadStatus::TEMPORARY_ERROR
            : PendingReadStatus::INVALID_DATA);
        LOG_WARNING("Incomplete pending packet read: path=%s bytes=%u expected=%ld concat_ok=%d read_error=%d",
                    pending.filePath.c_str(), (unsigned)totalRead, fileSize,
                    concatOk ? 1 : 0, readError ? 1 : 0);
        return String();
    }

    setPendingReadStatus(status, PendingReadStatus::OK);
    return packet;
}

bool filesysManager::quarantinePendingPacket(const PendingPacketInfo& pending) {
    ScopedSdLock sdLock(_sdMutex);
    if (!sdLock.locked()) return false;

    String invalidDir = "/sdcard/pending_bad/" +
                        String((int)pending.dataTime);
    if (file_storage::getInstance().makeDirs(invalidDir.c_str()) != 0) {
        LOG_ERROR("Failed to create pending quarantine directory: %s",
                  invalidDir.c_str());
        return false;
    }

    int lastSlash = pending.filePath.lastIndexOf('/');
    String fileName = lastSlash >= 0
        ? pending.filePath.substring(lastSlash + 1)
        : String(pending.timestamp) + ".pkt";
    String invalidPath = invalidDir + "/" + fileName;

    FILE* existing = fopen(invalidPath.c_str(), "rb");
    if (existing != nullptr) {
        fclose(existing);
        invalidPath = invalidDir + "/" + String(pending.timestamp) + "_" +
                      String(millis()) + ".pkt";
    }

    if (rename(pending.filePath.c_str(), invalidPath.c_str()) == 0) {
        LOG_WARNING("[DIAG] PENDING_QUARANTINED type=%u timestamp=%llu source=%s target=%s",
                    (unsigned)pending.dataTime, pending.timestamp,
                    pending.filePath.c_str(), invalidPath.c_str());
        return true;
    }
    LOG_ERROR("Failed to quarantine pending packet: source=%s target=%s",
              pending.filePath.c_str(), invalidPath.c_str());
    return false;
}

bool filesysManager::deletePendingPacket(const PendingPacketInfo& pending) {
    if (!file_storage::getInstance().isSDcardReady()) return false;
    ScopedSdLock sdLock(_sdMutex);
    if (!sdLock.locked()) {
        LOG_WARNING("[DIAG] SD_LOCK_BUSY operation=delete_pending");
        return false;
    }

    int ret = remove(pending.filePath.c_str());
    
    if (ret == 0) {
        LOG_INFO("Deleted delivered pending packet: %s", pending.filePath.c_str());
        cleanEmptyDirectories(pending.filePath);
        return true;
    } else {
        LOG_ERROR("Failed to delete pending packet: %s", pending.filePath.c_str());
        return false;
    }
}
void filesysManager::cleanEmptyDirectories(String filePath) {
    int lastSlash = filePath.lastIndexOf('/');
    if (lastSlash == -1) return;
    String hourDir = filePath.substring(0, lastSlash);
    if (rmdir(hourDir.c_str()) == 0) {
        LOG_DEBUG("Cleaned empty hour directory: %s", hourDir.c_str());
        int secondLastSlash = hourDir.lastIndexOf('/');
        if (secondLastSlash != -1) {
            String dateDir = hourDir.substring(0, secondLastSlash);
            if (rmdir(dateDir.c_str()) == 0) {
                LOG_DEBUG("Cleaned empty date directory: %s", dateDir.c_str());
            }
        }
    }
}

static String getJsonStringOrNumber(cJSON *root, const char *key)
{
    if (root == nullptr || key == nullptr)
    {
        return "";
    }
    cJSON *item = cJSON_GetObjectItemCaseSensitive(root, key);
    if (cJSON_IsString(item) && item->valuestring != nullptr)
    {
        return String(item->valuestring);
    }
    if (cJSON_IsNumber(item))
    {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.0f", item->valuedouble);
        return String(buf);
    }
    return "";
}

static bool parseTimestamp10(const String &text, uint64_t &out)
{
    if (text.length() == 0)
    {
        return false;
    }
    for (size_t i = 0; i < text.length(); ++i)
    {
        if (!isDigit(text.charAt(i)))
        {
            return false;
        }
    }
    out = strtoull(text.c_str(), nullptr, 10);
    return out > 0;
}

static bool timestampToTm(uint64_t timestamp, struct tm &timeInfo)
{
    String text = String(timestamp);
    while (text.length() < 14)
    {
        text += "0";
    }
    if (text.length() != 14)
    {
        return false;
    }

    memset(&timeInfo, 0, sizeof(timeInfo));
    timeInfo.tm_year = text.substring(0, 4).toInt() - 1900;
    timeInfo.tm_mon = text.substring(4, 6).toInt() - 1;
    timeInfo.tm_mday = text.substring(6, 8).toInt();
    timeInfo.tm_hour = text.substring(8, 10).toInt();
    timeInfo.tm_min = text.substring(10, 12).toInt();
    timeInfo.tm_sec = text.substring(12, 14).toInt();
    timeInfo.tm_isdst = -1;

    return timeInfo.tm_year >= (2020 - 1900) &&
           timeInfo.tm_mon >= 0 && timeInfo.tm_mon <= 11 &&
           timeInfo.tm_mday >= 1 && timeInfo.tm_mday <= 31 &&
           timeInfo.tm_hour >= 0 && timeInfo.tm_hour <= 23 &&
           timeInfo.tm_min >= 0 && timeInfo.tm_min <= 59 &&
           timeInfo.tm_sec >= 0 && timeInfo.tm_sec <= 59;
}

static uint64_t tmToTimestamp(const struct tm &timeInfo)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "%04d%02d%02d%02d%02d%02d",
             timeInfo.tm_year + 1900,
             timeInfo.tm_mon + 1,
             timeInfo.tm_mday,
             timeInfo.tm_hour,
             timeInfo.tm_min,
             timeInfo.tm_sec);
    return strtoull(buf, nullptr, 10);
}

static uint64_t nextQueryTimestamp(uint64_t current, int dataType)
{
    struct tm timeInfo;
    if (!timestampToTm(current, timeInfo))
    {
        switch (dataType)
        {
        case (int)DataTime::MIN_DATA:
            return current + 1000; // 10 minutes in YYYYMMDDHHMMSS form inside same hour only.
        case (int)DataTime::HOUR_DATA:
            return current + 10000;
        case (int)DataTime::DAY_DATA:
            return current + 1000000;
        default:
            return current + 1;
        }
    }

    time_t t = mktime(&timeInfo);
    if (t <= 0)
    {
        return current + 1;
    }

    switch (dataType)
    {
    case (int)DataTime::MIN_DATA:
        t += 10 * 60;
        break;
    case (int)DataTime::HOUR_DATA:
        t += 60 * 60;
        break;
    case (int)DataTime::DAY_DATA:
        t += 24 * 60 * 60;
        break;
    default:
        t += 1;
        break;
    }

    struct tm nextInfo;
    if (localtime_r(&t, &nextInfo) == nullptr)
    {
        return current + 1;
    }
    return tmToTimestamp(nextInfo);
}

void filesysManager::processQuery(JSONCmdData* req) {
    if (req == nullptr) return;
    LOG_INFO("Processing record query request arguments: %s", req->arguments.c_str());
    config_json jsonParser(req->arguments.c_str());
    if (!jsonParser.isValid()) {
        LOG_ERROR("Invalid record query JSON.");
        return;
    }

    cJSON *root = jsonParser.getJsonObject();
    String operation = getJsonStringOrNumber(root, "operation");
    String param     = getJsonStringOrNumber(root, "param");
    String time_str  = getJsonStringOrNumber(root, "time"); // 20250527142800 or 20250527140000-20250527150000
    if (operation != "get_records" || time_str == "" || param == "") {
        LOG_ERROR("Invalid query parameters or empty arguments.");
        return;
    }

    AllProcessedDataPacket *pendingData = nullptr;
    uint64_t start_ts = 0, end_ts = 0;
    int dashIndex = time_str.indexOf('-');
    if (dashIndex != -1) {
        String start_str = time_str.substring(0, dashIndex);
        String end_str   = time_str.substring(dashIndex + 1);
        if (!parseTimestamp10(start_str, start_ts) ||
            !parseTimestamp10(end_str, end_ts) ||
            start_ts > end_ts)
        {
            LOG_ERROR("Invalid record range time: %s", time_str.c_str());
            return;
        }
        LOG_INFO("Range query detected. Start: %llu, End: %llu", start_ts, end_ts);
    } else {
        uint64_t ts = 0;
        if (!parseTimestamp10(time_str, ts))
        {
            LOG_ERROR("Invalid record query time: %s", time_str.c_str());
            return;
        }
        start_ts = ts;
        end_ts = ts;
        LOG_INFO("Single timestamp query detected: %llu", ts);
    }

    int dataType = (int)DataTime::REAL_DATA;
    if (param == "real" || param == "raw") {
        dataType = (int)DataTime::REAL_DATA;
    } else if (param == "min") {
        dataType = (int)DataTime::MIN_DATA;
    } else if (param == "hour") {
        dataType = (int)DataTime::HOUR_DATA;
    } else if (param == "day") {
        dataType = (int)DataTime::DAY_DATA;
    } else {
        LOG_ERROR("Invalid record param: %s", param.c_str());
        return;
    }

    if (!file_storage::getInstance().isSDcardReady())
    {
        LOG_ERROR("SD card not ready for record query");
    }

    for (uint64_t current_ts = start_ts; current_ts <= end_ts;) {
        pendingData = readPendingPacket(dataType, current_ts);
        if (pendingData == nullptr) {
            pendingData = new AllProcessedDataPacket(); 
            pendingData->last_update = current_ts;
            pendingData->dataTime = (DataTime)dataType;
        }
        int subCount = EventBus::getInstance().getSubscriberCount(EventID::RECORD_QUERY_RES);
        for (int i = 0; i < subCount; i++) {
            pendingData->retain();
        }
        EventBus::getInstance().publish(EventID::RECORD_QUERY_RES, pendingData);
        pendingData->release();
        if (current_ts == end_ts)
        {
            break;
        }
        uint64_t next_ts = nextQueryTimestamp(current_ts, dataType);
        if (next_ts <= current_ts)
        {
            LOG_ERROR("Record query timestamp did not advance: %llu -> %llu", current_ts, next_ts);
            break;
        }
        current_ts = next_ts;
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

void filesysManager::poll() {
    EventMsg msg;
    if (EventBus::waitEvent(SaveDataFileTaskQueue, msg))
    {
        RemoteOtaManager::BusinessActivityGuard businessActivity;
        if (msg.id == EventID::PROCESSED_DATA_COLLECTED)
        {
            AllProcessedDataPacket *allData = (AllProcessedDataPacket *)msg.data;
            if (allData != nullptr)
            {
                storeProcessedPacket(allData);
                allData->release();
            }
        }
        else if (msg.id == EventID::RECORD_QUERY_REQ)
        {
            JSONCmdData* req = (JSONCmdData*)msg.data;
            processQuery(req);
            req->release();
        }
    }
}
