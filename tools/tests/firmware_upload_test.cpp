// Actual production implementations are extracted by Run-FirmwareUploadTests.
// Host doubles cannot validate GPIO waveforms or real SD driver latency.
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <functional>
#include <map>
#include <new>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

class String {
public:
    std::string text;
    String(const char* s = "") : text(s) {}
    String(std::string s) : text(std::move(s)) {}
    String(char c) : text(1, c) {}
    template<class T, typename std::enable_if<std::is_integral<T>::value, int>::type = 0>
    String(T v) : text(std::to_string(v)) {}
    size_t length() const { return text.size(); }
    const char* c_str() const { return text.c_str(); }
    bool reserve(size_t n) { text.reserve(n); return true; }
    char charAt(size_t n) const { return n < text.size() ? text[n] : 0; }
    void setCharAt(size_t n, char c) { assert(n < text.size()); text[n] = c; }
    int indexOf(const String& s, size_t from = 0) const { auto n = text.find(s.text, from); return n == text.npos ? -1 : int(n); }
    int indexOf(char c, size_t from = 0) const { auto n = text.find(c, from); return n == text.npos ? -1 : int(n); }
    String substring(size_t a, size_t b = std::string::npos) const { return text.substr(a, b == std::string::npos ? b : b - a); }
    long toInt() const { return std::strtol(c_str(), nullptr, 10); }
    bool startsWith(const String& s) const { return text.compare(0, s.length(), s.text) == 0; }
    bool endsWith(const String& s) const { return length() >= s.length() && text.compare(length() - s.length(), s.length(), s.text) == 0; }
    void trim() { auto a = text.find_first_not_of(" \t\r\n"); auto b = text.find_last_not_of(" \t\r\n"); text = a == text.npos ? "" : text.substr(a, b - a + 1); }
    void toLowerCase() { for (char& c : text) c = char(std::tolower(static_cast<unsigned char>(c))); }
    String& operator+=(const String& s) { text += s.text; return *this; }
    friend String operator+(String a, const String& b) { a += b; return a; }
    friend bool operator==(const String& a, const String& b) { return a.text == b.text; }
    friend bool operator!=(const String& a, const String& b) { return !(a == b); }
    friend bool operator<(const String& a, const String& b) { return a.text < b.text; }
};
static std::vector<std::string> logs;
static void simulateLogContention();
template<class... Args> void logLine(const char* fmt, Args... args) {
    simulateLogContention();
    if constexpr (sizeof...(Args) == 0) { logs.emplace_back(fmt); }
    else { char out[1024]; std::snprintf(out, sizeof(out), fmt, args...); logs.emplace_back(out); }
}
#define LOG_DEBUG(...) logLine(__VA_ARGS__)
#define LOG_INFO(...) logLine(__VA_ARGS__)
#define LOG_ERROR(...) logLine(__VA_ARGS__)
#define LOG_WARNING(...) logLine(__VA_ARGS__)
static uint32_t nowMs;
uint32_t millis() { return nowMs; }
using UBaseType_t = unsigned;
using TickType_t = uint32_t;
using BaseType_t = int;
using SemaphoreHandle_t = void*;
constexpr int pdTRUE = 1, pdFALSE = 0, eInvalid = 0;
#define pdMS_TO_TICKS(x) (x)
static unsigned basePriority = 8, effectivePriority = 8, inheritedPriority = 0;
static unsigned heldSerialMutexes = 0, logWaiterPriority = 0;
static bool rejectSerialLock = false;
static std::function<void(unsigned)> priorityHook;
static void simulateLogContention() {
    // A higher-priority SD logger waits while the UART owner holds log+serial
    // mutexes. Releasing only log leaves the inherited level until serial unlock.
    if (heldSerialMutexes && effectivePriority < logWaiterPriority)
        effectivePriority = logWaiterPriority;
}
struct TaskStatus_t { unsigned uxBasePriority; };
void vTaskGetInfo(void*, TaskStatus_t* s, int, int) { s->uxBasePriority = basePriority; }
// ESP-IDF 5.5.2 tasks.c: changing a base while inherited leaves effective unchanged.
void vTaskPrioritySet(void*, unsigned p) {
    if (basePriority == p) return;
    if (basePriority == effectivePriority) effectivePriority = p;
    basePriority = p;
    if (priorityHook) priorityHook(p);
}
unsigned uxTaskPriorityGet(void*) { return effectivePriority; }
static unsigned heldSdMutexes = 0;
int xSemaphoreTake(void* mutex, unsigned) {
    if (mutex == reinterpret_cast<void*>(2)) {
        if (rejectSerialLock) return pdFALSE;
        ++heldSerialMutexes;
    } else ++heldSdMutexes;
    return pdTRUE;
}
void xSemaphoreGive(void* mutex) {
    if (mutex == reinterpret_cast<void*>(2)) { assert(heldSerialMutexes); --heldSerialMutexes; }
    else { assert(heldSdMutexes); --heldSdMutexes; }
    if (!heldSerialMutexes && !heldSdMutexes) effectivePriority = basePriority;
}
void yield() { assert(basePriority < 11 && effectivePriority < 11); }
void vTaskDelay(unsigned ms) { assert(basePriority < 11 && effectivePriority < 11); nowMs += ms; }
using portMUX_TYPE = int;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(x) ((void)0)
#define portEXIT_CRITICAL(x) ((void)0)
#include "module/diagnostics/SdIoDiagnostics.h"
#include "diagnostic_source.h"
struct { unsigned getFreeHeap() { return 100000; } unsigned getMaxAllocHeap() { return 90000; } } ESP;
namespace CalendarClock { bool isValidTimestamp(uint64_t t) { return t >= 20200101000000ULL && t < 21000101000000ULL; } }
enum class DataTime { REAL_DATA, MIN_DATA, HOUR_DATA, DAY_DATA };
struct ProcessedDataPacket { float value = 10, min_val = 5, max_val = 15, cou_val = 0; bool is_valid = true; char status = 'N'; };
char dataStatusCode(char c) { return c; }
struct AllProcessedDataPacket {
    DataTime dataTime = DataTime::REAL_DATA;
    uint64_t last_update = 20261020120000ULL;
    uint32_t trace_id = 42;
    std::map<String, ProcessedDataPacket> processed_data_map;
    void retain() {}
    void release() {}
};
struct HJ212CONFIG {
    String st = "22", pw = "123456", mn = "JCKJ123", flag = "4", ack_mode = "compatible";
    int timeout = 1, retry_times = 3;
};
struct CollectSetting { String unit = "ug/m3"; };
using COLLECTMAP = std::map<String, CollectSetting>;
struct ConfigManager {
    COLLECTMAP map;
    HJ212CONFIG hj;
    HJ212CONFIG getHJ212() const { return hj; }
    static ConfigManager& getInstance() { static ConfigManager c; return c; }
    const COLLECTMAP& getCollectConfigs() const { return map; }
};
#include "module/gas/GasUnitConverter.h"
#include "pack_source.h"
#include "guard_source.h"

