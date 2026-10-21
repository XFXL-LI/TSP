# Open Issues

This file tracks unresolved matters with lasting engineering value. It does not
record completed repository-cleanup work. Evidence is based on the canonical
firmware and current workspace scripts unless explicitly marked otherwise.

## Firmware version has three sources

Status: Confirmed

Priority: Medium

Area: Build and configuration

Description:

Each variant defines its version in `TSP.ino`, `src/inc/sys_init.h`, and
`src/app/configManager/system_json.h`.

Evidence:

`Build-Firmware.ps1` reads all three values and stops when they differ.

Risk:

A manual release edit can leave runtime reporting, JSON defaults, and build
identity inconsistent.

Suggested next action:

Design a single-source version mechanism while preserving the current build
gate. Do not perform this refactor during unrelated firmware work.

## Standard and certified variants can drift

Status: Confirmed

Priority: High

Area: Variant maintenance

Description:

The variants are independent source trees. The certified tree contains one
additional `GasSpecificPolicy.h` and several intentional certified gas and
calibration differences, while common fixes must be kept synchronized.

Evidence:

Current source comparison shows eight byte-different shared paths: seven contain
content or semantic differences and `dtuManager.h` differs only by line
endings. One header is certified-only. The certified policy caps O3/NO2/SO2 at
500 ppb, does not cap CO, and uses the `certified_250ppb_v1` profile.

Risk:

A common reliability fix may reach only one variant, or certified-only behavior
may accidentally enter standard.

Suggested next action:

Add a maintained variant-difference check that permits only documented
differences and flags unexpected drift.

## Current packages still require hardware verification

Status: Confirmed

Priority: High

Area: Release qualification

Description:

The latest published 2.0.22 standard and 2.0.22.1 certified releases are
packaged and file-level verified, but neither is recorded as hardware verified.
The current 2.1.5 and 2.1.5.1 diagnostic source builds are compiled but not yet packaged
or hardware verified.

Evidence:

Both committed release manifests use `packaged-not-hardware-verified`.

Risk:

Static validation cannot prove SD behavior, UART timing, sensors, HJ212
acknowledgements, OTA recovery, or long-running memory stability on target
hardware.

Suggested next action:

Execute and record a controlled hardware qualification matrix for both variants
without changing the release status until all required checks pass.

## Staged 120-second collection needs hardware timing verification

Status: Needs verification

Priority: High

Area: Sensors, pump, storage, and HJ212

Description:

Versions 2.1.0 and 2.1.0.1 replace the former one-pass collection with a
120-second batch: pump start at second 0, non-air sensors from second 45,
particulate and gas reads from second 60, and an independent pump cutoff at
second 70. The particulate sensor is queried once for all four channels.

Evidence:

Both variants compile successfully. A certified 2.1.0.1 target-device run of
about 18.5 hours covered 557 batches: the 120-second cadence remained stable,
all particulate batch reads completed within the deadline, and no material
batch drift was observed. The log did not include explicit physical
`PUMP_ON/PUMP_OFF` edges, and standard plus partial-factor configurations have
not completed the same qualification.

Risk:

The physical 70-second pump high time is not yet directly proven by logs. A
configuration with only some particulate factors also needs confirmation that
one physical four-channel read emits only the selected logical factors.

Suggested next action:

Add minimal pump-edge diagnostics and confirm the physical 70-second high time.
Complete standard and partial-factor tests covering partial PM selection,
gas-only, non-air-only, repeated module timeouts, network loss and recovery,
SD records, HJ212 timestamps, and 10-minute/hour boundaries before packaging
either version.

## Pending SD write hardening needs a long-run hardware test

Status: Needs verification

Priority: High

Area: Storage and HJ212 recovery

Description:

Six observed pending-write incidents produced correctly sized files whose first
512 bytes read back as zero while the in-memory HJ212 packet remained valid.
Versions 2.1.0 and 2.1.0.1 keep the failed `.tmp1` allocation in place and
use an independent `.tmp2` fallback with 256-byte POSIX writes, but this change
still needs broader long-run qualification.

