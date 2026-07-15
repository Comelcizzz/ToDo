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

Dynamic EQ + FD sidechain + detector: **IMPLEMENTED**. **M1C ACCEPTED**.

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
| Early Inno Setup installer | **PARTIAL** | script + CI attempt; **NOT MANUALLY VERIFIED** |
| Portable ZIP includes Analyzer + Mix Node | **IMPLEMENTED** | packaging script |
| FL Studio manual checklist | **IMPLEMENTED** doc | all rows NOT TESTED |
| FL Studio end-to-end | **NOT VERIFIED IN FL** | postponed; does not block M3A |
| ML Lab in installer | **MISSING** | intentionally excluded |
| M/S processing | **SCAFFOLD / MISSING** | |

**Gate:** `MILESTONE 2A REMAINS PARTIAL — AUTOMATED IMPLEMENTATION PASSED, MANUAL FL STUDIO AND INSTALLER VALIDATION POSTPONED`

## Milestone 3A — Standalone Metalcore Mix Pass V1

| Feature | Status | Notes |
|---|---|---|
| Stem import WAV/AIFF | **IMPLEMENTED** | Suite desktop |
| Track / pair / bus hierarchy | **IMPLEMENTED** | `MetalcoreMixPass::ensureHierarchy` |
| Rhythm Guitar L/R separate + pair | **IMPLEMENTED** | no auto time/phase align |
| BPM + manual section markers | **IMPLEMENTED** | intro…outro + custom |
| Actionable MixPassAction DSP | **IMPLEMENTED** | absolute processor state |
| Kick/bass FD DynEQ Actions | **IMPLEMENTED** | not broadband duck when DynEQ on |
| Guitar L/R balance / mud / harshness | **IMPLEMENTED** | pair-scope options |
| Vocal/guitar unmask Actions | **IMPLEMENTED** | DynEQ + riding |
| Snare/guitar unmask Actions | **IMPLEMENTED** | DynEQ |
| Preview / Apply / Reject / Edit | **IMPLEMENTED** | Suite UI + engine |
| Idempotent Apply | **IMPLEMENTED** | unit tested |
| Undo / Redo | **IMPLEMENTED** | |
| RAW / AUTO / CURRENT / REF compare | **IMPLEMENTED** | loudness-matched REF |
| Save / reopen MixPlan + DynEQ | **IMPLEMENTED** | schema v3 |
| Master export + QC | **IMPLEMENTED** | DynEQ on render path |
| Synthetic benchmark layout | **IMPLEMENTED** | `benchmarks/personal/` (no copyrighted audio) |
| Mix Pass tests + validation tool | **IMPLEMENTED** | `[milestone3a]` + `mixpass_validation` |
| Suite / Analyzer / Mix Node / installer / ZIP CI | **IMPLEMENTED** | continues from M2A |
| FL-specific Mix Node features | **NOT VERIFIED IN FL** | unchanged |
| Installer manual validation | **NOT MANUALLY VERIFIED** | unchanged |
| ML Lab | **MISSING** | not started |

**Gate:** see `docs/M3A_EVIDENCE_REPORT.md` (automated acceptance).

## Manual validation checklist

Kept for a later full Metalcore Mix Pass manual pass: `docs/FL_MIX_NODE_VALIDATION.md`.
