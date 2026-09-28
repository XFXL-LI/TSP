// Host: g++ -std=c++17 -I firmware/standard/TSP/src tools/tests/get_data_status_test.cpp -o status-test
// ESP cross-compiler: use -fsyntax-only; constexpr assertions still execute at compile time.
#include "module/diagnostics/CalendarClock.h"
#include "module/diagnostics/RealtimeSnapshotInfo.h"
#include "module/json/GetDataResponse.h"
#include <assert.h>
#include <float.h>
#include <string.h>

static_assert(CalendarClock::isValidTimestamp(20200101000000ULL));
static_assert(CalendarClock::isValidTimestamp(20240229235959ULL));
static_assert(CalendarClock::isValidTimestamp(20991231235959ULL));
static_assert(!CalendarClock::isValidTimestamp(20230229000000ULL));
static_assert(!CalendarClock::isValidTimestamp(20260431000000ULL));
static_assert(!CalendarClock::isValidTimestamp(20260928240000ULL));
static_assert(!CalendarClock::isValidTimestamp(20260928120060ULL));
static_assert(!CalendarClock::isValidTimestamp(0));
static_assert(!CalendarClock::isValidTimestamp(202609281200ULL));
static_assert(CalendarClock::isValidClockInput(202609281200ULL));
static_assert(!CalendarClock::isValidClockInput(UINT64_MAX));
static_assert(nextRealtimeSequence(0) == 1);
static_assert(nextRealtimeSequence(UINT32_MAX) == 1);
static_assert(realtimeAgeMs(0, 0, 1000000) == -1);
static_assert(realtimeAgeMs(1, 1000000, 4000000) == 3000);
static_assert(realtimeAgeMs(1, 0, (uint64_t(UINT32_MAX) + 1) * 1000) == UINT32_MAX);
static_assert(realtimeAgeMs(1, 9000, 1000) == 0);

int main() {
    char response[GetDataResponse::BUFFER_CAPACITY] = {};
    size_t used = 0;
    const char* ids[16] = {"a34004", "a34002", "a34001", "a34005",
        "a01007", "a01008", "a01006", "a01001", "a01002", "L90",
        "w34011", "a21004", "a21005", "a21026", "a24035", "a34004"};
    float values[16] = {12, 18};
    uint8_t decimals[16] = {2, 2};
    RealtimeSnapshotInfo sample;
    sample.seq = 1;
    sample.ageMs = 3000;
    sample.validMask = 3;
    DeviceStatusSnapshot device;
    device.init = 1;
    device.sd = 2;
    auto build = [&](size_t count, const DeviceStatusSnapshot* ds, size_t capacity = sizeof(response)) {
        return GetDataResponse::build(response, capacity, used, ids, values, decimals,
            count, 0, 99, 25.3f, 48.0f, sample, ds);
    };

    assert(build(2, nullptr));
    assert(strcmp(response, "{\"operation\":\"get_data\",\"code\":\"OK\","
        "\"message\":\"get data success\",\"timestamp\":0,\"csq\":99,"
        "\"temp\":25.30,\"mete\":48.00,\"params\":[\"a34004\",\"a34002\"],"
        "\"values\":[12.00,18.00]}") == 0);
    assert(build(2, &device));
    assert(strstr(response, "\"valid_mask\":3") != nullptr);
    assert(used + 2 <= 800);

    values[0] = 0; // A valid zero must remain valid.
    assert(build(2, &device));
    assert(strstr(response, "\"valid_mask\":3") != nullptr);
    sample.validMask = 2;
    values[0] = 123;
    assert(build(2, &device));
    assert(strstr(response, "\"values\":[0.00,18.00]") != nullptr);
    assert(strstr(response, "\"valid_mask\":2") != nullptr);

    sample.seq = 0;
    sample.ageMs = -1;
    sample.validMask = 3;
    assert(build(2, &device));
    assert(strstr(response, "\"values\":[0.00,0.00]") != nullptr);
    sample.seq = 1;
    sample.ageMs = 3000;
    assert(build(0, &device));
    assert(strstr(response, "\"params\":[],\"values\":[]") != nullptr);
    assert(strstr(response, "\"seq\":1") != nullptr);
    assert(strstr(response, "\"valid_mask\":0") != nullptr);

    values[0] = NAN;
    assert(build(2, &device));
    assert(strstr(response, "nan") == nullptr);
    assert(strstr(response, "\"valid_mask\":2") != nullptr);
    values[0] = INFINITY;
    assert(build(2, &device));
    assert(strstr(response, "inf") == nullptr);

    device.sd = 3;
    device.hasSdError = true;
    device.sdError = -1;
    assert(build(2, &device));
    assert(strstr(response, "\"sd_error\":-1") != nullptr);
    device.sd = 2;
    assert(build(2, &device));
    assert(strstr(response, "sd_error") == nullptr);

    sample.seq = UINT32_MAX;
    sample.ageMs = UINT32_MAX;
    sample.validMask = UINT16_MAX;
    for (unsigned i = 0; i < 16; ++i) {
        values[i] = 1000000000.0f;
        decimals[i] = 3;
    }
    assert(build(16, &device)); // Encoder position boundary, not a valid selection.
    assert(used + 2 <= 800);
    assert(!build(17, &device));
    assert(!build(2, &device, 64));
    for (float& value : values) value = -FLT_MAX;
    assert(!build(14, &device)); // Bounded error, never transmit partial JSON.
    return 0;
}
