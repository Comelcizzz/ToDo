# Feature status

Statuses: `IMPLEMENTED` | `PARTIAL` | `STUB` | `MOCK` | `MISSING` | `SCAFFOLD`

## Scope

| Layout | Status |
|---|---|
| Mono / stereo | In scope |
| Multichannel / 5.1 / surround | **OUT OF SCOPE / MISSING** |

## Milestone 1A — Metering

Mono/stereo LUFS / TP / LRA: **IMPLEMENTED** (see prior evidence). Multichannel OUT OF SCOPE.

## Milestone 1B — Master safety DSP

| Feature | Status |
|---|---|
| Oversampler + sat/soft/hard + LA TP limiter | **IMPLEMENTED** |
| Export technical QC | **IMPLEMENTED** |
| Mix Node / installer / metalcore pass | **MISSING** |

**M1B: ACCEPTED** — `docs/M1B_VERIFICATION_REPORT.md`.

## Milestone 1C — Dynamic EQ / FD sidechain

| Feature | Status | Notes |
|---|---|---|
| `EnvelopeDetector` (peak/RMS/hybrid, BP, SC filters) | **IMPLEMENTED** | No output feedback |
| `DynamicEqProcessor` 1–4 bands | **IMPLEMENTED** | Bell / low / high shelf; TPT SVF |
| Downward dynamic cut + maxCut guardrail | **IMPLEMENTED** | Soft knee ~3 dB |
| Internal + external sidechain | **IMPLEMENTED** | Missing SC = silence |
| Frequency-dependent sidechain wrapper | **IMPLEMENTED** | Detector ≠ target band allowed |
| Linked stereo | **IMPLEMENTED** | Default |
| Independent L/R | **IMPLEMENTED** | |
| Mid/Side processing | **SCAFFOLD / MISSING** | Architecture not blocking |
| Zero-latency Dynamic EQ | **IMPLEMENTED** | No look-ahead |
| Absolute state + JSON + idempotency | **IMPLEMENTED** | schema v1 |
| Per-band GR metering + history ring | **IMPLEMENTED** | |
| Legacy `DynamicSeparator` broadband | **IMPLEMENTED** | Kept for compatibility |
| Automatic frequency selection | **MISSING** | MixAdvisor later |
| Resonance detector | **MISSING** | |
| Metalcore rules / Mix Pass | **MISSING** | |
| Section awareness | **MISSING** | |
| Mix Node VST3 | **MISSING** | |
| Multiband compressor | **MISSING** | |
| Upward expansion | **MISSING** | Future |

## Gate

**M1A ACCEPTED** · **M1B ACCEPTED** · **M1C ACCEPTED** (see `docs/M1C_EVIDENCE_REPORT.md`).

Dynamic EQ is **not** intelligent metalcore processing. Do not start Mix Node / Metalcore Mix Pass / installer / ML Lab without a separate confirmation.