Evidence:

The incident logs consistently reported `mismatch_offset=0`, differing memory
and file CRC values, and a zero-filled first sector. In the latest certified
long run, the stdio `.tmp1` path reproduced that failure, the independent POSIX
`.tmp2` path verified successfully, and the resulting pending packet was later
ACKed, delivered, and deleted. This proves the fallback chain for one observed
failure but does not explain or eliminate the underlying first-write defect.

Risk:

The primary stdio write can still silently produce a zero-filled first sector.
Only one observed fallback activation has completed end to end, so other cards,
devices, repeated incidents, and the double-failure rebuild path remain
unqualified.

Suggested next action:

Continue controlled long-duration runs that force pending writes and retries on
more than one device/card. Confirm that normal writes remain quiet, `.tmp2` is
used only after a verified `.tmp1` failure, final `.pkt` files pass CRC/byte
comparison, and any double failure still rebuilds and retransmits from raw data.

## Builds contain nondeterministic toolchain metadata

Status: Confirmed

Priority: Medium

Area: Reproducible builds

Description:

Rebuilding unchanged source produces a different application SHA-256 because
ESP32 Core 3.3.7 embeds compilation date/time metadata.

Evidence:

Migration-build comparison localized the payload differences to
`chip-debug-report.cpp` `__DATE__`/`__TIME__`, the resulting ELF SHA, and
image checksum/validation fields; source fingerprints and code layout matched.

Risk:

A simple BIN hash comparison cannot distinguish expected build metadata from a
real code or configuration change.

Suggested next action:

Document or automate classified binary comparison. Do not modify firmware
source, ESP32 Core, or compilation timestamps merely to force bit identity.

## Release restoration and default validation entry are misaligned

Status: Confirmed

Priority: Medium

Area: Release tooling

Description:

Git now stores release metadata without BIN files. `Flash-Firmware.ps1` still
defaults to a historical 2.0.4 local release directory, and
`validate-release.cmd` invokes that default without an explicit release path.
`Package-Release.ps1` also retains a historical default version of 2.0.6.

Evidence:

The current scripts contain those defaults, while the root `.gitignore`
externalizes `firmware_workspace/releases/**/*.bin`.

Risk:

An operator may validate the wrong release or receive a missing-BIN failure
after a clean clone.

Suggested next action:

Design an explicit variant/tag restoration-and-validation command that downloads
or imports verified BINs and never relies on a stale default version.

## Status CMD host compatibility needs verification

Status: Confirmed

Priority: Medium

Area: Development tooling

Description:

On the baseline host, `status-standard.cmd` and
`status-certified.cmd` launch Windows PowerShell and fail because
`Get-FileHash` is not resolved. Running `Get-FirmwareStatus.ps1` directly
under PowerShell 7 succeeds.
The same command-resolution failure also reproduced for the two build CMD
entries in the 2026-10-08 upgrade; the unchanged official variant build scripts
succeeded under PowerShell 7. Fixing the launcher remains separately scoped.

Evidence:

Both CMD entries reproduced the command-resolution failure. Direct PowerShell 7
execution reported the expected versions, input counts, and source fingerprints.

Risk:

The documented status entry can fail depending on the installed PowerShell host
even though the underlying status implementation is valid.

Suggested next action:

Reproduce on the supported developer machines and choose an explicit supported
PowerShell host or implement a compatible hashing fallback in a separately
scoped tooling change.

## MQTT task has no publishing implementation

Status: Confirmed

Priority: Medium

Area: Network and telemetry

Description:

The MQTT task is conditionally created, but the current task body only delays in
a loop.

Evidence:

`MqttPublicTask` in the current `system.cpp` contains no publish path.

Risk:

Enabling MQTT may consume a task without providing the expected feature, or the
flag may be a deliberately dormant compatibility option.