struct Stream {
    std::string wire, incoming;
    unsigned expectedPriority = 11, writes = 0, flushes = 0;
    bool shortWrite = false;
    std::function<void()> writeHook;
    int available() { return int(incoming.size()); }
    char read() { char c = incoming.front(); incoming.erase(0, 1); return c; }
    size_t write(const uint8_t* data, size_t len) {
        assert(effectivePriority == expectedPriority); ++writes;
        if (writeHook) writeHook();
        size_t n = shortWrite && len ? len - 1 : len;
        wire.append(reinterpret_cast<const char*>(data), n); nowMs += uint32_t(len);
        return n;
    }
    void flush() { assert(basePriority < 11 && effectivePriority < 11); ++flushes; }
};
#include "driver_source.h"
#include "app/filesysManager/PendingWritePolicy.h"
struct HostQueue { std::deque<void*> jobs; bool reject = false; unsigned capacity = 2; };
using QueueHandle_t = HostQueue*;
int xQueueSend(QueueHandle_t q, void* const* job, unsigned timeout) {
    assert(timeout == 0);
    if (q->reject || q->jobs.size() >= q->capacity) return 0;
    q->jobs.push_back(*job); return pdTRUE;
}
int xQueueReceive(QueueHandle_t q, void** job, unsigned timeout) {
    assert(timeout == 0); if (q->jobs.empty()) return 0;
    *job = q->jobs.front(); q->jobs.pop_front(); return pdTRUE;
}
struct JSONCmdData { String arguments; };
namespace RemoteOtaManager { struct BusinessActivityGuard {}; }
// Expose internals only to inspect bounded ownership and inject filesystem faults.
#define private public
#include "pending_decl.h"
#undef private
int xQueueSend(QueueHandle_t q, filesysManager::PendingWriteRequest* const* p, unsigned t) { void* v = *p; return xQueueSend(q, &v, t); }
int xQueueReceive(QueueHandle_t q, filesysManager::PendingWriteRequest** p, unsigned t) { void* v = nullptr; int r = xQueueReceive(q, &v, t); *p = static_cast<filesysManager::PendingWriteRequest*>(v); return r; }
static HostQueue pendingQueue;
filesysManager::filesysManager() { _pendingWriteQueue = &pendingQueue; _sdMutex = reinterpret_cast<void*>(1); }
static bool sdReady = true, fullWrite = true, markerWrite = true;
static unsigned diskCalls, diskLatency;
static std::function<void()> diskHook;
static unsigned measurementWrites = 0, mounts = 0;
namespace DeviceRuntimeStatus {
void measurementWriteFinished(bool) { ++measurementWrites; }
void sdMountStarted() {}
void sdMountFinished(int) {}
}
constexpr int ESP_OK = 0, SD_CLK_IO = 47, SD_CMD_IO = 48, SD_DAT0_IO = 21;
using esp_err_t = int;
#define SD_MOUNT_POINT "/host-only-test"
namespace SdMountPolicy { constexpr unsigned IO_BUDGET_MS = 3000; }
struct file_storage {
    std::atomic<bool> sd_mount_attempted{false}, sd_card_initialized{false};
    std::atomic<uint32_t> sd_mount_finished_ms{0};
    struct Session { int mount(const char*, int, int, int) { ++mounts; return ESP_OK; } } sd_mount_session;
    static file_storage& getInstance() { static file_storage value; return value; }
    bool isSDcardReady() const { return sdReady; }
    bool SDcardInit();
    int writeSDCard(const char*, const String&, bool append = false);
    int readSDCard(const char*, String&);
    int makeDirs(const char*) { ++diskCalls; return 0; }
};
String filesysManager::getFilePath(int, uint64_t) { return "/host-only-test/history"; }
bool filesysManager::writeToFile(const String&, const std::vector<fileStorage>&) { ++diskCalls; return fullWrite; }
#include "storage_source.h"
#include "sd_file_source.h"
struct cJSON {};
static bool validHistoryQuery = false;
struct config_json {
    explicit config_json(const char*) {}
    bool isValid() const { return validHistoryQuery; }
    cJSON* getJsonObject() { return nullptr; }
};
String getJsonStringOrNumber(cJSON*, const char* key) {
    if (std::strcmp(key, "operation") == 0) return "get_records";
    if (std::strcmp(key, "param") == 0) return "hour";
    return "20261020120000";
}
bool parseTimestamp10(const String&, uint64_t& value) { value = 20261020120000ULL; return true; }
uint64_t nextQueryTimestamp(uint64_t value, int) { return value + 1; }
enum class EventID { RECORD_QUERY_RES };
struct EventBus {
    static inline unsigned replies = 0;
    static inline void* lastReply = reinterpret_cast<void*>(1);
    static EventBus& getInstance() { static EventBus bus; return bus; }
    unsigned getSubscriberCount(EventID) { return 0; }
    void publish(EventID, void* data) { ++replies; lastReply = data; }
};
AllProcessedDataPacket* filesysManager::readPendingPacket(int, uint64_t, PendingReadStatus*) { ++diskCalls; return nullptr; }
#include "query_source.h"
bool filesysManager::savePendingPacket(DataTime, uint64_t, const String&) {
    ++diskCalls; nowMs += diskLatency; if (diskHook) diskHook();
    return sdReady && fullWrite;
}
bool filesysManager::savePendingRebuildMarker(DataTime, uint64_t) { ++diskCalls; nowMs += diskLatency; return sdReady && markerWrite; }
#include "pending_source.h"


