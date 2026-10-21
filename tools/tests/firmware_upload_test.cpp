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
    int indexOf(const String& s) const { auto n = text.find(s.text); return n == text.npos ? -1 : int(n); }
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
template<class... Args> void logLine(const char* fmt, Args... args) {
    if constexpr (sizeof...(Args) == 0) { logs.emplace_back(fmt); }
    else { char out[1024]; std::snprintf(out, sizeof(out), fmt, args...); logs.emplace_back(out); }
}
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
struct TaskStatus_t { unsigned uxBasePriority; };
void vTaskGetInfo(void*, TaskStatus_t* s, int, int) { s->uxBasePriority = basePriority; }
void vTaskPrioritySet(void*, unsigned p) { basePriority = p; effectivePriority = std::max(p, inheritedPriority); }
unsigned uxTaskPriorityGet(void*) { return effectivePriority; }
static unsigned heldSdMutexes = 0;
int xSemaphoreTake(void*, unsigned) { ++heldSdMutexes; return pdTRUE; }
void xSemaphoreGive(void*) { assert(heldSdMutexes); --heldSdMutexes; }
void yield() { assert(basePriority < 11); }
void vTaskDelay(unsigned ms) { assert(basePriority < 11); nowMs += ms; }
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
struct HJ212CONFIG { String st = "22", pw = "123456", mn = "JCKJ123", flag = "4"; };
struct CollectSetting { String unit = "ug/m3"; };
using COLLECTMAP = std::map<String, CollectSetting>;
struct ConfigManager {
    COLLECTMAP map;
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
    int available() { return int(incoming.size()); }
    char read() { char c = incoming.front(); incoming.erase(0, 1); return c; }
    size_t write(const uint8_t* data, size_t len) {
        assert(basePriority == expectedPriority); ++writes;
        size_t n = shortWrite && len ? len - 1 : len;
        wire.append(reinterpret_cast<const char*>(data), n); nowMs += uint32_t(len);
        return n;
    }
    void flush() { assert(basePriority < 11); ++flushes; }
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
struct config_json {
    explicit config_json(const char*) {}
    bool isValid() const { return false; }
    cJSON* getJsonObject() { return nullptr; }
};
String getJsonStringOrNumber(cJSON*, const char*) { return ""; }
bool parseTimestamp10(const String&, uint64_t&) { return false; }
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
    hj.sendCommand("config,get,csq", 20);
    assert(s.wire == "config,get,csq\r\n" && basePriority == 6 && effectivePriority == 8);
    inheritedPriority = 0; basePriority = effectivePriority = 8;
    DTUDriver remote("Remote", 1); Stream hw; hw.expectedPriority = 8; remote.setStream(hw);
    assert(remote.sendData("hardware", false) == "TX_OK" && basePriority == 8);
    basePriority = effectivePriority = 12;
    { Hj212TxGuard g(true); assert(basePriority == 12); } assert(basePriority == 12);
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
static void testSdComparison() {
    using namespace SdIoDiagnostics;
    // Pure policy: wrap, repeated begin and no false quiet phase during I/O.
    State policy;
    uint32_t start = 0xfffffff0U;
    policy.begin(start);
    assert(policy.enter(start + FIRST_ON_MS - 1U, Skip::Other));
    policy.advance(start + FIRST_ON_MS);
    assert(policy.phase == Phase::Draining && policy.active == 1);
    for (Skip s : {Skip::History, Skip::Pending, Skip::Query, Skip::Mount, Skip::Other})
        assert(!policy.enter(start + FIRST_ON_MS + 1U, s));
    policy.leave(start + FIRST_ON_MS + 5000U);
    const uint32_t quietStart = policy.since;
    assert(policy.phase == Phase::OffB && policy.active == 0);
    policy.begin(quietStart + 1U); assert(policy.since == quietStart);
    policy.advance(quietStart + OFF_MS - 1U); assert(policy.blocked());
    policy.advance(quietStart + OFF_MS); assert(policy.phase == Phase::OnA2);
    policy.advance(quietStart + OFF_MS + SECOND_ON_MS); assert(policy.phase == Phase::Complete);
    policy.advance(quietStart + OFF_MS + SECOND_ON_MS + 123456U); assert(!policy.blocked());
    for (unsigned value : policy.skipped) assert(value == 1);

    // Real diagnostic module + actual filesystem admission and storage function.
    nowMs = 100; begin();
    filesysManager fs; AllProcessedDataPacket d; d.processed_data_map["a01001"] = {};
    sdReady = fullWrite = markerWrite = true; diskLatency = 0; diskCalls = 0;
    fs.storeProcessedPacket(&d); assert(diskCalls == 1 && measurementWrites == 1);
    String pending = "already_queued"; assert(fs.queuePendingPacket(&d, pending));
    nowMs = 100 + FIRST_ON_MS - 1U;
    Guard slow("simulated_slow_sd"); assert(slow.entered());
    nowMs += 100; poll(); assert(snapshot().phase == Phase::Draining);
    const unsigned ioBefore = diskCalls;
    { ScopedSdLock denied(reinterpret_cast<void*>(1), "blocked_sd"); assert(!denied.locked()); }
    assert(heldSdMutexes == 0);
    // Live UART proceeds while the test waits for a genuinely quiet SD phase.
    Stream stream; DTUDriver hj("HJ212", 2); hj.setStream(stream);
    assert(hj.sendData("##example", false) == "TX_OK");
    slow.finish(); assert(snapshot().phase == Phase::OffB && snapshot().active == 0);
    const uint32_t offStart = snapshot().since;
    fs.storeProcessedPacket(&d); assert(diskCalls == ioBefore && measurementWrites == 1);
    String rejected = "must_not_accumulate";
    assert(!fs.queuePendingPacket(&d, rejected) && rejected == "must_not_accumulate");
    fs.servicePendingWrites();
    assert(fs._pendingWriteCount == 0 && fs._pendingWriteBytes == 0 && pendingQueue.jobs.empty());
    assert(diskCalls == ioBefore);
    auto& rawStorage = file_storage::getInstance();
    sdReady = false; String content;
    assert(!rawStorage.SDcardInit() && mounts == 0);
    assert(rawStorage.readSDCard("/does-not-exist", content) == 4);
    assert(rawStorage.writeSDCard("/does-not-exist", "data") == 4);
    assert(diskCalls == ioBefore && snapshot().active == 0);
    JSONCmdData query; query.arguments = "query-during-pause";
    const uint32_t beforeQuery = nowMs;
    fs.processQuery(&query);
    assert(EventBus::replies == 1 && EventBus::lastReply == nullptr);
    assert(nowMs == beforeQuery && diskCalls == ioBefore);
    nowMs = offStart + OFF_MS - 1U; poll(); assert(blocked());
    ++nowMs; poll(); assert(snapshot().phase == Phase::OnA2);
    sdReady = true; fs.storeProcessedPacket(&d);
    assert(diskCalls == ioBefore + 1 && measurementWrites == 2);
    assert(fs._pendingWriteCount == 0 && heldSdMutexes == 0);
    nowMs += SECOND_ON_MS; poll(); assert(snapshot().phase == Phase::Complete);
    begin(); assert(snapshot().phase == Phase::Complete);
    assert(std::any_of(logs.begin(), logs.end(), [](auto& l) { return l.find("phase=off_b") != l.npos; }));
    assert(std::any_of(logs.begin(), logs.end(), [](auto& l) { return l.find("phase=draining") != l.npos; }));
    assert(std::any_of(logs.begin(), logs.end(), [](auto& l) { return l.find("reason=sd_aba_pause") != l.npos; }));
    std::puts("PASS real SD comparison: 30/60/30, drain before quiet, timer wrap, history/raw/mount gate, pending release, UART continues, normal resumes");
}
int main() { testTransmission(); testOzone(); testPending(); testSdComparison(); std::puts("All upload fault tests passed; hardware not verified."); }
