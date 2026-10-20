# AI Changelog

This is a compact record of changes that materially affect how an AI assistant
should understand or navigate the project. It is not a substitute for Git
history or firmware release changelogs.

## 2026-09-14 — Repository canonicalization completed

- Established `firmware/standard/TSP` and `firmware/certified/TSP` as the
  canonical firmware sources.
- Retired the legacy root source tree from the active branch.
- Migrated build and status entry points to explicit standard/certified variants.
- Separated current documentation from frozen historical material under
  `docs/archive/`.
- Changed the release model so Git tracks provenance metadata while GitHub
  Releases carries current formal firmware binaries and an offline archive
  preserves historical BIN packages.
- Made `github` the canonical code remote; retained `origin` as a legacy
  JihuLab remote that must not be pushed without explicit authorization.
- Introduced `AGENTS.md`, `PROJECT_CONTEXT.md`, `OPEN_ISSUES.md`, and this
  file as the canonical long-term AI knowledge surface.

## 2026-09-16 — Firmware 2.1 staged collection baseline

- Advanced the canonical variants to standard 2.1.0 and certified 2.1.0.1.
- Fixed real-time batches and both configuration interval fields at 120 seconds.
- Defined the air path as pump start at second 0, particulate/gas reads from
  second 60 while the pump remains on, and an independent cutoff at second 70.
- Moved non-air sensor collection to the second-45 stage.
- Replaced four independent particulate requests with one function-03 read of
  eight registers from `0x0010`, while still emitting only enabled factors.
- Changed hour statistics from every-third-minute sampling to every valid
  real-time batch; invalid values remain excluded per factor.
- Preserved the certified-only gas cap and calibration policy as the sole
  product-specific gas behavior.

## 2026-09-28 — Offline snapshot and status baseline

- Advanced only canonical standard/certified to 2.1.1/2.1.1.1; experimental
  laboratory firmware remains independent and unchanged.
- Separated always-available local measurement snapshots from valid-date
  SD records, statistics and HJ212; no fake dates or backdating.
- Added opt-in `get_data/with_status:true` compact device status, per-batch
  sequence, monotonic age and request-order validity bits. Legacy requests
  retain their field structure; the extended wire limit is 800 bytes.
- Unified calendar validity across the dated data flow. No new task or LCD
  startup dependency; 120-second batches, 70-second pump and certified gas
  policy remain unchanged. Builds passed; hardware/LCD qualification is pending.

## 2026-10-08 — Runtime switch compatibility and continuous LED baseline

- Advanced only canonical standard/certified to 2.1.3/2.1.3.1 on explicit
  request, preserving the 2.1.2-series reliability behavior.
- Separated canonical LED persistence/runtime state from legacy LCD/remote
  `enable_remote_dtu` and `save_raw_data` wire aliases; both interfaces share
  LED-enable and four-region semantics, with safe first-upgrade defaults.
- Replaced the LED packet-held rotation window with a bounded short-text cache
  and continuous five-second compact 2/4-region paging using the existing task
  and queue. Runtime notifications preserve owned packets on a full queue.
- Made the heating switch runtime-effective and fail-closed while keeping
  SHT30 sampling/fan independent; the original thermal task is always created.
- Left interval, certified gas policy, experimental firmware, and deferred
  statistic/storage/upload switch semantics unchanged. Hardware qualification
  remains pending; no device or published-release operation was performed.