Suggested next action:

Confirm the product requirement and deployed configuration. Either implement
and test the feature or explicitly document/remove the dormant switch in a
separately scoped change.

## Remote OTA trust model is not explicit in application code

Status: Confirmed

Priority: High

Area: OTA security

Description:

The remote OTA path performs partition-size checks and ESP image finalization,
but the application protocol does not visibly require an expected SHA-256 or
application-level signature.

Evidence:

`remote_ota_manager` streams data through ESP-IDF OTA APIs and relies on
`esp_ota_end` before selecting the boot partition.

Risk:

Integrity and authenticity may depend entirely on deployment controls, secure
boot settings, or transport trust that are not documented in the active tree.

Suggested next action:

Verify production secure-boot, flash-encryption, sender authentication, and
transport assumptions. Then document the trust boundary and add cryptographic
verification if the deployment does not already enforce it.

## Embedded deployment defaults require a credential review

Status: Confirmed

Priority: High

Area: Configuration security

Description:

Current embedded configuration contains production-looking endpoint and
identity defaults, and the permission module contains fallback user credentials.

Evidence:

The values are present in current configuration headers and permission source.
Their actual deployment role has not been established.

Risk:

Real credentials could be exposed in source or identical fallback credentials
could be deployed across devices.

Suggested next action:

Classify every embedded value as template, test, or production secret; rotate
real secrets where necessary and establish a provisioning mechanism. Avoid
copying credential values into documentation or logs.

## HJ212-2025 field readiness is not established

Status: Needs verification

Priority: Medium

Area: HJ212

Description:

The firmware contains separate HJ212-2017 and HJ212-2025 task paths and selects
one by configuration, but the field qualification status of the 2025 path is
not documented.

Evidence:

Both task paths exist in current source; the default configuration remains on
the established path.

Risk:

Selecting the 2025 mode may expose unverified packet, acknowledgement, or
platform-compatibility behavior.

Suggested next action:

Run protocol-vector tests and a server interoperability test for the 2025 mode,
including ACK compatibility, retries, QN uniqueness, live priority, and pending
recovery.

## Processed-data backpressure can block the EventBus

Status: Needs verification

Priority: Medium

Area: Concurrency

Description:

Processed-data publication uses a blocking queue send to preserve ordering while
the EventBus synchronization path is active.

Evidence:

The current EventBus uses `portMAX_DELAY` for processed-data subscribers;
other event types use overflow handling and `EVENT_DROP` diagnostics.

Risk:

A stalled processed-data subscriber could delay collection or other EventBus
activity. The practical impact depends on task priorities, queue drain rates,
and failure modes.

Suggested next action:

Measure queue high-water marks and deliberately stall each subscriber on
hardware before changing semantics. Preserve data-order guarantees in any fix.

## Legacy storage write helper has ambiguous success reporting

Status: Needs verification

Priority: Low

Area: Storage

Description:

`file_storage::writeSDCard` evaluates a byte-count comparison without using its
result and returns success after `fopen` regardless of that comparison.

Evidence:

The current implementation contains the discarded
`written == content.length()` expression. No active call site was found in the
current source scan.

Risk:

If the dormant API is reused, partial writes may be reported as successful.

Suggested next action:

Confirm that the helper is truly unused. Remove it or correct and test its
return contract in a dedicated storage change.

## Offline local snapshot and status interface needs hardware qualification

Status: Implemented; needs verification

Priority: High

Area: RTC, local display, storage, and remote interface

Description:

The former collector timestamp gate and fixed 2026-05-27 processing cutoff
prevented LCD cache updates when a new board had neither valid RTC time nor
network synchronization. Versions 2.1.1/2.1.1.1 separate local snapshots from
dated storage/statistics/HJ212 and add opt-in `get_data/with_status:true` health
metadata, without changing experimental firmware.

Evidence:

