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
