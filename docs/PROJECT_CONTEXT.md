# Project Context

This document describes the long-lived technical context of the active firmware.
Current source and build scripts take precedence over archived documents.
`docs/archive/` may provide historical background but is not an authority for
the current implementation.

## Project Overview

TSP-ESP32-S3 is ESP32-S3 firmware for an air-quality micro-station. It collects
particulate, gas, meteorological, noise, and related sensor data; calculates
real-time and statistical records; stores records on SD; exposes local control
interfaces; and uploads environmental data through HJ212.

Arduino starts the system in `TSP.ino`. `setup()` delegates initialization to
the `System` singleton, while `loop()` remains intentionally idle apart from
a delay. Runtime work is performed by FreeRTOS tasks and event queues.

## Current Firmware Variants

| Variant | Canonical source | Version | Build inputs | Source fingerprint |
| --- | --- | ---: | ---: | --- |
| standard | `firmware/standard/TSP` | 2.0.23 | 88 | `A97AA91FD298EAA31D3ED2051C1A19FAEB210A98284533267508A947D644C3E9` |
| certified | `firmware/certified/TSP` | 2.0.23.1 | 89 | `090B6AD564ED66941D9DA60EF1ABB1626113E5837A9A77C736A23CAC4A3CAA6B` |

The certified variant adds `GasSpecificPolicy.h` and applies a 500 ppb upper
cap to O3, NO2, and SO2. CO is not capped. Its certified calibration profile is
`certified_250ppb_v1`, with targets O3 250 ppb, NO2 250 ppb, CO 5000 ppb,
and SO2 250 ppb. Common changes must be evaluated for both variants; this
certified policy must not leak into standard.

Both variants passed repository-migration build verification with only expected
nondeterministic build metadata differences. This status is not hardware
verification.

## Repository Layout

- `firmware/standard/TSP/`: current standard firmware source.
- `firmware/certified/TSP/`: current certified firmware source.
- `firmware_workspace/scripts/`: build, status, packaging, validation, and
  flashing implementation.
- `firmware_workspace/build/`: reproducible local build output; ignored by Git.
- `firmware_workspace/releases/`: Git-tracked release metadata. Firmware BINs
  are obtained from GitHub Releases or the controlled offline archive.
- `docs/`: current technical documentation and explicitly separated history.
- `docs/archive/`: frozen historical material; not current operating guidance.
- `tools/flashing/`: retained vendor flashing documentation and defaults.

## Build and Validation Workflow

Preferred entry points:

- `firmware_workspace/build-standard.cmd`
- `firmware_workspace/build-certified.cmd`
- `firmware_workspace/build-current.cmd` (standard compatibility entry)
- `firmware_workspace/status-standard.cmd`
- `firmware_workspace/status-certified.cmd`
- `firmware_workspace/status.cmd` (standard default)

`Build-Firmware.ps1` maps the requested variant to its canonical source. Before
building, it checks that all three firmware version definitions agree and records
the source input list and fingerprint. It also verifies that the source did not
change during compilation.

The validated toolchain uses Arduino CLI with ESP32 Core 3.3.7 and the ESP32-S3
16 MB `app3M_fat9M_16MB` partition scheme. The release flash layout is:

| Offset | Image |
| ---: | --- |
| `0x000000` | `bootloader.bin` |
| `0x008000` | `partitions.bin` |
| `0x00E000` | `boot_app0.bin` |
| `0x010000` | `firmware.bin` |

Builds are not byte-reproducible solely from identical project source because
ESP32 Core 3.3.7 embeds `__DATE__` and `__TIME__` in
`chip-debug-report.cpp`. These values propagate into the ELF hash and image
checksum. Compare source fingerprints, layout, and classified byte differences;
do not alter source or the toolchain to force identical hashes.

## Runtime Architecture

`SystemInit` establishes logging, synchronization, serial ports, FFat/SD
storage, configuration, calibration, DTU/time support, and FreeRTOS tasks.
The runtime uses:

- queue-based event publication for data and command flows;
- reference-counted data packets shared across subscribers;
- per-interface serial mutexes;
- task notifications and atomics for operational coordination;
- `vTaskDelay`, `vTaskDelayUntil`, `millis()`, and wall-clock scheduling.

No FreeRTOS software timer is used in the current source.

## Main Modules

- `system`: startup and task creation.
- `collectorManager` and sensor collectors: polling and raw measurements.
- `dataManager`: real-time output and minute/hour/day statistics.
- `eventBus`: queued event distribution and packet lifetime management.
- `file_storage`: FFat configuration, SD records, and HJ212 pending packets.
- `pack212` and `dtuManager`: HJ212 construction, sending, ACK handling,
  recovery, and link maintenance.
- `configManager` and `setupManager`: persistent configuration and runtime
  setup.
- `permissionManager`: LCD and remote-DTU request routing.
- `GasCalibrationManager`: gas calibration workflow and certified policy.
- `remote_ota_manager`: remote application OTA receiving and activation.
- `LogManager` and runtime diagnostics: serialized logs and health metrics.