Both canonical variants compile successfully. Date, sequence and monotonic-age
compile-time regression assertions pass. The response encoder checks its
1024-byte buffer and 800-byte extended wire limit. Native host response-encoder
tests now pass on both variants, but the physical LCD/DTU/SD/time-recovery
cases are not yet qualified.

Risk:

Legacy LCD logic may still reject timestamp 0; it must implement the documented
status-aware display and history guards. SD mount/write status is not a
continuous card-presence test or proof of read-back correctness. Compilation
does not prove UART receive capacity, missing-LCD operation or boot-time memory.

Suggested next action:

Run the status protocol acceptance matrix on both variants: invalid/valid RTC
without network, failed SD mount/write, true zero/invalid factors, first batch,
time recovery without backdating, 14-factor replies, legacy clients and OTA.
Do not close this issue, pending first-sector corruption, or pump timing based
only on source/build validation.

## LED compatibility controls and heating master require hardware qualification

Status: Implemented; needs verification

Priority: High

Area: LCD/remote configuration, LED control card, and heating output

Description:

Versions 2.1.3/2.1.3.1 repurpose the legacy `enable_remote_dtu` and
`save_raw_data` wire names as LED enable and four-region mode. Internally the
LED has separate canonical persisted/runtime state. Heating accepts the
canonical LCD Boolean field and the old alias, and switch-off immediately
forces LOW without disabling SHT30 sampling or the fan. The LED uses continuous
five-second compact pages rather than retaining one processed packet through
the former rotation window.

Evidence:

The user reports region 41 cycling while all other regions remain blank with
`LED_CACHE items=14 bytes=336`. The first 2.1.3-series implementation issued
same-page region writes back-to-back, without a quiet interval. On 2026-10-09,
both variants were changed to one frame per poll and a minimum 50 ms gap after
UART completion, including clears. This is the leading software explanation,
not a hardware-confirmed root cause; repeated field verification is still pending.

Both updated variants compile and pass 17 current-source control test groups,
including transmission latency, mid-burst controls and timer wrap. ELF static
RAM is 28024 bytes: 80 more than the first 2.1.3-series builds and 424 more than
the 2.1.2-series baseline. The existing
thermal 4 KB task is now always present, so configurations which previously
omitted it must also budget its stack/RTOS allocation. Host tests use actual
configuration helpers, LED encoding/cache, heating implementation, and EventBus
publishing with mocked peripherals; they do not establish electrical behavior.

Risk:

The LED card may not clear on a one-space update, and its autonomous template
timing/receive capacity must be checked with the new spaced region writes.
UART write success has no card ACK. Legacy clients retaining old widget values
can unintentionally select a mode on their first save; they should query first.
Heating actuation under concurrent activity and long-run heap/task-stack margin
still require target-device evidence. Do not close pre-existing SD/gas issues
based on this control upgrade.

Suggested next action:

Use the `led-gap-20261009` builds. Check `LED_CONFIG` region mode and
`LED_TX` entries for all active regions; confirm at least 50 ms from the prior
frame's end to the next start and observe actual screen content/clearing.
Then complete the [switch protocol matrix](protocols/lcd/LCD_SWITCH_CONFIG_PROTOCOL.md)
on standard and certified: LCD/remote get/set/reboot, 2/4 modes, partial factors,
empty/invalid data, LED disable, heating off during active heating, re-enable,
SHT30/fan independence, SD/network activity and long running stability. Keep
minute/hour/day switch business semantics for a separately authorized change.

## Source scan found no TODO or FIXME markers

Status: Informational

Priority: Low

Area: Maintenance

Description:

No `TODO`, `FIXME`, `HACK`, or `XXX` markers were found in the canonical
firmware or workspace scripts during the 2026-09-14 baseline scan.

Evidence:

Repository text search returned zero matches in those active paths.

Risk:

Absence of markers does not imply absence of defects; outstanding work must be
tracked explicitly here.

Suggested next action:

