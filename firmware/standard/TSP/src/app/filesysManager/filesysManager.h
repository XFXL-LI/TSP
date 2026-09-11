#ifndef FILESYS_MANAGER_H
#define FILESYS_MANAGER_H

#include <Arduino.h>
#include <vector>
#include "../../module/file/file_storage.h"
#include "../../inc/sys_init.h" 

#pragma pack(push, 1)
struct fileStorage {
    uint64_t timestamp;   // 4�ֽ�
    char sensor_id[15];   // 15�ֽ�
    float value;          // 4�ֽ�
    float min_val;        // 4�ֽ�
    float max_val;        // 4�ֽ�
    float cou_val;        // 4�ֽ�
    uint8_t is_valid;     // 1�ֽ�
}; // �ϼ� 32 �ֽ�
#pragma pack(pop)

struct PendingPacketInfo {
    uint64_t timestamp = 0;
    DataTime dataTime = DataTime::MIN_DATA;
    String filePath;
};

enum class PendingReadStatus : uint8_t {
    OK,
    NOT_FOUND,
    TEMPORARY_ERROR,
    INVALID_DATA
};

enum class PendingProtectResult : uint8_t {
    FULL_PACKET_SAVED,
    REBUILD_MARKER_SAVED,
    UNPROTECTED
};

class filesysManager {
public:
    static filesysManager& getInstance();

    void storeProcessedPacket(AllProcessedDataPacket* pkg);
    
    // ���������ݹ���ӿ�
    AllProcessedDataPacket* readPendingPacket(
        int type,
        uint64_t timestamp,
        PendingReadStatus* status = nullptr);
    bool savePendingPacket(const AllProcessedDataPacket* data, const String& packet);
    bool savePendingRebuildMarker(const AllProcessedDataPacket* data);
    PendingProtectResult protectPendingPacket(
        const AllProcessedDataPacket* data,
        const String& packet);
    std::vector<PendingPacketInfo> scanPendingPackets(
        size_t maxPackets = 3,
        const PendingPacketInfo* after = nullptr);
    String loadPendingPacketContent(
        const PendingPacketInfo& pending,
        PendingReadStatus* status = nullptr);
    bool deletePendingPacket(const PendingPacketInfo& pending);
    bool quarantinePendingPacket(const PendingPacketInfo& pending);
    void cleanEmptyDirectories(String filePath);                 // �����Ŀ¼
    void poll(); // ��ѯ������������ݵķ���
    
private:
    String min_path;
    String hour_path;
    String day_path;
    String pending_path;                                            // ����������Ŀ¼
    std::vector<fileStorage> historyData;

    QueueHandle_t SaveDataFileTaskQueue;
    SemaphoreHandle_t _sdMutex;
    filesysManager();
    void traversePendingDirectory(
        const char* dirPath,
        std::vector<PendingPacketInfo>& afterResult,
        std::vector<PendingPacketInfo>& wrapResult,
        size_t maxPackets,
        const PendingPacketInfo* after);
    String getFilePath(int type, uint64_t ts);
    String getPendingFilePath(DataTime type, uint64_t ts);
    void parseTimestamp(uint64_t ts, char* date, char* hour, char* min);
    bool writeToFile(const String& path, const std::vector<fileStorage>& rec);

    void processQuery(JSONCmdData* req);
};

#endif
