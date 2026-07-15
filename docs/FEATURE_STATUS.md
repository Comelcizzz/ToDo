# Feature status

Statuses: `IMPLEMENTED` | `PARTIAL` | `STUB` | `MOCK` | `MISSING` | `SCAFFOLD` | `NOT VERIFIED IN FL`

## Scope

| Layout | Status |
|---|---|
| Mono / stereo | In scope |
| Multichannel / 5.1 / surround | **OUT OF SCOPE / MISSING** |

## Milestone 1A — Metering

Mono/stereo LUFS / TP / LRA: **IMPLEMENTED**. Multichannel OUT OF SCOPE.

## Milestone 1B — Master safety DSP

Oversampler + sat/soft/hard + LA TP limiter + Export QC: **IMPLEMENTED**. **M1B ACCEPTED**.

## Milestone 1C — Dynamic EQ / FD sidechain

Dynamic EQ + FD sidechain + detector: **IMPLEMENTED**. **M1C ACCEPTED** (see verification gate in M2A kickoff).

## Milestone 2A — Mix Node VST3 + Suite + installer

| Feature | Status | Notes |
|---|---|---|
| Separate Mix Node VST3 target | **IMPLEMENTED** | `Mastering Audio Mix Node.vst3` |
| Analyzer remains analysis-only | **IMPLEMENTED** | zero-latency pass-through |
| Chain: In→Static EQ→DynEQ→Sat→Out | **IMPLEMENTED** | shared `MixNodeChain` |
| Host sidechain bus | **IMPLEMENTED** | mono/stereo SC layouts |
| Versioned identity + role presets | **IMPLEMENTED** | |
| Action protocol preview/commit/cancel/undo | **IMPLEMENTED** | unit tested |
| Host state persistence (no preview) | **IMPLEMENTED** | unit tested |
| Suite Mix Nodes panel | **IMPLEMENTED** | developer UI |
| Mix Node plugin UI | **IMPLEMENTED** | compact WebView |
| Automation parameters | **IMPLEMENTED** | APVTS |
| Early Inno Setup installer | **PARTIAL** | script + CI attempt; **NOT VERIFIED** manually |
| Portable ZIP includes Analyzer + Mix Node | **IMPLEMENTED** | packaging script |
| FL Studio manual checklist | **IMPLEMENTED** doc | all rows NOT TESTED |
| FL Studio end-to-end | **NOT VERIFIED IN FL** | |
| Metalcore Mix Pass | **MISSING** | |
| ML Lab in installer | **MISSING** | intentionally excluded |
| M/S processing | **SCAFFOLD / MISSING** | |

## Gate

**M1A ACCEPTED** · **M1B ACCEPTED** · **M1C ACCEPTED** · **M2A REMAINS PARTIAL** (see `docs/M2A_EVIDENCE_REPORT.md`).

Do not start full Metalcore Mix Pass / ML Lab / section-aware logic without a separate confirmation after evidence review.