#include "manager_helpers.h"
constexpr int SERIAL_HJ212 = 2;
static unsigned serialOverflowChecks = 0;
struct SerialManager {
    static SerialManager& getInstance() { static SerialManager serial; return serial; }
    void* getMutex(int) { return reinterpret_cast<void*>(2); }
    void checkAndReportOverflow(int) {
        assert(!heldSerialMutexes && basePriority == 8 && effectivePriority == 8);
        ++serialOverflowChecks;
    }
};
void observeRuntimeMemory() {}
enum class Hj212SendResult : uint8_t { SENT, FAILED, DEFERRED };
struct DTUManager {
    enum class HjSerialOwner { NONE, TIME_SYNC, CSQ, SET_IP, LIVE_PACKET, PENDING_PACKET, DTU_COMMAND };
    DTUDriver* _hj212DTU;
    std::atomic<uint32_t> _hjUploadWaiters{0}, _hjLiveWaiters{0}, _lastHjUploadEndMs{0};
    std::atomic<bool> _hjUploadActive{false}, _hjLiveActive{false};
    HjSerialOwner owner = HjSerialOwner::NONE;
    static constexpr int CSQ_DEFERRED = -2;
    explicit DTUManager(DTUDriver& driver) : _hj212DTU(&driver) {}
    void setHjSerialOwner(HjSerialOwner value) { owner = value; }
    void clearHjSerialOwner() { owner = HjSerialOwner::NONE; }
    void logHjSerialBusy(const char*, uint32_t = 0) const {}
    Hj212SendResult sendHJ212PacketInternal(const String&, int, uint32_t, int, uint64_t, bool);
    uint64_t hjSystemTime();
    int hj212DTUCSQ();
    bool updateHJDtuGoalIP(String);
};
#include "manager_source.h"

