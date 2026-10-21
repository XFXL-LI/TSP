// Host-only tests. The runner extracts the actual class/function bodies from
// each canonical variant into an ignored build directory. No device access.
#include <atomic>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <cJSON.h>
#include "app/configManager/SwitchConfigPolicy.h"
#include "app/ledManager/LedDisplayPolicy.h"
#include "app/tempManager/HeatingControlPolicy.h"
#include "module/diagnostics/SdIoDiagnostics.h"

using String = std::string;
struct SYSTEMSWITCH {
    bool tempConSwitch = true, log_to_sd = true, save_min_data = true;
    bool save_hour_data = true, save_day_data = true, enable_hj212 = true;
    bool mqtt_public = false;
};
#include "switch_source.h"

#define LOG_DEBUG(...) ((void)0)
#define LOG_INFO(...) ((void)0)
#define LOG_WARNING(...) ((void)0)
#define LOG_ERROR(...) ((void)0)
#define TEMP_HEAT_PIN 42
#define FAN_PIN 14
#define TEMP_I2C_SDA 8
#define TEMP_I2C_SCL 18
#define OUTPUT 1
#define LOW 0
#define HIGH 1
#define pdTRUE 1
#define pdMS_TO_TICKS(x) (x)
using TaskHandle_t = void *;
using portMUX_TYPE = std::mutex;
#define portMUX_INITIALIZER_UNLOCKED {}
#define taskENTER_CRITICAL(x) (x)->lock()
#define taskEXIT_CRITICAL(x) (x)->unlock()
static std::atomic<uint32_t> taskNow{0};
namespace SdIoDiagnostics {
static bool testBlocked = false;
static unsigned testSkips = 0;
bool blocked() { return testBlocked; }
void noteSkip(Skip) { ++testSkips; }
}
static std::atomic<int> pins[64];
static unsigned notifications = 0;
unsigned long millis() { return taskNow.load(); }
void pinMode(int, int) {}
void digitalWrite(int pin, int value) { pins[pin].store(value); }
void xTaskNotifyGive(TaskHandle_t) { ++notifications; }
int xSemaphoreTake(void *, unsigned) { return pdTRUE; }
void xSemaphoreGive(void *) {}
struct { void *mutex = reinterpret_cast<void *>(1); float temp = 0, mete = 0; } systemInfo;
struct SHT30 {
    static inline bool valid = true;
    static inline float temp = 30, humi = 80;
    static inline unsigned reads = 0;
    bool begin(int, int, int) { return true; }
    bool readTemperatureHumidity(float &t, float &h) {
        ++reads; t = temp; h = humi; return valid;
    }
};
#include "thermal_source.h"

struct ProcessedDataPacket { float value; bool is_valid; };
struct AllProcessedDataPacket { std::map<String, ProcessedDataPacket> processed_data_map; unsigned released = 0; void release() { ++released; } };
struct JSONCmdData { unsigned released = 0; void release() { ++released; } };
struct CollectSetting { int alarmLimit = 100; String unit = "ppb"; };
using COLLECTMAP = std::map<String, CollectSetting>;
// Gas unit conversion is unchanged and is outside these control tests.
namespace GasUnitConverter {
bool isGasSensor(const String &s) {
    return s == "a21004" || s == "a21005" || s == "a21026" || s == "w34011";
}
String normalizeUnit(const String &, const String &unit) { return unit.empty() ? "ppb" : unit; }
float convertFromPpb(const String &, float value, const String &) { return value; }
uint8_t decimalsForUnit(const String &) { return 1; }
}
#define SERIAL_LED 3
#define RS485_UPDATE_DATA 0x37
struct SerialManager {
    struct Attempt { uint32_t started, finished; uint8_t region; bool success; };
    std::vector<std::vector<uint8_t>> frames;
    std::vector<Attempt> attempts;
    std::map<unsigned, std::vector<uint8_t>> cardFrames;
    unsigned receiverDrops = 0;
    unsigned fail43 = 0;
    static SerialManager &getInstance() { static SerialManager value; return value; }
    size_t write(int, const uint8_t *data, size_t len) {
        const uint32_t started = taskNow.load();
        // 9600 baud, 8N1. UART success is not an acknowledgement from the card.
        const uint32_t finished = started + (len * 10000U + 9599U) / 9600U;
        const bool accepted = attempts.empty() ||
            uint32_t(started - attempts.back().finished) >= LedDisplayPolicy::FRAME_INTERVAL_MS;
        const bool success = !(data[15] == 43 && fail43 > 0);
        if (!success) --fail43;
        attempts.push_back({started, finished, data[15], success});
        taskNow = finished;
        if (!success) return 0;
        frames.emplace_back(data, data + len);
        if (accepted) cardFrames[data[15]] = frames.back();
        else ++receiverDrops;
        return len;
    }
};
#include "led_source.h"

