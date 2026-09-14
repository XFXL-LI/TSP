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

The current 2.0.22 standard and 2.0.22.1 certified releases are packaged and
file-level verified, but neither is recorded as hardware verified.

Evidence:

Both committed release manifests use `packaged-not-hardware-verified`.

Risk:

Static validation cannot prove SD behavior, UART timing, sensors, HJ212
acknowledgements, OTA recovery, or long-running memory stability on target
hardware.

Suggested next action:

Execute and record a controlled hardware qualification matrix for both variants
without changing the release status until all required checks pass.

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

On the 2026-09-14 baseline host, `status-standard.cmd` and
`status-certified.cmd` launch Windows PowerShell and fail because
`Get-FileHash` is not resolved. Running `Get-FirmwareStatus.ps1` directly
under PowerShell 7 succeeds.

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
