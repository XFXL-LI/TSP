#ifndef FILESYS_MANAGER_H
#define FILESYS_MANAGER_H

#include <Arduino.h>
#include <vector>
#include "../../module/file/file_storage.h"
#include "../../inc/sys_init.h" 
#include "PendingWritePolicy.h"

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

    // Maintenance-only; LCD queries never initialize or remount the card.
    void retrySDMountIfDue();

    void storeProcessedPacket(AllProcessedDataPacket* pkg);
    
    // ���������ݹ���ӿ�
    AllProcessedDataPacket* readPendingPacket(
        int type,
        uint64_t timestamp,
        PendingReadStatus* status = nullptr);
    bool savePendingPacket(DataTime dataTime, uint64_t timestamp, const String& packet);
    bool savePendingRebuildMarker(DataTime dataTime, uint64_t timestamp);
    PendingProtectResult protectPendingPacket(
        DataTime dataTime, uint64_t timestamp, const String& packet);
    // Admission never takes the SD lock or waits for file I/O. Ownership moves
    // only after bounded metadata/byte reservations have succeeded.
    bool queuePendingPacket(const AllProcessedDataPacket* data, String& packet);
    bool hasPendingStoreJobs() const {
        return _pendingWriteCount.load(std::memory_order_acquire) != 0;
    }
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
    struct PendingWriteRequest {
        DataTime dataTime = DataTime::REAL_DATA;
        uint64_t timestamp = 0;
        uint32_t trace = 0;
        uint32_t nextTryAt = 0;
        uint8_t attempts = 0;
        String packet;
    };
    QueueHandle_t _pendingWriteQueue = nullptr;
    std::atomic<uint32_t> _pendingWriteCount{0};
    std::atomic<size_t> _pendingWriteBytes{0};
    void servicePendingWrites();
    void releasePendingWrite(PendingWriteRequest* request);
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