enum class EventID { PROCESSED_DATA_COLLECTED, RUNTIME_SWITCH_CHANGED, RECORD_QUERY_REQ, OTHER };
struct EventMsg { EventID id; void *data; };
struct HostQueue {
    std::deque<EventMsg> items;
    std::vector<EventMsg> consumed;
    size_t capacity = 5;
    unsigned resetCount = 0;
};
using QueueHandle_t = HostQueue *;
using UBaseType_t = unsigned;
using BaseType_t = int;
constexpr unsigned portMAX_DELAY = 0xffffffffU;
unsigned uxQueueMessagesWaiting(QueueHandle_t q) { return static_cast<unsigned>(q->items.size()); }
unsigned uxQueueSpacesAvailable(QueueHandle_t q) { return static_cast<unsigned>(q->capacity - q->items.size()); }
int xQueueSend(QueueHandle_t q, const EventMsg *msg, unsigned timeout) {
    if (q->items.size() == q->capacity) {
        if (timeout != portMAX_DELAY) return 0;
        // Simulate one normal consumer drain while the blocking send waits.
        q->consumed.push_back(q->items.front()); q->items.pop_front();
    }
    q->items.push_back(*msg); return pdTRUE;
}
int xQueueReceive(QueueHandle_t q, EventMsg *msg, unsigned) {
    if (q->items.empty()) return 0;
    *msg = q->items.front(); q->items.pop_front(); return pdTRUE;
}
void xQueueReset(QueueHandle_t q) { ++q->resetCount; q->items.clear(); }
struct EventBus {
    void *busMutex = reinterpret_cast<void *>(1);
    std::map<EventID, std::vector<QueueHandle_t>> subscribers;
    std::map<QueueHandle_t, String> queueNames;
    std::atomic<uint32_t> droppedMessages{0};
    void publish(EventID id, void *data);
};
#include "event_source.h"

