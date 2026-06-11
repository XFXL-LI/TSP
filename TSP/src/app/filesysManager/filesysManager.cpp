#include "filesysManager.h"
#include "../../module/log/log_manager.h"
#include "../../system/event/eventBus.h"
#include "../../module/json/config_json.h"
#include <sys/dirent.h>
#include <sys/types.h>


filesysManager& filesysManager::getInstance() {
    static filesysManager instance;
    return instance;
}

filesysManager::filesysManager() {
    file_storage::getInstance().makeDirs("/sdcard/history");
    if (SaveDataFileTaskQueue == nullptr) {
        SaveDataFileTaskQueue = EventBus::getInstance().createReceiverQueue(10);
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
// 202605151158
String filesysManager::getFilePath(int type, uint64_t ts) {
    char dateStr[10], hourStr[5], minStr[5];
    parseTimestamp(ts, dateStr, hourStr, minStr);

    String basePath = "/sdcard/" + String(dateStr);
    String finalPath;

    switch(type) {
        case 0: // 实时: /sdcard/20260427/raw/14/01.dat
            finalPath = basePath + "/raw/" + hourStr + "/" + minStr + ".dat";
            break;
        case 1: // 分钟: /sdcard/20260427/min/14/01.dat
            finalPath = basePath + "/min/" + hourStr + "/" + minStr + ".dat";
            break;
        case 2: // 小时: /sdcard/20260427/hour/14.dat
            finalPath = basePath + "/hour/" + hourStr + ".dat";
            break;
        case 3: // 天:   /sdcard/20260427/day/day.dat
            finalPath = basePath + "/day/day.dat";
            break;
        default:
            finalPath = "/sdcard/history/other.dat";
            break;
    }
    return finalPath;
}

void filesysManager::storeProcessedPacket(AllProcessedDataPacket* pkg) {
    if (!pkg || !file_storage::getInstance().isSDcardReady()) return;
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
        rec.is_valid = data.is_valid ? 1 : 0;
        historyData.push_back(rec);
    }
    writeToFile(path, historyData);
}

void filesysManager::writeToFile(const String& path, const std::vector<fileStorage>& rec) {
    if (rec.empty()) return;

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
}

String filesysManager::getPendingFilePath(uint64_t ts) {
    char dateStr[10], hourStr[5], minStr[5];
    parseTimestamp(ts, dateStr, hourStr, minStr);

    String path = "/sdcard/pending/" + String(dateStr) + "/" + hourStr + "/" + minStr + ".flag";
    return path;
}

// 保存待补传的时间戳 (创建标记文件)
void filesysManager::savePendingPacket(uint64_t timestamp) {
    if (!file_storage::getInstance().isSDcardReady()) return;
    
    String path = getPendingFilePath(timestamp);
    int lastSlash = path.lastIndexOf('/');
    if (lastSlash != -1) {
        String dirPath = path.substring(0, lastSlash);
        file_storage::getInstance().makeDirs(dirPath.c_str());
        
        // 创建标记文件 (内容为时间戳)
        FILE* f = fopen(path.c_str(), "w");
        if (f) {
            fprintf(f, "%llu", timestamp);
            fclose(f);
            LOG_DEBUG("Saved pending packet marker: %s", path.c_str());
        } else {
            LOG_ERROR("Failed to create pending marker file: %s", path.c_str());
        }
    }
}
AllProcessedDataPacket* filesysManager::readPendingPacket(int type, uint64_t timestamp) {
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
    AllProcessedDataPacket* pkg = new AllProcessedDataPacket();
    if (pkg == nullptr) {
        LOG_ERROR("Failed to allocate AllProcessedDataPacket");
        fclose(f);
        return nullptr;
    }
    
    pkg->dataTime = (DataTime)type;

    fileStorage rec;
    int recordCount = 0;
    
    uint64_t lasttime = 0;
    while (fread(&rec, sizeof(fileStorage), 1, f) == 1) {
        uint64_t fileTs10 = rec.timestamp / 100;
        uint64_t targetTs10 = timestamp / 100;
        lasttime = rec.timestamp;
        if (fileTs10 == targetTs10) {
            ProcessedDataPacket processedData;
            processedData.value = rec.value;
            processedData.min_val = rec.min_val;
            processedData.max_val = rec.max_val;
            processedData.is_valid = (rec.is_valid != 0);
            processedData.cou_val = 0;
            
            String sensorId(rec.sensor_id);
            pkg->processed_data_map[sensorId] = processedData;
            recordCount++;
        }
    }
    pkg->last_update = lasttime;
    fclose(f);
    
    if (recordCount > 0) {
        LOG_INFO("Successfully loaded pending packet: timestamp=%llu, type=%d, records=%d", 
                lasttime, type, recordCount);
        return pkg;
    } else {
        LOG_WARNING("No matching records found for timestamp %llu in type %d", timestamp, type);
        pkg->release();
    }
    
    LOG_ERROR("Failed to load pending packet for timestamp: %llu", timestamp);
    return nullptr;
}
void filesysManager::traverseDirectory(const char* dirPath, std::vector<uint64_t>& result) {
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
            traverseDirectory(fullPath.c_str(), result);
        } 
        else if (entry->d_type == DT_REG && name.endsWith(".flag")) {
            int dotIndex = name.lastIndexOf('.');
            String ssStr = name.substring(0, dotIndex);
            uint64_t min = strtoull(ssStr.c_str(), NULL, 10);
            int lastSlash = fullPath.lastIndexOf('/');
            int secondLastSlash = fullPath.lastIndexOf('/', lastSlash - 1);
            int thirdLastSlash = fullPath.lastIndexOf('/', secondLastSlash - 1);
            if (thirdLastSlash != -1) {
                String dateStr = fullPath.substring(thirdLastSlash + 1, secondLastSlash);
                String hourStr = fullPath.substring(secondLastSlash + 1, lastSlash);

                uint64_t datePart = strtoull(dateStr.c_str(), NULL, 10);
                uint64_t hourPart = strtoull(hourStr.c_str(), NULL, 10);
                uint64_t finalTs = (datePart * 10000) + (hourPart * 100) + min;
                result.push_back(finalTs);
            }
        }
    }
    closedir(dir);
}

