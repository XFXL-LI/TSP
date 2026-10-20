#include "GasModbusAccess.h"
#include "GasUnitConverter.h"
#include "SdMountSession.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
#include <string>

uint64_t testClockMs = 10000;

namespace {
struct Reply {
    uint16_t status;
    uint16_t raw;
    bool ok = true;
    uint32_t durationMs = 40;
};
std::vector<Reply> replies;
std::vector<uint64_t> requests;
size_t nextReply = 0;

void gasScenario(std::initializer_list<Reply> input) {
    testClockMs += 10000;
    replies = input;
    requests.clear();
    nextReply = 0;
}

void checkIntervals() {
    for (size_t i = 1; i < requests.size(); ++i) {
        assert(requests[i] - requests[i - 1] >=
               replies[i - 1].durationMs + 500);
    }
}

enum class Failure { None, Drive, Host, Slot, Card, Vfs, Fat };
struct SdState {
    Failure failure = Failure::None;
    int hostLive = 0, diskLive = 0, vfsLive = 0;
    int attempts = 0;
    int fullDeinits = 0;
    int probeCommands = 1;
    uint32_t probeTimeoutMs = 1000;
    uint32_t commandDurationMs = 30;
    uint32_t lastCommandTimeoutMs = 0;
    sdmmc_card_t *card = nullptr;
    FATFS *filesystem = nullptr;
    std::vector<std::string> events;
} sd;

void resetSd() {
    // Simulate a fresh device between successful-mount scenarios. Failure
    // scenarios instead reuse the same session without resetting resources.
    delete sd.filesystem;
    sd = {};
    testClockMs += 10000;
}

void assertReleased() {
    assert(sd.hostLive == 0 && sd.diskLive == 0 && sd.vfsLive == 0);
    assert(sd.filesystem == nullptr);
}

esp_err_t hostInit() {
    ++sd.attempts;
    if (sd.failure == Failure::Host) return ESP_ERR_NO_MEM;
    ++sd.hostLive;
    sd.events.push_back("host");
    return ESP_OK;
}

esp_err_t hostDeinit(int slot) {
    assert(slot == 1 && sd.hostLive == 1);
    --sd.hostLive;
    sd.events.push_back("host-release");
    return ESP_OK;
}
}

bool modbus_manager::readModbusRegs(uint8_t, uint16_t address, uint16_t count,
                                    uint16_t *values, uint8_t maxAttempts,
                                    uint32_t timeoutMs, uint32_t retryDelayMs) {
    assert(address == 0x6000 && count == 2);
    assert(maxAttempts == 1 && retryDelayMs == 0 && timeoutMs == 500);
    assert(nextReply < replies.size());
    requests.push_back(testClockMs);
    const Reply reply = replies[nextReply++];
    testClockMs += reply.durationMs;
    values[0] = reply.ok ? reply.status : 0;
    values[1] = reply.ok ? reply.raw : 0;
    return reply.ok;
}

bool modbus_manager::writeModbusRegs(uint8_t, uint16_t, uint16_t, uint16_t *,
                                     uint8_t, uint32_t, uint32_t) {
    testClockMs += 40;
    return true;
}

sdmmc_host_t mockDefaultHost() {
    return {1, hostInit, hostDeinit, sdmmc_host_do_transaction, nullptr};
}

esp_err_t sdmmc_host_do_transaction(int slot, sdmmc_command_t *command) {
    assert(slot == 1);
    sd.lastCommandTimeoutMs = command->timeout_ms;
    const uint32_t duration = sd.commandDurationMs;
    testClockMs += duration < command->timeout_ms ? duration : command->timeout_ms;
    return duration > command->timeout_ms ? ESP_ERR_TIMEOUT : ESP_OK;
}

esp_err_t sdmmc_host_init_slot(int slot, const sdmmc_slot_config_t *config) {
    assert(slot == 1 && config->width == 1);
    assert(config->clk == GPIO_NUM_47 && config->cmd == GPIO_NUM_48 &&
           config->d0 == GPIO_NUM_21);
    return sd.failure == Failure::Slot ? ESP_FAIL : ESP_OK;
}

esp_err_t sdmmc_host_deinit() {
    ++sd.fullDeinits;
    return hostDeinit(1);
}