## FreeRTOS Tasks

The active task set is configuration-dependent:

| Task | Role |
| --- | --- |
| Collect | schedules sensor collection |
| DataProcess | converts raw events into real-time/statistical data |
| Storage | saves real-time, minute, hour, and day records |
| HJ212 2017 or 2025 | serializes live uploads and pending recovery |
| SerialControl | handles LCD and remote-DTU input |
| Maintenance | polls CSQ, synchronizes time, and schedules pending recovery |
| Config | applies configuration requests |
| Calibration | advances gas calibration |
| OTA listener/worker | receives and executes remote OTA requests |
| LED | updates displayed measurements |
| Alarm | evaluates alarm rules when enabled |
| Temperature control | executes enclosure temperature control when enabled |
| MQTT | conditionally created; current loop is a placeholder delay |

DataProcess and HJ212 run on core 0; collection and serial-control work primarily
run on core 1. OTA temporarily raises its task priority during an active transfer.
Exact stack sizes and priorities should be read from current `system.cpp` before
changing scheduling behavior.

## Queues / Semaphores / Timers

Long-lived queue depths in the current source include:

- DataManager 10
- Config 10
- gas calibration 10
- remote DTU command 5
- HJ212 20
- Storage 20
- LED 5
- Alarm task 5
- OTA 5

`permissionManager` also creates short-lived response queues of depth 1.
The EventBus owns a mutex and one queue per subscriber. Processed-data events
use blocking delivery to preserve order and backpressure; other overflowing
event queues discard stale entries and emit an `EVENT_DROP` diagnostic.

Each serial interface has a mutex. Additional synchronization protects SD,
configuration/setup/system state, data statistics, calibration state, log
output, and Modbus request/response operation. Scheduling is delay/time based;
there is no current FreeRTOS software-timer layer.

## Sensor Data Flow

1. `configManager` loads the sensor map and catalog from FFat.
2. `collectorManager` polls the configured collector set.
3. A reference-counted `AllDataPacket` is published as
   `RAW_DATA_COLLECTED`.
4. `dataManager` emits real-time processed data immediately and updates
   statistical windows.
5. Processed subscribers independently feed storage, HJ212, LED, and alarms.

The pump is enabled for the first third of a collection interval. Gas Modbus
transactions enforce at least 200 ms between the end of one gas-module request
and the start of the next, including calibration and retry paths.

Gas status mapping currently treats not-ready or sensor-fault conditions as
`D/SENSOR_FAULT`, high-concentration or over-range conditions as
`T/OVER_RANGE`, and otherwise reports `N`. During gas calibration, gas data
uses calibration status and is excluded from local statistics.

Statistics close an hour before accepting the first sample of the next hour,
and the day is finalized after the final hour. Only every third minute is used
in hour statistics. This hour-statistics behavior is a frozen compatibility
constraint.

## Storage Flow

FFat stores configuration. Missing configuration files are seeded from embedded
defaults. SDMMC uses 1-bit mode with CLK GPIO 47, CMD GPIO 48, and D0 GPIO 21;
automatic format-on-mount-failure is disabled.

Measurement records use:

- `/sdcard/YYYYMMDD/raw/HH/MM.dat`
- `/sdcard/YYYYMMDD/min/HH/MM.dat`
- `/sdcard/YYYYMMDD/hour/HH.dat`
- `/sdcard/YYYYMMDD/day/day.dat`

SD operations are serialized by a mutex. HJ212 pending packets are stored under
`/sdcard/pending/<type>/<timestamp>.pkt`. In 2.0.23/2.0.23.1, the first write
uses a `.tmp1` stdio path and reopens it for CRC and byte-level verification.
If that fails, `.tmp1` remains allocated while an independent `.tmp2` POSIX
path writes in 256-byte chunks and is reopened for the same verification. Only
a verified temporary file is renamed to the final `.pkt`. If both attempts
fail, the existing rebuild marker and raw-record reconstruction remain the
fallback. Recovery scans are bounded and yield to live data.

## HJ212 Upload Flow

The configured HJ212 task subscribes to processed and resume events through a
queue of depth 20. Live packets always take priority. Pending recovery is
attempted only after the live queue is empty and is rechecked before serial use.

`dtuManager` is the sole owner of HJ212 serial arbitration. It records the
current serial owner/source for diagnostics. Pending traffic waits briefly and
defers when live traffic is active or waiting.

Important compatibility settings:

- minimum interval between packets: 3000 ms;
- default acknowledgement timeout: 5000 ms;
- default retries: 3;
- acknowledgement modes: standard, compatible, and legacy;
- acknowledgement checks include command, framing/CRC, QN/MN, and response
  result as required by the selected mode.

QN values are generated uniquely. Split-packet flags are rejected. CSQ polling
and time synchronization defer around active uploads. Both HJ212-2017 and
HJ212-2025 implementations exist; the deployment status of the 2025 path needs
verification.

