# MILESTONE 4B — Evidence Report

**Gate: MILESTONE 4B REMAINS PARTIAL**

Implementation and local `[milestone4b]` tests are complete. Tip CI was still **queued** (no runner pickup) at documentation time — Linux/Windows green tip is required for full acceptance.

Tip commit: `368578294912d51b685167b327c37a4c7039c34c` (`3685782`)  
Push CI (queued/pending): https://github.com/Comelcizzz/ToDo/actions/runs/29710747373  
PR CI (queued/pending): https://github.com/Comelcizzz/ToDo/actions/runs/29710750626  
Local: `[milestone4b]` 290 assertions / 18 cases; M3B/M3C/M4A regression green.

---

## A. Commits and CI

- Branch: `cursor/m4b-product-hardening-932f`
- Modules: `modules/reliability/*`
- Tests: `[milestone4b]` (18 cases / 290 assertions) green locally
- CI: Linux + Windows filters added for `[milestone4b]`

## B. Reliability audit

See `docs/M4B_RELIABILITY_AUDIT.md` — create/open/save/autosave/recovery/migration/assets/render/export decisions recorded before coding.

## C. Atomic saves

`atomicSaveText` / `atomicSaveBytes`: temp → validate → flush → rename/replace → `.bak`.  
Checksum SHA-256 + schema version. Incomplete `.tmp` does not replace final.

## D. Autosave and recovery

Autosave path `.autosave.masuite`; strips Preview→pending before persist.  
`scanRecoveryCandidates` surfaces Recover / Open original / Save copy / Delete — never auto-overwrites original. Suite timer autosave + status in UI.

## E. Undo/redo

MixPass undo remains **exact absolute state** restore (`previous*`). Session-only stacks; cleared on open. Bounded absolute snapshot pattern tested (50 edits / cap 32).

## F. Schema migrations

Central `migrateDocument` for project/profile/manifest. Steps M3A→M4B through schema **7**. Newer unsupported → read-only refusal. Backup hint `.pre-migrate.bak`.

## G. Asset relinking

Fingerprint search; **name-only match rejected** by default. Manual relink verifies fingerprint; mismatch invalidates analysis cache flag.

## H. Portable projects

Metadata-only default; full local requires **explicit audio consent**. Path traversal blocked. Validate + import APIs.

## I. Job system

Typed jobs, statuses queued→…→interrupted, bounded concurrency, cancel, immutable `snapshotJson` + `renderGraphHash`.

## J. Render queue and integrity

Temp outputs; `validateRenderFile`; cancel/fail → `discardIncompleteRender` (`.incomplete` marker, no completed corrupted WAV).

## K. Disk/memory handling

`estimateDiskSpace`, path/filename validation, `LruByteCache` with budget + low-memory mode + eviction stats.

## L. Error model

Typed errors with code, user message, technical details, recoverable, suggested action.

## M. Diagnostics and logging

Diagnostic bundle without stems/audio; path redaction; structured logger + realtime fault counters (no file IO on audio callback).

## N. Project locking

`.lock` file; second instance → read-only error with owner pid/process; release API.

## O. Export and dither

Export workflow serialization; **no dither for float32**; TPDF only for PCM24 when requested; no double creative limiter note.

## P. Batch export

Routing validation against roles/buses; incomplete routing fails.

## Q. UI reliability

Save / Save as / dirty indicator / autosave status / schema / recovery summary / jobs / diagnostics export (no redesign).

## R. Stress/fuzz/security tests

100 save/open cycles; absolute undo snapshots; malformed JSON; future schema; IPC traversal/command/size/rate limits; package traversal + consent.

## S. Performance

Generous smoke gate (&lt;5s for cache+jobs microbench).

## T. Version consistency

`0.4.0-alpha.m4b`, engineRevision **5**, project schema **7**, profile schema 1; Suite/Analyzer/Mix Node/npm/installer still 0.4.0 family.

## U. Artifacts

Reliability module, M4B tests, audit + this report. No personal stems.

## V. Remaining limitations

- no real-stem musical quality result
- no ML Lab
- FL Studio manual validation postponed
- installer manual validation postponed
- M2A remains PARTIAL
- no perfect-mix claim
- Suite worker-pool is logical job concurrency (not full OS thread pool UI yet)
- StemEngine export still sync path; integrity helpers ready for queue promotion

---

Gate set after tip Linux+Windows CI green:

- **MILESTONE 4B ACCEPTED** or **MILESTONE 4B REMAINS PARTIAL**