Continue recording confirmed long-lived work in this file instead of scattering
temporary markers or task-summary documents.

## HJ212 software-UART integrity and bounded SD decoupling need field validation

Status: Mitigation insufficient; field test still loses realtime frame prefixes

Priority: High

Area: HJ212 transport, FreeRTOS scheduling, and storage overload

The 2026-10-19/20 JCKJ123 Flag=4 test has 523 live TX batches in the aligned
window and 399 unique valid server frames (124 missing). All missing batches
have MCU collect/store/TX-success logs; many are visible as truncated or garbled
raw TCP payloads. Storage activity overlaps 123 of the 124 missing TX windows.
This strongly suggests software-UART task preemption, but timing correlation is
not a waveform-level root-cause proof. Flag=4 cannot detect remote corruption.

Versions 2.1.4/2.1.4.1 boost only synchronous HJ UART writes to priority 11,
then restore the original base priority with interrupts enabled. Failed live
packet writes move to the existing storage task (two jobs/2048 packet bytes,
one retry after 60 seconds). The four-entry storage event queue never waits
on EventBus delivery; overflow releases the rejected owned reference and logs
loss explicitly. Queued is not durable, and SD failure can still lose history
or pending protection. Existing pending scans/reads/deletes remain synchronous;
a blocked SDK call cannot be cancelled by the outer retry policy.

Both variants pass current-source host tests including storage saturation,
priority restoration/ACK separation, slow/unmounted/double-failing write mocks,
byte/job limits, retry wrap, ozone packing and cached-frame migration. The host
model does not prove FreeRTOS GPIO waveforms, SD electrical behavior, or IRQ
latency. The first-sector-zero issue remains open independently.

The 2026-10-20 certified 2.1.4.1 field test still has 32 missing valid realtime
frames among 122 aligned TX batches (90 received), while all 24 minute frames
arrived. Raw TCP confirms 23 timestamp-identifiable truncated prefixes; six
other expected slots contain only 0xFF, and three have no identifiable live
payload. No complete missing frame was recovered by source-IP reassembly.
Priority boosting is therefore not an established fix. The 2.1.4 priority_tx
log prints the target rather than a measured effective priority.

Versions 2.1.5/2.1.5.1 add the approved per-boot 30/60/30-minute SD comparison,
with a draining phase before the full quiet hour, monotonic operation windows,
measured priority and bounded skip/release behavior. The intentional pause
loses local history/pending durability and returns NG for historical queries;
this is diagnostic behavior, not a fix.

The 2026-10-20/21 certified 2.1.5.1 field comparison received 4/15 valid live
frames before the pause, 30/30 during the full quiet hour, and 11/15 after
resumption. All 15 unmatched live batches overlap measured SD operation
windows and report effective UART priority 9 instead of the intended 11.
All 44 priority-11 batches in this two-hour comparison arrived. Existing old
pending frames are excluded from those live counts. This strongly narrows the
issue to SD concurrency and ineffective scheduling protection, without proving
that every transport failure has the same cause. The longer overnight window
also has two unmatched batches reporting priority 11.

The current host priority double updates effective priority as max(new base,
inherited), unlike ESP-IDF 5.5.2 when inheritance is already active, and the
mock stream checks base rather than effective priority. Passing those tests
does not qualify the current guard. Correct the model/assertions and review
priority changes with serial/log mutex ownership before the next firmware fix.
A blocked SDK call cannot be cancelled and must not be labelled off_b until it
actually returns. Confirm the expected SD on/off/on markers and zero admitted
operations in off_b before comparing server-valid realtime counts.

Validate Flag=4 with one destination at 9600: first two hours, then 24 hours
including midnight, matching each MCU batch to raw TCP and CRC-valid frames.
Include storage-failure/slow-card cases and check new HJ_UART_TX and
PENDING_STORE diagnostics. Keep 120-second batches and the 70-second pump
schedule. Multi-channel configuration and baud-rate changes are deferred.