## Network / DTU / MQTT Related Flow

Remote control DTU traffic and HJ212 upload traffic use separate serial
interfaces. LCD and remote-DTU parsers convert JSON requests into Permission
events for live data, configuration, records, calibration, upload, and DTU
commands.

Remote OTA accepts the current protocol and a legacy protocol. It checks the
target partition size, pauses conflicting business activity, acquires the DTU
serial interface, streams the image through ESP-IDF OTA APIs, finalizes the
image, selects the boot partition, and restarts. The current application-level
protocol does not visibly require a caller-supplied image SHA-256 or signature.
Whether deployed devices enforce secure boot, signed images, and authenticated
transport needs verification.

The MQTT task can be enabled by configuration, but its current implementation
only delays in a loop. Whether MQTT publishing is intentionally dormant needs
verification.

## Configuration System

Configuration is stored on FFat:

- `/config.json`
- `/model.json`
- `/sensorCatalog.json`
- `/system.json`
- `/switch.json`
- `/tempControl.json`
- `/hj212.json`
- `/alarm.json`

Embedded headers seed files that do not yet exist. Runtime requests are routed
through the EventBus, and sensor configuration supports selection, subsets, and
merging.

The firmware version is currently repeated in:

- `TSP.ino`
- `src/inc/sys_init.h`
- `src/app/configManager/system_json.h`

The build script requires agreement among all three. This is intentionally not
yet refactored into a single version source.

## Logging / Diagnostics

DEBUG logging is enabled. `LogManager` serializes output. Important diagnostics
cover:

- collection, processing, storage, and HJ212 trace IDs;
- HJ212 TX/ACK/retry and serial ownership;
- queue pressure, event drops, and UART overflow;
- pending write, corruption, rebuild, and recovery;
- gas request pacing;
- time synchronization and OTA memory;
- current/minimum free heap and largest contiguous allocation.

SHT30 remains independent from the gas polling path.

## Release Model

Git stores release metadata, including manifests, hashes, source input lists,
partition data, and flash addresses. Current formal BINs are distributed through
GitHub Releases. Historical BIN packages are kept in a controlled offline
archive and are not part of the daily Git worktree.

Current published packages:

- standard: tag `fw-2.0.22-20260907`
- certified: tag `fw-2.0.22.1-20260907-certified`

Both packages remain `packaged-not-hardware-verified`. A Git tag identifies
the canonical repository state after migration; binary provenance is defined by
the committed release metadata and recorded source fingerprint.

The 2.0.23 standard and 2.0.23.1 certified pending-write-hardening builds have
compiled successfully but are not yet packaged or hardware verified.

## Important Constraints

- Do not change the hour-statistics algorithm.
- Keep HJ212 packet spacing at 3000 ms.
- Keep the default HJ212 ACK timeout at 5000 ms and retry count at 3 unless
  explicitly requested.
- Keep SHT30 independent.
- Keep DEBUG enabled.
- Do not globally rename internal `w34011` or `L90` identifiers.
- Maintain standard and certified as separate variants.
- Synchronize public fixes while keeping certified gas caps certified-only.
- Do not flash, erase, or modify device FFat without explicit authorization.
- Do not increment versions without an explicit request.

## Where To Look First

- Project entry: [README](../README.md)
- Firmware workflow: [FIRMWARE_START_HERE](../FIRMWARE_START_HERE.md)
- Version locations: [FIRMWARE_VERSION_LOCATIONS](../FIRMWARE_VERSION_LOCATIONS.md)
- Workspace operations: [firmware_workspace/README](../firmware_workspace/README.md)
- Build implementation:
  [Build-Firmware.ps1](../firmware_workspace/scripts/Build-Firmware.ps1)
- Runtime initialization:
  [standard system.cpp](../firmware/standard/TSP/src/system/system/system.cpp)
- Event distribution:
  [standard eventBus.cpp](../firmware/standard/TSP/src/system/event/eventBus.cpp)
- Data processing:
  [standard dataManager.cpp](../firmware/standard/TSP/src/app/dataManager/dataManager.cpp)
- HJ212 packet construction:
  [standard pack212 module](../firmware/standard/TSP/src/module/pack212)
- HJ212 transport:
  [standard dtuManager.cpp](../firmware/standard/TSP/src/app/dtuManager/dtuManager.cpp)
- Storage implementation:
  [standard file_storage.cpp](../firmware/standard/TSP/src/module/file/file_storage.cpp)
- Certified-only gas policy:
  [GasSpecificPolicy.h](../firmware/certified/TSP/src/module/gas/GasSpecificPolicy.h)
- Outstanding work: [OPEN_ISSUES](OPEN_ISSUES.md)
- AI-relevant history: [AI_CHANGELOG](AI_CHANGELOG.md)

When a statement here conflicts with current code, trust the current code and
update this document after verification.