static void testPriorityInheritance() {
    // Reproduce the exact old failure with the faithful ESP-IDF model.
    basePriority = 8; effectivePriority = 9;
    vTaskPrioritySet(nullptr, 11);
    assert(basePriority == 11 && effectivePriority == 9);
    vTaskPrioritySet(nullptr, 8);
    assert(basePriority == 8 && effectivePriority == 9);
    Stream stream; DTUDriver driver("HJ212", 2); driver.setStream(stream);
    {
        Hj212PriorityScope scope;
        xSemaphoreTake(reinterpret_cast<void*>(2), 0);
        assert(driver.sendData("inherited", true, 30, &scope) == "");
        assert(stream.wire == "inherited" && basePriority == 9 && effectivePriority == 9);
        xSemaphoreGive(reinterpret_cast<void*>(2));
    }
    assert(basePriority == 8 && effectivePriority == 8);
    // Without a post-unlock scope there is no safe deferred-base restoration.
    basePriority = 8; effectivePriority = 9;
    const unsigned before = stream.writes; stream.wire.clear();
    assert(driver.sendData("must-not-write", false) == "" && stream.writes == before);
    const uint32_t commandStart = nowMs;
    assert(driver.sendCommand("blocked", 1000) == "" && nowMs == commandStart);
    assert(basePriority == 8 && effectivePriority == 9);
    // A concurrent higher inheritance between the two setters fails closed.
    {
        Hj212PriorityScope scope;
        xSemaphoreTake(reinterpret_cast<void*>(2), 0);
        priorityHook = [](unsigned p) { if (p == 9) effectivePriority = 10; };
        assert(driver.sendData("race-blocked", false, 30, &scope) == "");
        assert(stream.writes == before && stream.wire.empty());
        priorityHook = {};
        assert(driver.sendData("retry", false, 30, &scope) == "TX_OK");
        assert(basePriority == 10 && effectivePriority == 10);
        xSemaphoreGive(reinterpret_cast<void*>(2));
    }
    assert(basePriority == 8 && effectivePriority == 8);
    assert(std::any_of(logs.begin(), logs.end(), [](auto& l) { return l.find("HJ_UART_BLOCK") != l.npos; }));
    std::puts("PASS real inherited-priority contract: boost effective 9 to 11, preserve floor through ACK, restore base after unlock, race/no-scope block without GPIO writes");
}
static void testManagerPriorityLifecycle() {
    basePriority = effectivePriority = 8; logWaiterPriority = 9;
    Stream stream; DTUDriver driver("HJ212", 2); driver.setStream(stream);
    DTUManager manager(driver);
    const String flag4 = "QN=20261020120000123;MN=JCKJ123;Flag=4;CP=&&&&";
    stream.writeHook = [] { assert(heldSerialMutexes == 1 && basePriority == 11 && effectivePriority == 11); };
    // Inheritance occurs in the actual production TX_ATTEMPT logging point.
    priorityHook = [](unsigned p) { if (p == 8) assert(!heldSerialMutexes); };
    assert(manager.sendHJ212PacketInternal(flag4, 1, 1, 0, 20261020120000ULL, false) == Hj212SendResult::SENT);
    assert(basePriority == 8 && effectivePriority == 8 && !heldSerialMutexes);
    assert(!manager._hjUploadActive && !manager._hjLiveActive && manager.owner == DTUManager::HjSerialOwner::NONE);
    priorityHook = {};
    stream.shortWrite = true;
    assert(manager.sendHJ212PacketInternal(flag4, 2, 2, 0, 20261020120200ULL, false) == Hj212SendResult::FAILED);
    assert(basePriority == 8 && effectivePriority == 8 && !heldSerialMutexes);
    stream.shortWrite = false;
    const String flag5 = "QN=20261020120000123;MN=JCKJ123;Flag=5;CP=&&&&";
    assert(manager.sendHJ212PacketInternal(flag5, 1, 3, 0, 20261020120400ULL, false) == Hj212SendResult::FAILED);
    stream.writeHook = [&] { stream.incoming = "CN=9014;QN=20261020120000123;MN=JCKJ123;QnRtn=1;"; };
    assert(manager.sendHJ212PacketInternal(flag5, 1, 4, 0, 20261020120600ULL, false) == Hj212SendResult::SENT);
    assert(basePriority == 8 && effectivePriority == 8 && !heldSerialMutexes);
    manager._hjLiveWaiters = 1;
    assert(manager.sendHJ212PacketInternal(flag4, 1, 5, 0, 0, true) == Hj212SendResult::DEFERRED);
    manager._hjLiveWaiters = 0; rejectSerialLock = true;
    assert(manager.sendHJ212PacketInternal(flag4, 1, 6, 0, 0, false) == Hj212SendResult::FAILED);
    assert(!manager._hjUploadWaiters && !manager._hjLiveWaiters && !heldSerialMutexes);
    rejectSerialLock = false;
    // Both configuration commands share one serial transaction. The diagnostic
    // log between them reproduces the inherited floor before SAVE as well.
    stream.writeHook = [&] { stream.incoming = "ok\r\n"; };
    assert(manager.updateHJDtuGoalIP("127.0.0.1:1234"));
    assert(basePriority == 8 && effectivePriority == 8 && !heldSerialMutexes);
    assert(stream.wire.find("config,set,save\r\n") != std::string::npos);
    stream.wire.clear();
    priorityHook = [](unsigned p) { if (p == 9) effectivePriority = 10; };
    assert(!manager.updateHJDtuGoalIP("127.0.0.1:1234"));
    priorityHook = {};
    assert(stream.wire.find("config,set,save") == std::string::npos);
    assert(basePriority == 8 && effectivePriority == 8 && !heldSerialMutexes);
    basePriority = effectivePriority = 6; nowMs += 2000;
    stream.writeHook = [&] { stream.incoming = "config,csq,ok,28\r\n"; };
    assert(manager.hj212DTUCSQ() == 28);
    assert(basePriority == 6 && effectivePriority == 6 && !heldSerialMutexes);
    stream.writeHook = [&] { stream.incoming = "config,nettime,ok,2026,10,21,12,0,0,3\r\n"; };
    assert(manager.hjSystemTime() == 20261021120000ULL);
    assert(basePriority == 6 && effectivePriority == 6 && !heldSerialMutexes);
    basePriority = effectivePriority = 8; logWaiterPriority = 0;
    std::puts("PASS actual manager: log+serial inheritance, Flag4/5, retry/partial/timeout/ACK, busy/defer, SET+SAVE, maintenance CSQ/time, post-unlock restoration");
}

