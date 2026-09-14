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