std::vector<uint64_t> filesysManager::scanPendingTimestamps() {
    std::vector<uint64_t> result;
    if (!file_storage::getInstance().isSDcardReady()) {
        LOG_ERROR("SD card not ready for scanning");
        return result;
    }
    traverseDirectory("/sdcard/pending", result);
    for(uint64_t ts : result) {
        LOG_DEBUG("Found pending timestamp: %llu", ts);
    }
    LOG_INFO("Total scanned %d pending data files", result.size());
    return result;
}

// 删除已补传的数据标记文件
bool filesysManager::deletePendingPacket(uint64_t timestamp) {
    if (!file_storage::getInstance().isSDcardReady()) return false;
    
    String path = getPendingFilePath(timestamp);
    int ret = remove(path.c_str());
    
    if (ret == 0) {
        LOG_DEBUG("Deleted pending marker: %s", path.c_str());
        cleanEmptyDirectories(path);
        return true;
    } else {
        LOG_ERROR("Failed to delete pending marker: %s", path.c_str());
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

void filesysManager::processQuery(JSONCmdData* req) {
    if (req == nullptr) return;
    LOG_INFO("Processing record query request arguments: %s", req->arguments.c_str());
    config_json jsonParser(req->arguments.c_str());
    String operation = jsonParser.getString("operation", "");
    String param     = jsonParser.getString("param", "");
    String time_str  = jsonParser.getString("time", ""); // 202505271428 or 202505271400-202505271500
    if (operation != "get_records" || time_str == "" || param == "") {
        LOG_ERROR("Invalid query parameters or empty arguments.");
        return;
    }
    uint64_t ts = strtoull(time_str.c_str(), nullptr, 10);
    AllProcessedDataPacket *pendingData = nullptr;
    uint64_t start_ts = 0, end_ts = 0;
    int dashIndex = time_str.indexOf('-');
    if (dashIndex != -1) {
        String start_str = time_str.substring(0, dashIndex);
        String end_str   = time_str.substring(dashIndex + 1);
        start_ts = strtoull(start_str.c_str(), nullptr, 12);
        end_ts   = strtoull(end_str.c_str(), nullptr, 12);
        LOG_INFO("Range query detected. Start: %llu, End: %llu", start_ts, end_ts);
    } else {
        start_ts = ts;
        end_ts = ts;
    }

    int step = 1;
    int dataType = RAW_DATA;
    if (param == "min") {
        step = 1;
        dataType = MIN_DATA;
    } else if (param == "hour") {
        step = 100;
        dataType = HOUR_DATA;
    } else if (param == "day") {
        step = 10000;
        dataType = DAY_DATA;
    }

    for (uint64_t current_ts = start_ts; current_ts <= end_ts; current_ts += step) {
        pendingData = readPendingPacket(dataType, current_ts);
        if (pendingData == nullptr) {
            pendingData = new AllProcessedDataPacket(); 
        }
        int subCount = EventBus::getInstance().getSubscriberCount(EventID::RECORD_QUERY_RES);
        for (int i = 0; i < subCount; i++) {
            pendingData->retain();
        }
        EventBus::getInstance().publish(EventID::RECORD_QUERY_RES, pendingData);
        pendingData->release();
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