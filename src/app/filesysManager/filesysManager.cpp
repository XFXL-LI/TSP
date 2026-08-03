#include "filesysManager.h"
#include "../../module/log/log_manager.h"
#include "../../module/pack212/pack212.h"
#include "../../system/event/eventBus.h"
#include "../../module/json/config_json.h"
#include <sys/dirent.h>
#include <sys/types.h>
#include <time.h>
#include <utility>
#include <new>

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
    file_storage::getInstance().makeDirs("/sdcard/history");
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

        FILE* f = fopen(path.c_str(), "wb");
        if (f) {
            size_t written = fwrite(packet.c_str(), 1, packet.length(), f);
            fclose(f);
            if (written == packet.length()) {
                // Read back the complete marker before reporting success.
                // This catches media/FAT corruption immediately.
                FILE* verify = fopen(path.c_str(), "rb");
                bool matches = verify != nullptr;
                size_t offset = 0;
                uint8_t buffer[128];
                while (matches && offset < packet.length()) {
                    size_t expected = min(sizeof(buffer), packet.length() - offset);
                    size_t got = fread(buffer, 1, expected, verify);
                    if (got != expected ||
                        memcmp(buffer, packet.c_str() + offset, expected) != 0) {
                        matches = false;
                        break;
                    }
                    offset += got;
                }
                if (verify != nullptr) fclose(verify);
                if (matches && offset == packet.length()) {
                    LOG_INFO("Saved and verified pending HJ212 packet: %s", path.c_str());
                    return true;
                }
                remove(path.c_str());
                LOG_ERROR("[DIAG] PENDING_WRITE_VERIFY_FAIL path=%s bytes=%u",
                          path.c_str(), (unsigned)packet.length());
                return false;
            }
            LOG_ERROR("Pending packet write size mismatch: %s", path.c_str());
        } else {
            LOG_ERROR("Failed to create pending packet: %s", path.c_str());
        }
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
    FILE* existing = fopen(path.c_str(), "rb");
    if (existing != nullptr) {
        fclose(existing);
        LOG_INFO("[DIAG] PENDING_MARKER_EXISTS type=%u timestamp=%llu path=%s",
                 (unsigned)data->dataTime, data->last_update, path.c_str());
        return true;
    }

    static const char marker[] = "REBUILD_FROM_SD";
    FILE* f = fopen(path.c_str(), "wb");
    if (f == nullptr) {
        LOG_ERROR("Failed to create pending rebuild marker: %s", path.c_str());
        return false;
    }
    size_t written = fwrite(marker, 1, sizeof(marker) - 1, f);
    fclose(f);
    if (written != sizeof(marker) - 1) {
        remove(path.c_str());
        LOG_ERROR("Pending rebuild marker write size mismatch: %s", path.c_str());
        return false;
    }
    LOG_INFO("[DIAG] PENDING_REBUILD_MARKER type=%u timestamp=%llu path=%s",
             (unsigned)data->dataTime, data->last_update, path.c_str());
    return true;
}

AllProcessedDataPacket* filesysManager::readPendingPacket(int type, uint64_t timestamp) {
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
    FILE* f = fopen(path.c_str(), "rb");
    
    if (!f) {
        LOG_DEBUG("File not found for type %d: %s", type, path.c_str());
        return nullptr;
    }
    LOG_DEBUG("Found file for pending packet: %s", path.c_str());
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
        processedData.cou_val = rec.cou_val;
        
        String sensorId(rec.sensor_id);
        pkg->processed_data_map[sensorId] = processedData;
        matchedTime = rec.timestamp;
        recordCount++;
    }
    pkg->last_update = matchedTime;
    fclose(f);
    
    if (recordCount > 0) {
        LOG_INFO("Successfully loaded pending packet: timestamp=%llu, type=%d, records=%d", 
                matchedTime, type, recordCount);
        return pkg;
    } else {
        LOG_WARNING("No matching records found for timestamp %llu in type %d", timestamp, type);
        pkg->release();
    }
    
    LOG_ERROR("Failed to load pending packet for timestamp: %llu", timestamp);
    return nullptr;
}
void filesysManager::traversePendingDirectory(
    const char* dirPath,
    std::vector<PendingPacketInfo>& result,
    size_t maxPackets) {
    if (result.size() >= maxPackets) return;

    DIR* dir = opendir(dirPath);
    if (!dir) return;
    struct dirent* entry;
    while (result.size() < maxPackets && (entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        String name = String(entry->d_name);
        String fullPath = String(dirPath) + "/" + name;

        if (entry->d_type == DT_DIR) {
            traversePendingDirectory(fullPath.c_str(), result, maxPackets);
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
                result.push_back(std::move(pending));
            } else {
                LOG_WARNING("Invalid pending file: %s", fullPath.c_str());
            }
        }
    }
    closedir(dir);
}

std::vector<PendingPacketInfo> filesysManager::scanPendingPackets(size_t maxPackets) {
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
    result.reserve(maxPackets);
    traversePendingDirectory("/sdcard/pending", result, maxPackets);
    for (const auto& pending : result) {
        LOG_DEBUG("Found pending packet: type=%d, timestamp=%llu, path=%s",
                  (int)pending.dataTime, pending.timestamp, pending.filePath.c_str());
    }
    LOG_INFO("Scanned %d pending packets (batch limit %d)",
             result.size(), maxPackets);
    return result;
}

String filesysManager::loadPendingPacketContent(const PendingPacketInfo& pending) {
    if (!pending.filePath.endsWith(".pkt")) return String();
    ScopedSdLock sdLock(_sdMutex);
    if (!sdLock.locked()) {
        LOG_WARNING("[DIAG] SD_LOCK_BUSY operation=load_pending");
        return String();
    }

    FILE* f = fopen(pending.filePath.c_str(), "rb");
    if (!f) {
        LOG_WARNING("Unable to open pending packet: %s", pending.filePath.c_str());
        return String();
    }

    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return String();
    }
    long fileSize = ftell(f);
    if (fileSize <= 0 || fileSize > 4096 || fseek(f, 0, SEEK_SET) != 0) {
        LOG_WARNING("Invalid pending packet size %ld: %s",
                    fileSize, pending.filePath.c_str());
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
    while ((readSize = fread(buffer, 1, sizeof(buffer) - 1, f)) > 0) {
        buffer[readSize] = '\0';
        packet.concat(buffer, readSize);
    }
    fclose(f);
    return packet;
}

bool filesysManager::quarantinePendingPacket(const PendingPacketInfo& pending) {
    ScopedSdLock sdLock(_sdMutex);
    if (!sdLock.locked()) return false;
    String invalidPath = pending.filePath + ".invalid";
    if (rename(pending.filePath.c_str(), invalidPath.c_str()) == 0) {
        LOG_WARNING("Quarantined unrecoverable pending packet: %s",
                    invalidPath.c_str());
        return true;
    }
    LOG_ERROR("Failed to quarantine pending packet: %s",
              pending.filePath.c_str());
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