esp_err_t sdmmc_card_init(const sdmmc_host_t *host, sdmmc_card_t *card) {
    card->host = *host;
    if (sd.failure == Failure::Card) return ESP_FAIL;
    for (int i = 0; i < sd.probeCommands; ++i) {
        sdmmc_command_t command{sd.probeTimeoutMs};
        const esp_err_t result = host->do_transaction(host->slot, &command);
        if (result != ESP_OK) return result;
    }
    return ESP_OK;
}

esp_err_t ff_diskio_get_drive(BYTE *drive) {
    if (sd.failure == Failure::Drive) return ESP_FAIL;
    assert(sd.diskLive == 0);
    *drive = 1; // Drive zero represents the untouched FFat filesystem.
    return ESP_OK;
}

void ff_diskio_register_sdmmc(BYTE drive, sdmmc_card_t *card) {
    assert(drive == 1 && sd.diskLive == 0);
    ++sd.diskLive;
    sd.card = card;
    sd.events.push_back("disk");
}

void ff_diskio_unregister(BYTE drive) {
    assert(drive == 1 && sd.diskLive == 1);
    --sd.diskLive;
    sd.card = nullptr;
    sd.events.push_back("disk-release");
}

void ff_sdmmc_set_disk_status_check(BYTE drive, bool enabled) {
    assert(drive == 1 && !enabled);
}

esp_err_t esp_vfs_fat_register(const char *path, const char *drive,
                               size_t maxFiles, FATFS **filesystem) {
    assert(std::strcmp(path, "/sdcard") == 0 && std::strcmp(drive, "1:") == 0);
    assert(maxFiles == 5 && sd.vfsLive == 0);
    if (sd.failure == Failure::Vfs) return ESP_ERR_NO_MEM;
    ++sd.vfsLive;
    sd.filesystem = new FATFS;
    *filesystem = sd.filesystem;
    sd.events.push_back("vfs");
    return ESP_OK;
}

esp_err_t esp_vfs_fat_unregister_path(const char *path) {
    assert(std::strcmp(path, "/sdcard") == 0 && sd.vfsLive == 1);
    --sd.vfsLive;
    delete sd.filesystem;
    sd.filesystem = nullptr;
    sd.events.push_back("vfs-release");
    return ESP_OK;
}

FRESULT f_mount(FATFS *filesystem, const char *drive, BYTE immediate) {
    assert(std::strcmp(drive, "1:") == 0);
    if (filesystem == nullptr) {
        assert(immediate == 0);
        sd.events.push_back("fat-release");
        return FR_OK;
    }
    assert(immediate == 1 && sd.card != nullptr);
    if (sd.failure == Failure::Fat) return FR_DISK_ERR;
    sdmmc_command_t command;
    return sd.card->host.do_transaction(1, &command) == ESP_OK ? FR_OK : FR_DISK_ERR;
}

