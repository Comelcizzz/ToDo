# Architecture

Mastering Audio Suite is a personal local Windows metalcore mixing ecosystem.

## Components

| Component | Status after Milestone 0 | Path |
|---|---|---|
| Analyzer VST3 | Present; honest RT labels; float+double pass-through policy | `apps/analyzer-plugin/` |
| Mix Node VST3 | Missing (PoC after Milestone 1B) | — |
| Standalone Suite | Present; stem mixer + rule MixAdvisor | `apps/mix-desktop/` |
| ML CLI | Research-only Python package | `ml/` |
| ML Lab GUI | Missing | — |
| Portable ZIP packaging | Present (`scripts/package-windows.ps1`) | CI artifact `mastering-audio-suite-windows` |
| Actual installer | Missing (early Inno after M1 / early M2) | — |

## Shared core

- `modules/audio-analysis` — offline analysis + realtime meter + `PassThroughPolicy`
- `modules/dsp` — ProcessorChain (EQ/comp) + Master Safety Chain + Dynamic EQ / FD sidechain + ExportQc
- `modules/assistant` — MixAdvisor with absolute Action targets
- `modules/project-bridge` — `.masuite` schema v2
- `modules/ipc` — bridge payload validation
- `modules/research-export` — privacy-safe ML examples

See `docs/DSP.md` for oversampler, nonlinear processors, limiter, QC, and latency contracts.

## Analyzer audio policy

Finite audio through the Analyzer is **bit-transparent**: samples are not rewritten.

Non-finite values (NaN/Inf) are sanitized to `0`. Sanitization is a safety policy for invalid samples; it does **not** mean arbitrary finite audio is altered.

Evidence: `[milestone0][bit-transparency]` tests in `tests/Milestone0RegressionTests.cpp` and `PassThroughPolicy.h`.

## Action Apply / Reject contract

- **Apply** assigns absolute `targetGainDb` and absolute `ProcessorSettings` (never `+=` / never accumulate).
- Re-Apply of the same absolute Action is idempotent.
- **Reject is valid only while `pending`.** Reject after Apply does **not** revert DSP; state stays `applied`.
- Revert requires a future **Undo** command that restores captured `previousGainDb` / `previousProcessing`.
- Unknown target IDs are skipped. Duplicate action IDs apply in order (last absolute write wins).

## Schema versions

- Project: `kCurrentSchemaVersion = 2` (accepts 1 with migration; rejects newer)
- IPC: `kCurrentSchemaVersion = 1` (rejects missing/newer)

## Milestone order

```text
0 Truth cleanup
→ 1A Standards-aligned metering
→ 1B Oversampling + master safety DSP
→ 1C Dynamic processing infrastructure
→ early developer installer + Mix Node PoC
→ FL hierarchy / Mix Pass / full Mix Node / polished UI / ML Lab GUI
```

Gates: M1A accepted; M1B accepted; **M1C accepted** (`docs/M1C_EVIDENCE_REPORT.md`). Do not start Mix Node / Metalcore Mix Pass / installer / ML Lab without explicit confirmation.

## Host latency model (Milestone 1B+)

Host-facing `getLatencySamples()` reports **end-to-end delay in base project sample-rate samples**, not `lookAhead × oversamplingFactor`.

Decompose internal delay:

- oversampling filter group delay
- look-ahead delay
- additional internal buffering
- resampling / downsampling delay
- rounding policy

Formula:

```text
hostLatencyBaseSamples = ceil(totalInternalDelaySeconds × baseSampleRate)
```

Requirements: reported latency equals measured impulse latency; active and bypass paths share timing; quality/OS changes update latency; standalone render compensates; future Mix Node reports latency via host API.