static void testTransmission() {
    basePriority = effectivePriority = 8; inheritedPriority = 0;
    Stream s; DTUDriver hj("HJ212", 2); hj.setStream(s);
    assert(hj.sendData("packet", false) == "TX_OK");
    assert(basePriority == 8 && s.wire == "packet" && s.flushes == 1);
    assert(hj.sendData("ack-wait", true, 30) == ""); // Delay asserts restored priority.
    assert(basePriority == 8 && nowMs >= 30);
    s.shortWrite = true; assert(hj.sendData("partial", false) == ""); assert(basePriority == 8);
    s.shortWrite = false; s.wire.clear();
    basePriority = 6; inheritedPriority = effectivePriority = 8;
    {
        Hj212PriorityScope scope;
        xSemaphoreTake(reinterpret_cast<void*>(2), 0);
        hj.sendCommand("config,get,csq", 20, &scope);
        assert(s.wire == "config,get,csq\r\n" && basePriority == 8 && effectivePriority == 8);
        xSemaphoreGive(reinterpret_cast<void*>(2));
    }
    assert(basePriority == 6 && effectivePriority == 6);
    inheritedPriority = 0; basePriority = effectivePriority = 8;
    DTUDriver remote("Remote", 1); Stream hw; hw.expectedPriority = 8; remote.setStream(hw);
    assert(remote.sendData("hardware", false) == "TX_OK" && basePriority == 8);
    basePriority = effectivePriority = 12;
    { Hj212TxGuard g(true); assert(basePriority == 12); } assert(basePriority == 12);
    basePriority = 8; effectivePriority = 12;
    { Hj212TxGuard g(true); assert(g.ready() && basePriority == 8 && effectivePriority == 12); }
    assert(basePriority == 8 && effectivePriority == 12);
    basePriority = 12; effectivePriority = 9;
    {
        Hj212PriorityScope scope;
        xSemaphoreTake(reinterpret_cast<void*>(2), 0);
        { Hj212TxGuard g(true, &scope); assert(g.ready() && basePriority == 12 && effectivePriority == 12); }
        xSemaphoreGive(reinterpret_cast<void*>(2));
    }
    assert(basePriority == 12 && effectivePriority == 12);
    basePriority = effectivePriority = 8;
    assert(std::any_of(logs.begin(), logs.end(), [](auto& l) {
        return l.find("priority_tx=11 priority_end=11 sd_begin=0 sd_end=0") != l.npos;
    }));
    std::puts("PASS UART boost covers writes only, restores inherited base, ACK/flush normal, partial writes fail, hardware UART unchanged");
}
static void testOzone() {
    HJ212_DataCenter center; HJ212CONFIG cfg;
    for (const char* alias : {"w34011", "a05024"}) {
        for (DataTime type : {DataTime::REAL_DATA, DataTime::MIN_DATA, DataTime::HOUR_DATA, DataTime::DAY_DATA}) {
            AllProcessedDataPacket d; d.dataTime = type; d.processed_data_map[alias] = {};
            for (bool newer : {false, true}) {
                String p = newer ? center.build2025Hj212Packet(&d, cfg) : center.build2017Hj212Packet(&d, cfg);
                assert(HJ212_DataCenter::isValidPacket(p));
                const char* cn = type == DataTime::REAL_DATA ? ";CN=2011;" :
                    type == DataTime::MIN_DATA ? ";CN=2051;" :
                    type == DataTime::HOUR_DATA ? ";CN=2061;" : ";CN=2031;";
                assert(p.indexOf(cn) >= 0);
                assert(p.indexOf("w34011-") >= 0 && p.indexOf("a05024-") == -1);
                assert(p.indexOf(type == DataTime::REAL_DATA ? "w34011-Rtd=19.63" : "w34011-Avg=19.63") >= 0);
                assert(!HJ212_DataCenter::rewriteLegacyOzoneCode(p));
            }
        }
    }
    // Build an old CRC-valid fixture without using the migration implementation.
    std::string body = "QN=20261020120000123;ST=22;CN=2051;PW=a05024;MN=a05024-JCK;Flag=4;CP=&&DataTime=20261020120000;a05024-Min=1.00,a05024-Avg=2.00,a05024-Max=3.00,a05024-Flag=N&&";
    // Published project CRC regression vector in the user's captured frame.
    String captured = "##0358QN=20261019152048275;ST=22;CN=2051;PW=123456;MN=TSP202610044;Flag=4;CP=&&DataTime=20261019151000;a34001-Min=27.00,a34001-Avg=30.20,a34001-Max=34.00,a34001-Flag=N;a34002-Min=27.00,a34002-Avg=27.80,a34002-Max=30.00,a34002-Flag=N;a34004-Min=15.00,a34004-Avg=15.40,a34004-Max=16.00,a34004-Flag=N;a34005-Min=12.00,a34005-Avg=13.00,a34005-Max=14.00,a34005-Flag=N&&A881\r\n";
    assert(HJ212_DataCenter::isValidPacket(captured));
    uint16_t crc = 0xffff;
    for (unsigned char c : body) { crc = uint16_t((crc >> 8) ^ c); for (int b = 0; b < 8; ++b) crc = uint16_t((crc >> 1) ^ ((crc & 1) ? 0xa001 : 0)); }
    char frame[1024]; std::snprintf(frame, sizeof(frame), "##%04u%s%04X\r\n", unsigned(body.size()), body.c_str(), unsigned(crc));
    String old = frame, original = old;
    assert(HJ212_DataCenter::rewriteLegacyOzoneCode(old));
    assert(old.length() == original.length() && HJ212_DataCenter::isValidPacket(old));
    assert(old.indexOf(";PW=a05024;MN=a05024-JCK;") >= 0);
    assert(old.indexOf("w34011-Min=1.00,w34011-Avg=2.00,w34011-Max=3.00,w34011-Flag=N") >= 0);
    assert(original == frame); // Original durable bytes are not overwritten.
    assert(!HJ212_DataCenter::rewriteLegacyOzoneCode(old));
    String corrupt = original; corrupt.setCharAt(corrupt.length() - 3, 'Z'); String copy = corrupt;
    assert(!HJ212_DataCenter::rewriteLegacyOzoneCode(corrupt) && corrupt == copy);
    std::puts("PASS 2017/2025 all CNs and ozone aliases; old frame migration preserves identity/value/length, refreshes CRC, rejects corruption");
}
static void testPending() {
    filesysManager fs; AllProcessedDataPacket d; String a = "one", b = "two", c = "three";
    nowMs = 0; diskCalls = diskLatency = 0;
    assert(fs.queuePendingPacket(&d, a) && fs.queuePendingPacket(&d, b));
    assert(diskCalls == 0 && fs._pendingWriteCount == 2 && fs._pendingWriteBytes == 6);
    assert(!fs.queuePendingPacket(&d, c) && c == "three");
    // One popped/in-flight job still consumes the two-job reservation.
    diskHook = [&] { assert(!fs.queuePendingPacket(&d, c)); };
    fs.servicePendingWrites(); diskHook = {}; fs.servicePendingWrites();
    assert(fs._pendingWriteCount == 0 && fs._pendingWriteBytes == 0);
    String huge(std::string(2049, 'x')); assert(!fs.queuePendingPacket(&d, huge));
    String one(std::string(1200, 'x')), two(std::string(900, 'x'));
    assert(fs.queuePendingPacket(&d, one) && !fs.queuePendingPacket(&d, two)); fs.servicePendingWrites();
    assert(fs._pendingWriteCount == 0 && fs._pendingWriteBytes == 0);
    pendingQueue.reject = true; String reject = "drop";
    assert(!fs.queuePendingPacket(&d, reject)); pendingQueue.reject = false;
    assert(fs._pendingWriteCount == 0 && fs._pendingWriteBytes == 0);
    // Unmounted/slow driver work happens only in the worker, then backs off.
    sdReady = false; diskLatency = 15000; String offline = "offline";
    uint32_t queuedAt = nowMs; unsigned before = diskCalls;
    assert(fs.queuePendingPacket(&d, offline) && nowMs == queuedAt && diskCalls == before);
    fs.servicePendingWrites(); assert(nowMs == queuedAt + 30000 && fs._pendingWriteCount == 1);
    before = diskCalls; fs.servicePendingWrites(); assert(diskCalls == before);
    nowMs += 59999; fs.servicePendingWrites(); assert(diskCalls == before);
    ++nowMs; fs.servicePendingWrites(); assert(fs._pendingWriteCount == 0 && diskCalls == before + 2);
    assert(fs._pendingWriteBytes == 0);
    sdReady = true; fullWrite = false; markerWrite = true; diskLatency = 0;
    String marker; assert(fs.queuePendingPacket(&d, marker)); fs.servicePendingWrites(); assert(fs._pendingWriteCount == 0);
    assert(std::any_of(logs.begin(), logs.end(), [](auto& l) { return l.find("outcome=rebuild_marker_saved") != l.npos; }));
    fullWrite = markerWrite = false; nowMs = 0xfffffff0U; String wrap = "wrap";
    assert(fs.queuePendingPacket(&d, wrap)); fs.servicePendingWrites(); before = diskCalls;
    nowMs += 59999; fs.servicePendingWrites(); assert(diskCalls == before);
    ++nowMs; fullWrite = true; fs.servicePendingWrites(); assert(fs._pendingWriteCount == 0 && fs._pendingWriteBytes == 0);
    String requeue = "requeue"; fullWrite = false; assert(fs.queuePendingPacket(&d, requeue));
    pendingQueue.reject = true; fs.servicePendingWrites(); pendingQueue.reject = false;
    assert(fs._pendingWriteCount == 0 && fs._pendingWriteBytes == 0 && pendingQueue.jobs.empty());
    assert(!fs.queuePendingPacket(nullptr, requeue));
    assert(std::any_of(logs.begin(), logs.end(), [](auto& l) { return l.find("durable=0") != l.npos; }));
    assert(std::any_of(logs.begin(), logs.end(), [](auto& l) { return l.find("outcome=unprotected") != l.npos; }));
    std::puts("PASS pending admission has no I/O/waits; count+byte limits include in-flight; slow/unmounted/failing SD, marker, 60s retry, wrap and cleanup");
}
static void testNormalSd() {
    using namespace SdIoDiagnostics;
    // Former pause boundaries, repeated initialization and clock wrap must
    // never prevent a real SD operation from being admitted.
    State policy;
    const uint32_t start = 0xfffffff0U;
    policy.begin(start);
    assert(policy.phase == Phase::Normal);
    for (uint32_t elapsed : {0U, 1799999U, 1800000U, 5399999U, 5400000U,
                             7199999U, 7200000U, 86400000U, 172800000U}) {
        for (Skip kind : {Skip::History, Skip::Pending, Skip::Query, Skip::Mount, Skip::Other}) {
            assert(policy.enter(start + elapsed, kind) && !policy.blocked());
            policy.begin(start + elapsed);
            assert(policy.since == start && policy.active == 1);
            policy.leave(start + elapsed + 1U);
            assert(policy.active == 0 && policy.phase == Phase::Normal);
        }
    }
    for (unsigned skipped : policy.skipped) assert(skipped == 0);

    nowMs = 100; begin();
    filesysManager fs; AllProcessedDataPacket packet;
    packet.processed_data_map["a01001"] = {};
    sdReady = fullWrite = markerWrite = true;
    diskLatency = 0; diskCalls = measurementWrites = 0;
    validHistoryQuery = true;
    for (uint32_t elapsed : {0U, 1800000U, 3600000U, 5400000U, 7200000U, 86400000U, 172800000U}) {
        nowMs = 100U + elapsed; poll();
        const uint32_t since = snapshot().since;
        begin();
        assert(!blocked() && snapshot().phase == Phase::Normal && snapshot().since == since);
        for (DataTime type : {DataTime::REAL_DATA, DataTime::MIN_DATA, DataTime::HOUR_DATA, DataTime::DAY_DATA}) {
            packet.dataTime = type;
            const unsigned before = diskCalls, saved = measurementWrites;
            fs.storeProcessedPacket(&packet);
            assert(diskCalls == before + 1 && measurementWrites == saved + 1);
        }
        String pending = "normal-pending";
        const unsigned beforePending = diskCalls;
        assert(fs.queuePendingPacket(&packet, pending));
        fs.servicePendingWrites();
        assert(diskCalls == beforePending + 1 && fs._pendingWriteCount == 0);
        assert(fs._pendingWriteBytes == 0 && pendingQueue.jobs.empty());
        JSONCmdData query; query.arguments = "valid-hour-query";
        const unsigned beforeQuery = diskCalls, replies = EventBus::replies;
        fs.processQuery(&query);
        assert(diskCalls == beforeQuery + 1 && EventBus::replies == replies + 1);
        assert(EventBus::lastReply != nullptr);
        delete static_cast<AllProcessedDataPacket*>(EventBus::lastReply);
        EventBus::lastReply = nullptr;
        assert(snapshot().active == 0 && heldSdMutexes == 0);
    }
    validHistoryQuery = false;
    // Mount retry/raw entry points reach the normal driver path rather than
    // returning the old artificial-pause error. No host file is created.
    auto& raw = file_storage::getInstance();
    sdReady = false;
    const unsigned previousMounts = mounts;
    assert(raw.SDcardInit() && mounts == previousMounts + 1);
    raw.sd_card_initialized.store(false); // Simulate an unmounted raw entry point.
    String content;
    assert(raw.readSDCard("/host-only-test/missing", content) == 1);
    assert(raw.writeSDCard("/host-only-test/missing", "data") == 1);
    raw.sd_card_initialized.store(true);
    sdReady = true;
    nowMs = 0xfffffff0U;
    {
        Guard slow("slow-normal-sd"); assert(slow.entered());
        nowMs += SLOW_IO_MS;
        Guard concurrent("concurrent-normal-sd");
        assert(concurrent.entered() && snapshot().active == 2 && !blocked());
    }
    assert(snapshot().active == 0);
    assert(std::any_of(logs.begin(), logs.end(), [](auto& l) { return l.find("SD_IO_SLOW") != l.npos; }));
    assert(std::none_of(logs.begin(), logs.end(), [](auto& l) {
        return l.find("SD_ABA_") != l.npos || l.find("reason=sd_aba_pause") != l.npos;
    }));
    std::puts("PASS continuous SD: real/min/hour/day store, pending, queries, mount/raw paths, former 30/60/30 boundaries, 48h, wrap and slow I/O accounting");
}
int main() { testTransmission(); testPriorityInheritance(); testManagerPriorityLifecycle(); testOzone(); testPending(); testNormalSd(); std::puts("All upload fault tests passed; hardware not verified."); }