int main() {
    modbus_manager manager;
    uint16_t values[2] = {};
    auto readGas = [&](ReadDeadline deadline = {}) {
        return GasModbusAccess::readStatusAndConcentration(
            manager, 6, values, 3, 500, "a21026", "test", deadline);
    };

    gasScenario({{0x8000, 8}});
    assert(readGas() && nextReply == 1 && values[1] == 8);
    gasScenario({{0, 0}, {0x8000, 9}});
    assert(readGas() && nextReply == 2 && values[1] == 9);
    checkIntervals();
    gasScenario({{0, 0}, {0, 1}, {0x8000, 10}});
    assert(readGas() && nextReply == 3 && values[0] == 0x8000 && values[1] == 10);
    checkIntervals();
    gasScenario({{0, 1}, {0, 2}, {0, 3}});
    assert(readGas() && nextReply == 3 && values[0] == 0 && values[1] == 3);
    assert(!GasUnitConverter::isMeasurementValid(values[0]));
    gasScenario({{0, 1}, {0, 2}, {0, 0, false, 500}});
    assert(readGas() && nextReply == 3 && values[0] == 0 && values[1] == 2);
    gasScenario({{0, 0}, {0, 0, false, 500}, {0x8000, 11}});
    assert(readGas() && nextReply == 3 && values[1] == 11);
    checkIntervals();
    gasScenario({{0, 0, false, 500}, {0, 0, false, 500},
                 {0, 0}, {0, 0}, {0x8000, 12}});
    assert(readGas() && nextReply == 5 && values[1] == 12);
    checkIntervals();
    gasScenario({{0, 0, false, 500}, {0, 0, false, 500}, {0, 0, false, 500}});
    assert(!readGas() && nextReply == 3);
    gasScenario({{0, 1}, {0, 2}});
    const uint64_t deadlineStart = testClockMs;
    assert(readGas(ReadDeadline::at(millis() + 1700)) && nextReply == 2);
    assert(testClockMs - deadlineStart < 1700 && values[0] == 0);
    gasScenario({});
    assert(!readGas(ReadDeadline::at(millis())) && nextReply == 0);
    gasScenario({{0, 0}, {0, 0}, {0x8020, 13}});
    assert(readGas() && nextReply == 3 && !GasUnitConverter::isMeasurementValid(values[0]));
    gasScenario({{0x8000, 14}});
    testClockMs = UINT32_MAX - 1000ULL;
    assert(readGas(ReadDeadline::at(0)) && nextReply == 1);

    assert(!SdMountPolicy::retryDue(false, false, 60000, 0));
    assert(!SdMountPolicy::retryDue(true, false, 59999, 0));
    assert(SdMountPolicy::retryDue(true, false, 60000, 0));
    assert(!SdMountPolicy::retryDue(true, true, 60000, 0));
    assert(SdMountPolicy::retryDue(true, false, 59949, UINT32_MAX - 50));

    for (Failure failure : {Failure::Drive, Failure::Host, Failure::Slot,
                             Failure::Card, Failure::Vfs, Failure::Fat}) {
        resetSd();
        SdMountSession session;
        sd.failure = failure;
        for (int i = 0; i < 100; ++i) {
            testClockMs += 60000;
            assert(session.mount("/sdcard", GPIO_NUM_47, GPIO_NUM_48, GPIO_NUM_21) != ESP_OK);
            assertReleased();
        }
        assert(sd.fullDeinits == (failure == Failure::Slot ? 100 : 0));
        sd.failure = Failure::None;
        assert(session.mount("/sdcard", GPIO_NUM_47, GPIO_NUM_48, GPIO_NUM_21) == ESP_OK);
        assert(sd.hostLive == 1 && sd.diskLive == 1 && sd.vfsLive == 1);
        const int attempts = sd.attempts;
        assert(session.mount("/sdcard", GPIO_NUM_47, GPIO_NUM_48, GPIO_NUM_21) == ESP_OK);
        assert(sd.attempts == attempts);
        sd.commandDurationMs = 2000;
        sdmmc_command_t command{5000};
        testClockMs += 10000;
        assert(sd.card->host.do_transaction(1, &command) == ESP_OK);
        assert(command.timeout_ms == 5000); // Operational I/O is not mount-limited.
    }

    resetSd();
    SdMountSession timedSession;
    sd.probeCommands = 20;
    sd.commandDurationMs = 400;
    const uint64_t mountStart = testClockMs;
    assert(timedSession.mount("/sdcard", GPIO_NUM_47, GPIO_NUM_48, GPIO_NUM_21) == ESP_ERR_TIMEOUT);
    assert(testClockMs - mountStart == SdMountPolicy::IO_BUDGET_MS);
    assert(sd.lastCommandTimeoutMs == 200); // Last command gets only the remainder.
    assertReleased();
    sd.probeCommands = 1;
    sd.commandDurationMs = 30;
    assert(timedSession.mount("/sdcard", GPIO_NUM_47, GPIO_NUM_48, GPIO_NUM_21) == ESP_OK);
    resetSd();
    SdMountSession commandLimitedSession;
    sd.probeTimeoutMs = 5000;
    sd.commandDurationMs = 1500;
    assert(commandLimitedSession.mount("/sdcard", GPIO_NUM_47, GPIO_NUM_48, GPIO_NUM_21) == ESP_ERR_TIMEOUT);
    assert(sd.lastCommandTimeoutMs == SdMountPolicy::COMMAND_TIMEOUT_MS);
    assertReleased();
    std::puts("PASS: gas retries/deadlines/wrap and SD cleanup/60s retry/budget (600 failed mounts)");
}
