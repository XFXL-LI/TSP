# Repository Instructions for AI Agents

## Canonical firmware

- Standard source: `firmware/standard/TSP`
- Certified source: `firmware/certified/TSP`
- Standard version: `2.0.23`
- Certified version: `2.0.23.1`

The root-level legacy `TSP/` tree has been retired. Never recover current code
from archived or historical source trees unless the user explicitly requests a
historical investigation.

## Build and status entry points

- Standard build: `firmware_workspace/build-standard.cmd`
- Certified build: `firmware_workspace/build-certified.cmd`
- Compatibility build: `firmware_workspace/build-current.cmd` (standard)
- Standard status: `firmware_workspace/status-standard.cmd`
- Certified status: `firmware_workspace/status-certified.cmd`
- Default status: `firmware_workspace/status.cmd` (standard)

Use the variant entry points instead of manually supplying long source paths.
Do not change firmware version numbers unless the user explicitly requests it.

## Release model

- Git tracks release metadata, manifests, hashes, and flash layouts.
- GitHub Releases stores current published firmware binaries.
- Historical release binaries are preserved in an offline archive.
- A `certified` variant name does not imply hardware or field verification.
- Respect the status recorded by each release, including
  `packaged-not-hardware-verified`.

## Change rules

- Modify only the canonical source tree for the requested variant.
- Keep common fixes synchronized between standard and certified when applicable.
- Keep certified-only gas policy isolated to the certified variant.
- Treat `docs/archive/` and other historical material as read-only by default.
- Never overwrite canonical source from historical source snapshots.
- Do not modify source-fingerprint inputs to manufacture matching binaries.
- Do not perform broad formatting or line-ending rewrites of established source.
- Do not flash, erase, or alter device FFat without explicit authorization.

## Git safety

- `github` is the canonical code remote.
- `origin` is the legacy JihuLab remote; never push to it without an explicit
  user request.
- Never force-push.
- Never run `git clean -fd`, `git clean -fdx`, or `git reset --hard`.
- Preserve unrelated worktree changes and use explicit paths when staging.

## Canonical documentation

Long-lived project knowledge belongs only in:

- `docs/PROJECT_CONTEXT.md`
- `docs/OPEN_ISSUES.md`
- `docs/AI_CHANGELOG.md`

Do not automatically create HANDOFF, SESSION, SUMMARY, TASK_REPORT,
DEBUG_REPORT, NEXT_STEPS, or IMPLEMENTATION_NOTES documents.

Keep temporary analysis and intermediate conclusions in the conversation.
Write to canonical documentation only when information has lasting value.

## Documentation update policy

An ordinary development commit should contain only the source, configuration,
build files, tests, and necessary technical documentation actually involved in
that change. Do not mechanically update AI documentation for every code change.

- Update `AGENTS.md` only when repository-level AI rules, Git rules,
  development rules, or documentation-governance rules change.
- Update `docs/PROJECT_CONTEXT.md` only when long-lived project understanding
  changes materially, such as architecture, FreeRTOS tasks, queues or mutexes,
  data flow, configuration model, protocol behavior, canonical source paths,
  build workflow, or release model.
- Update `docs/OPEN_ISSUES.md` only when a long-lived issue is discovered,
  resolved, or materially changes status.
- Update `docs/AI_CHANGELOG.md` only for major architecture or project-baseline
  changes that future AI assistants need to understand. Do not record ordinary
  bug fixes, logging adjustments, or small parameter changes.

If a code change does not alter long-lived project knowledge, do not modify the
canonical AI documentation.

Debug analysis, session summaries, handoffs, task summaries, implementation
notes, debug reports, and next steps remain in the conversation by default and
must not be committed unless the user explicitly requests them.