static unsigned cases = 0;
static void pass(const char *name) { ++cases; std::printf("PASS %s\n", name); }
static void checkBool(cJSON *root, const char *name, bool value) {
    cJSON *item = cJSON_GetObjectItemCaseSensitive(root, name);
    assert(cJSON_IsBool(item)); assert((cJSON_IsTrue(item) != 0) == value);
}
static void testSwitches() {
    using namespace SwitchConfigPolicy;
    for (uint8_t bits = 0; bits < 8; ++bits) assert(pack(unpack(bits)) == bits);
    Controls controls;
    BoolField absent{false, false, false}, yes{true, true, true};
    BoolField no{true, true, false}, numeric{true, false, true};
    assert(!applyClient(controls, absent, absent, yes, numeric));
    assert(pack(controls) == 3); // Transactional reject, not partially applied.
    assert(applyClient(controls, no, yes, no, yes));
    assert(!controls.heatingEnabled && !controls.ledEnabled && controls.ledRegions == 4);
    assert(!readHeating(numeric, yes, controls)); // Invalid canonical wins over alias.
    pass("atomic control encoding and strict boolean aliases");

    SYSTEMSWITCH stored;
    controls = Controls{};
    auto *root = cJSON_Parse(R"({"tempConSwitch":false,"save_raw_data":true,"enable_remote_dtu":false,"mqtt_public":true})");
    assert(readStoredSwitch(root, stored, controls)); cJSON_Delete(root);
    assert(!controls.heatingEnabled && controls.ledEnabled && controls.ledRegions == 2);
    assert(stored.mqtt_public);
    root = cJSON_Parse(R"({"tempControlSwitch":true,"tempConSwitch":false,"led":{"enabled":false,"region_count":4}})");
    assert(readStoredSwitch(root, stored, controls)); cJSON_Delete(root);
    assert(controls.heatingEnabled && !controls.ledEnabled && controls.ledRegions == 4);
    root = cJSON_Parse(R"({"led":{"enabled":true,"region_count":3}})");
    assert(!readStoredSwitch(root, stored, controls)); cJSON_Delete(root);
    root = cJSON_Parse(R"({"tempControlSwitch":"false"})");
    assert(!readStoredSwitch(root, stored, controls)); cJSON_Delete(root);
    root = cJSON_Parse(R"({"log_to_sd":1})");
    assert(!readOtherSwitches(root, stored)); cJSON_Delete(root);
    pass("old-file migration defaults and canonical stored validation");

    char text[512];
    assert(serializeSwitch(stored, controls, false, text, sizeof(text)));
    root = cJSON_Parse(text); assert(root);
    assert(!cJSON_GetObjectItem(root, "save_raw_data"));
    assert(!cJSON_GetObjectItem(root, "enable_remote_dtu"));
    auto *led = cJSON_GetObjectItem(root, "led");
    checkBool(led, "enabled", false);
    assert(cJSON_GetObjectItem(led, "region_count")->valueint == 4); cJSON_Delete(root);
    assert(serializeSwitch(stored, controls, true, text, sizeof(text)));
    root = cJSON_Parse(text); assert(root);
    checkBool(root, "tempControlSwitch", true); checkBool(root, "save_raw_data", true);
    checkBool(root, "enable_remote_dtu", false); checkBool(root, "mqtt_public", true);
    assert(!cJSON_GetObjectItem(root, "led")); assert(!cJSON_GetObjectItem(root, "tempConSwitch"));
    cJSON_Delete(root);
    root = cJSON_Parse(R"({"tempControlSwitch":false,"save_raw_data":false,"enable_remote_dtu":true,"save_min_data":false})");
    assert(readOtherSwitches(root, stored)); assert(stored.mqtt_public && !stored.save_min_data);
    cJSON_Delete(root); assert(!serializeSwitch(stored, controls, true, text, 8));
    pass("canonical storage / unchanged LCD-DTU wire names and omitted-field preservation");
}
static void testCache() {
    LedDisplayPolicy::Cache cache;
    for (int i = 0; i < 14; ++i) { char s[8]; std::snprintf(s, sizeof(s), "%d", i); assert(cache.append(s, false)); }
    assert(!cache.append("overflow", false)); assert(cache.count() == 14);
    assert(cache.pageCount(2) == 7 && cache.pageCount(4) == 4);
    cache.advance(2); assert(std::strcmp(cache.slot(0, 2)->text, "2") == 0);
    cache.beginUpdate(); for (int i = 0; i < 11; ++i) { char s[8]; std::snprintf(s, sizeof(s), "%d", i); cache.append(s, false); }
    cache.finishUpdate(2); assert(std::strcmp(cache.slot(0, 2)->text, "2") == 0);
    assert(cache.pageCount(2) == 6 && cache.pageCount(4) == 3);
    cache.resetPage(); cache.advance(4); cache.advance(4);
    assert(cache.slot(2, 4) && !cache.slot(3, 4));
    cache.beginUpdate(); cache.append("only", false); cache.finishUpdate(2);
    assert(std::strcmp(cache.slot(0, 2)->text, "only") == 0);
    LedDisplayPolicy::Item item;
    LedDisplayPolicy::setItem(item, "12345678901234567890", true);
    assert(std::strlen(item.text) == 16 && item.alarm);
    const char longGbk[] = "123456789012345\xb7\xe7xx";
    LedDisplayPolicy::setItem(item, longGbk, false);
    assert(std::strlen(item.text) == 15); // Same truncation rule as the real encoder.
    assert(LedDisplayPolicy::due(4, 0xfffffffeU));
    assert(!LedDisplayPolicy::due(0xfffffffeU, 4));
    pass("bounded cache, 2/4 pagination, snapshot cursor and millis wrap");
}
static void testControlQueue() {
    HostQueue queue; EventBus bus;
    bus.subscribers[EventID::RUNTIME_SWITCH_CHANGED].push_back(&queue);
    for (uintptr_t i = 1; i <= 5; ++i) queue.items.push_back({EventID::PROCESSED_DATA_COLLECTED, reinterpret_cast<void *>(i)});
    bus.publish(EventID::RUNTIME_SWITCH_CHANGED, nullptr);
    assert(queue.resetCount == 0 && bus.droppedMessages.load() == 0);
    assert(queue.consumed.size() == 1 && queue.consumed[0].data == reinterpret_cast<void *>(1));
    for (uintptr_t i = 2; i <= 5; ++i) {
        assert(queue.items.front().data == reinterpret_cast<void *>(i)); queue.items.pop_front();
    }
    assert(queue.items.size() == 1 && queue.items.front().id == EventID::RUNTIME_SWITCH_CHANGED);
    pass("full LED queue preserves all owned packets when publishing control changes");
}
static void testStorageBackpressure() {
    HostQueue storage, upload; storage.capacity = 4;
    EventBus bus; bus.queueNames[&storage] = "STORAGE"; bus.queueNames[&upload] = "HJ212";
    bus.subscribers[EventID::PROCESSED_DATA_COLLECTED] = {&storage, &upload};
    AllProcessedDataPacket packet;
    for (unsigned i = 0; i < 4; ++i)
        storage.items.push_back({EventID::PROCESSED_DATA_COLLECTED, nullptr});
    bus.publish(EventID::PROCESSED_DATA_COLLECTED, &packet);
    assert(packet.released == 1 && storage.items.size() == 4);
    assert(storage.resetCount == 0 && storage.consumed.empty());
    assert(upload.items.size() == 1 && upload.items.front().data == &packet);
    JSONCmdData query;
    bus.subscribers[EventID::RECORD_QUERY_REQ] = {&storage};
    bus.publish(EventID::RECORD_QUERY_REQ, &query);
    assert(query.released == 1 && storage.items.size() == 4);
    assert(bus.droppedMessages.load() == 2);
    pass("stalled SD queue preserves owned work, releases rejected references and never blocks live delivery");
    storage.items.clear(); upload.items.clear();
    SdIoDiagnostics::testBlocked = true;
    bus.publish(EventID::PROCESSED_DATA_COLLECTED, &packet);
    SdIoDiagnostics::testBlocked = false;
    assert(packet.released == 2 && storage.items.empty());
    assert(upload.items.size() == 1 && upload.items.front().data == &packet);
    assert(SdIoDiagnostics::testSkips == 1 && bus.droppedMessages.load() == 2);
    pass("SD diagnostic pause rejects history admission without retaining maps or dropping live delivery");
}
static void testHeating() {
    auto &heat = TempManager::getInstance();
    heat.setHeatingEnabled(false); heat.begin(); heat.bindControlTask(reinterpret_cast<void *>(1));
    heat.poll(); assert(SHT30::reads == 1 && systemInfo.temp == 30 && systemInfo.mete == 80);
    assert(pins[TEMP_HEAT_PIN] == LOW && !heat.isHeatingActive() && pins[FAN_PIN] == HIGH);
    heat.setHeatingEnabled(true); heat.applyHeatingControl();
    assert(heat.isHeatingActive() && pins[TEMP_HEAT_PIN] == HIGH);
    heat.setHeatingEnabled(false); assert(pins[TEMP_HEAT_PIN] == LOW && !heat.isHeatingActive());
    assert(pins[FAN_PIN] == HIGH && notifications == 2);
    pass("heater master off is immediate; SHT30 and fan stay alive");
    SHT30::valid = false; heat.poll(); heat.setHeatingEnabled(true); heat.applyHeatingControl();
    assert(!heat.isHeatingActive());
    SHT30::valid = true; SHT30::temp = 15; SHT30::humi = 60; heat.poll();
    assert(heat.isHeatingActive() && pins[FAN_PIN] == LOW);
    heat.setTargetTemp(35, 15); heat.setTargetHumi(70, 40);
    SHT30::temp = 15; SHT30::humi = 70; heat.poll(); assert(!heat.isHeatingActive());
    SHT30::temp = 30; SHT30::humi = 71; heat.poll(); assert(heat.isHeatingActive());
    taskNow = 6U * 60 * 60 * 1000 + 1; heat.applyHeatingControl(); assert(!heat.isHeatingActive());
    SHT30::valid = false; heat.poll(); assert(!heat.isHeatingActive());
    pass("original temperature/humidity boundaries, invalid sample and six-hour cutoff");
    taskNow = 0; SHT30::valid = true; SHT30::temp = 10; SHT30::humi = 80; heat.poll();
    std::atomic<bool> run{true};
    std::thread worker([&] { while (run.load()) heat.applyHeatingControl(); });
    for (int i = 0; i < 20000; ++i) {
        heat.setHeatingEnabled(false); assert(pins[TEMP_HEAT_PIN] == LOW && !heat.isHeatingActive());
        heat.setHeatingEnabled(true);
    }
    run = false; worker.join(); heat.setHeatingEnabled(false);
    assert(pins[TEMP_HEAT_PIN] == LOW && !heat.isHeatingActive());
    pass("20,000 cross-thread switch-off / stale-decision races");
}
static String frameText(const std::vector<uint8_t> &f) {
    assert(f.size() == size_t(21 + f[19]) && f[19] <= 16);
    assert(f[0] == 0xfe && f[1] == 0x98 && f[4] == 0x97 && f[5] == 0x37);
    assert((size_t(f[2]) * 256 + f[3]) == f.size() - 2);
    uint8_t sum = 0; for (size_t i = 1; i + 1 < f.size(); ++i) sum ^= f[i];
    assert(sum == f.back());
    return String(reinterpret_cast<const char *>(&f[20]), f[19]);
}
static String lastText(unsigned region) {
    auto &frames = SerialManager::getInstance().frames;
    for (auto it = frames.rbegin(); it != frames.rend(); ++it) if ((*it)[15] == region) return frameText(*it);
    assert(false); return {};
}
static AllProcessedDataPacket allFactors() {
    AllProcessedDataPacket packet;
    for (const char *id : LedDisplayPolicy::SENSOR_ORDER) packet.processed_data_map[id] = {1.0f, true};
    return packet;
}
static void at(LedManager &led, uint32_t now) {
    auto &serial = SerialManager::getInstance();
    const auto before = serial.attempts.size();
    taskNow = now; led.poll(now);
    assert(serial.attempts.size() <= before + 1); // Never burst multiple UART writes.
}
static void configureAt(LedManager &led, bool enabled, uint8_t regions, uint32_t now) {
    auto &serial = SerialManager::getInstance();
    const auto before = serial.attempts.size();
    taskNow = now; led.configure(enabled, regions, now);
    assert(serial.attempts.size() == before); // Only queue; no synchronous clear burst.
}
static void finishBurst(LedManager &led) {
    for (unsigned i = 0; i < 16; ++i) {
        const uint32_t wait = led.waitMs(taskNow.load());
        if (wait > LedDisplayPolicy::FRAME_INTERVAL_MS) return;
        at(led, taskNow.load() + wait);
    }
    assert(false); // Bounded four-slot work, no busy-loop after completion.
}
static void pageAt(LedManager &led, uint32_t now) { at(led, now); finishBurst(led); }
static void testLed() {
    auto &led = LedManager::getInstance(); auto &serial = SerialManager::getInstance();
    COLLECTMAP settings;
    configureAt(led, true, 2, 0); at(led, 0); assert(serial.frames.empty());
    auto packet = allFactors(); led.updateCache(&packet, settings); pageAt(led, 0);
    assert(lastText(41).find("TSP:") == 0 && lastText(42).find("PM10:") == 0);
    assert(lastText(43) == " " && lastText(44) == " ");
    const auto initialCount = serial.frames.size(); at(led, 4999); assert(serial.frames.size() == initialCount);
    pageAt(led, 5000); assert(lastText(41).find("PM2.5:") == 0 && lastText(42).find("PM1:") == 0);
    // A new snapshot does not restart at particle page one.
    led.updateCache(&packet, settings); pageAt(led, 10000);
    assert(static_cast<uint8_t>(lastText(41)[0]) == 0xce); // temperature
    for (uint32_t t = 15000; t <= 35000; t += 5000) pageAt(led, t);
    assert(lastText(41).find("TSP:") == 0); // Keeps rotating beyond first batch window.
    pass("real LED frames: no startup message, 5-second pages, preserved cursor and continuous cycle");
    configureAt(led, true, 4, 40000); pageAt(led, 40000);
    assert(lastText(41).find("TSP:") == 0 && lastText(43).find("PM2.5:") == 0 && lastText(44).find("PM1:") == 0);
    packet.processed_data_map.erase("a34001"); packet.processed_data_map.erase("a34005"); packet.processed_data_map.erase("L90");
    configureAt(led, false, 4, 41000); pageAt(led, 41000); led.updateCache(&packet, settings);
    configureAt(led, true, 4, 42000); pageAt(led, 42000);
    assert(lastText(41).find("PM10:") == 0 && lastText(42).find("PM2.5:") == 0);
    pageAt(led, 47000); assert(lastText(44).find("NO2:") == 0); // compact, no category gaps
    pageAt(led, 52000); assert(lastText(41).find("CO:") == 0 && lastText(43).find("O3:") == 0 && lastText(44) == " ");
    pageAt(led, 57000); assert(lastText(41).find("PM10:") == 0);
    pass("11-factor dust mode: three compact four-region pages and partial-page clearing");
    // Failed clears must retry even if the old slot already had a successful-write bit.
    serial.fail43 = 2;
    configureAt(led, true, 2, 58000); assert(lastText(43) != " "); pageAt(led, 58000);
    assert(lastText(43) != " " && !serial.attempts.back().success);
    auto failedAt = serial.attempts.back().finished;
    auto attempts = serial.attempts.size();
    at(led, failedAt + 4999); assert(serial.attempts.size() == attempts && led.waitMs(taskNow) == 1);
    at(led, failedAt + 5000); assert(!serial.attempts.back().success && lastText(43) != " ");
    failedAt = serial.attempts.back().finished;
    pageAt(led, failedAt + 5000); assert(lastText(43) == " " && lastText(44) == " ");
    auto base = taskNow.load() + 1000;
    configureAt(led, false, 2, base); pageAt(led, base);
    for (unsigned region = 41; region <= 44; ++region) assert(lastText(region) == " ");
    auto disabledCount = serial.frames.size(); at(led, taskNow.load() + 5000); assert(serial.frames.size() == disabledCount);
    pass("four-to-two cleanup, failed-clear retry and LED disable");
    packet.processed_data_map.clear(); packet.processed_data_map["a34002"] = {1.0f, true};
    base = taskNow.load() + 1000;
    led.updateCache(&packet, settings); configureAt(led, true, 2, base); pageAt(led, base);
    auto singleCount = serial.frames.size(); pageAt(led, base + 5000); pageAt(led, base + 10000);
    assert(serial.frames.size() == singleCount && lastText(42) == " ");
    settings["a34002"].alarmLimit = 1; led.updateCache(&packet, settings); pageAt(led, base + 15000);
    assert(serial.frames.size() == singleCount + 1 && serial.frames.back()[17] == 0x21);
    settings["a34002"].alarmLimit = 0; led.updateCache(&packet, settings); pageAt(led, base + 20000);
    assert(serial.frames.size() == singleCount + 2 && serial.frames.back()[17] == 0xff);
    singleCount = serial.frames.size();
    packet.processed_data_map["a34002"].is_valid = false; led.updateCache(&packet, settings); pageAt(led, base + 25000);
    assert(serial.frames.size() == singleCount && lastText(41).find("PM10:") == 0);
    // Exact GBK label, unchanged numeric/unit formatting, and wire-byte limit.
    packet.processed_data_map.clear(); packet.processed_data_map["a01006"] = {101.6f, true};
    const uint32_t pressureAt = taskNow.load() + 1000;
    led.updateCache(&packet, settings); pageAt(led, pressureAt);
    const char *pressureText = "\xb4\xf3\xc6\xf8\xd1\xb9:101.6KPa";
    assert(std::strlen(pressureText) == 15 && lastText(41) == pressureText);
    assert(serial.frames.back()[17] == 0xff);
    settings["a01006"].alarmLimit = 102;
    led.updateCache(&packet, settings); pageAt(led, pressureAt + 5000);
    assert(lastText(41) == pressureText && serial.frames.back()[17] == 0x21);
    packet.processed_data_map["a01006"].value = 1000.0f;
    led.updateCache(&packet, settings); pageAt(led, pressureAt + 10000);
    assert(lastText(41) == "\xb4\xf3\xc6\xf8\xd1\xb9:1000.0KPa");
    assert(lastText(41).size() == 16); // Boundary still fits without truncation.
    for (auto &frame : serial.frames) frameText(frame);
    pass("single-page suppression, alarm colors, invalid data, pressure GBK label and all wire lengths/checksums");
    // Refreshing the collection cache halfway through transmission must not mix pages.
    base = taskNow.load() + 1000;
    configureAt(led, false, 4, base); pageAt(led, base);
    packet = allFactors(); led.updateCache(&packet, settings);
    base = taskNow.load() + 1000;
    configureAt(led, true, 4, base); at(led, base);
    assert(serial.attempts.back().region == 41);
    const uint32_t firstEnd = taskNow.load(); attempts = serial.attempts.size();
    at(led, firstEnd + 49); assert(serial.attempts.size() == attempts && led.waitMs(taskNow) == 1);
    for (auto &entry : packet.processed_data_map) entry.second.value = 9.0f;
    led.updateCache(&packet, settings);
    at(led, firstEnd + 50); assert(serial.attempts.size() == attempts + 1 && serial.attempts.back().region == 42);
    finishBurst(led);
    for (unsigned region = 41; region <= 44; ++region) assert(lastText(region).find(":1.0") != String::npos);
    pageAt(led, base + 5000);
    for (unsigned region = 41; region <= 44; ++region) assert(lastText(region).find("9.0") != String::npos);
    pass("50ms quiet gap after UART completion; mid-burst cache refresh keeps one coherent page");

    base = taskNow.load() + 1000;
    configureAt(led, false, 4, base); pageAt(led, base);
    packet = allFactors(); led.updateCache(&packet, settings);
    base = taskNow.load() + 1000;
    configureAt(led, true, 4, base); at(led, base);
    const auto switchEnd = taskNow.load(); attempts = serial.attempts.size();
    configureAt(led, true, 2, switchEnd + 1); at(led, switchEnd + 1);
    assert(serial.attempts.size() == attempts);
    pageAt(led, switchEnd + 50);
    assert(lastText(41).find("TSP:") == 0 && lastText(42).find("PM10:") == 0);
    assert(lastText(43) == " " && lastText(44) == " ");
    for (auto it = serial.frames.rbegin(); it != serial.frames.rend() && it->at(15) >= 43; ++it)
        assert(frameText(*it) == " ");
    pass("four-to-two mid-burst cancels stale content without resetting the quiet-gap deadline");

    base = taskNow.load() + 1000;
    configureAt(led, true, 4, base); at(led, base);
    const auto disableEnd = taskNow.load(); attempts = serial.attempts.size();
    configureAt(led, false, 4, disableEnd + 1); at(led, disableEnd + 1);
    assert(serial.attempts.size() == attempts);
    pageAt(led, disableEnd + 50);
    for (unsigned region = 41; region <= 44; ++region) assert(lastText(region) == " ");
    disabledCount = serial.frames.size(); at(led, taskNow.load() + 5000);
    assert(serial.frames.size() == disabledCount);
    pass("disable during transmission cancels unsent page and clears all regions with spaced frames");

    // Move the clock in steps below the signed-half-range before crossing wrap.
    packet = allFactors(); led.updateCache(&packet, settings);
    configureAt(led, true, 4, 0x70000000U); pageAt(led, 0x70000000U);
    configureAt(led, false, 4, 0xe0000000U); pageAt(led, 0xe0000000U);
    configureAt(led, true, 4, 0xfffffe00U); pageAt(led, 0xfffffe00U);
    configureAt(led, false, 4, 0xfffffff0U); at(led, 0xfffffff0U);
    assert(taskNow.load() < 50); // The actual mock UART transmission crossed wrap.
    const auto wrapEnd = taskNow.load(); attempts = serial.attempts.size();
    at(led, wrapEnd + 49); assert(serial.attempts.size() == attempts && led.waitMs(taskNow) == 1);
    pageAt(led, wrapEnd + 50);
    for (unsigned region = 41; region <= 44; ++region) assert(lastText(region) == " ");
    pass("frame pacing and clearing survive millis wrap during synchronous UART transmission");

    assert(serial.receiverDrops == 0);
    for (size_t i = 1; i < serial.attempts.size(); ++i) {
        assert(uint32_t(serial.attempts[i].started - serial.attempts[i - 1].finished) >= 50);
    }
    for (unsigned region = 41; region <= 44; ++region) assert(frameText(serial.cardFrames[region]) == lastText(region));
    pass("9600-baud receiver model accepts every region; all successful UART writes obey card quiet gap");
    std::printf("Host sizeof LED manager=%zu, cache=%zu (firmware ELF is RAM authority)\n", sizeof(LedManager), sizeof(LedDisplayPolicy::Cache));
}
int main() {
    testSwitches(); testCache(); testControlQueue(); testStorageBackpressure(); testHeating(); testLed();
    std::printf("All %u control regression groups passed; hardware not verified.\n", cases);
}
