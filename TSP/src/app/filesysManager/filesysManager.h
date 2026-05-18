#ifndef FILESYS_MANAGER_H
#define FILESYS_MANAGER_H

#include <Arduino.h>
#include <vector>
#include "../../module/file/file_storage.h"
#include "../../inc/sys_init.h" 

#pragma pack(push, 1)
struct fileStorage {
    uint64_t timestamp;   // 4字节
    char sensor_id[15];   // 15字节
    float value;          // 4字节
    float min_val;        // 4字节
    float max_val;        // 4字节
    uint8_t is_valid;     // 1字节
}; // 合计 32 字节
#pragma pack(pop)

class filesysManager {
public:
    static filesysManager& getInstance();

    void storeProcessedPacket(AllProcessedDataPacket* pkg);
    
    // 待补传数据管理接口
    AllProcessedDataPacket* readPendingPacket(uint64_t timestamp);
    void savePendingPacket(uint64_t timestamp);                     // 保存待补传的时间戳到文件
    std::vector<uint64_t> scanPendingTimestamps();                 // 启动时扫描未补传的时间戳
    bool deletePendingPacket(uint64_t timestamp);                   // 补传成功后删除待补传数据
    void cleanEmptyDirectories(String filePath);                 // 清理空目录
    void poll(); // 轮询处理待补传数据的发送
    
private:
    String min_path;
    String hour_path;
    String day_path;
    String pending_path;                                            // 待补传数据目录
    std::vector<fileStorage> historyData;

    QueueHandle_t SaveDataFileTaskQueue;
    filesysManager();
    void traverseDirectory(const char* dirPath, std::vector<uint64_t>& result);
    String getFilePath(int type, uint64_t ts);
    String getPendingFilePath(uint64_t ts);                         // 生成待补传文件路径
    void parseTimestamp(uint64_t ts, char* date, char* hour, char* min);
    void writeToFile(const String& path, const std::vector<fileStorage>& rec);
};

#endif