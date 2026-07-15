# DSP — Milestone 1B Master Safety

Shared master-safety DSP foundation for Suite realtime, offline render, and future Mix Node.

## Processor audit (pre-M1B → action)

| Processor | Current (pre-M1B) | Keep | Refactor | Replace | Reason |
|---|---|---:|---:|---:|---|
| `ProcessorChain` | EQ→comp→tanh→`std::clamp` | EQ/comp | yes | — | Nonlinear stages moved to Master Safety Chain |
| Saturation | 1× tanh inside chain | — | — | yes | New oversampled `SaturationProcessor` |
| Soft clip | Missing | — | — | new | Continuous C¹ soft knee |
| Hard clip | Base-rate `std::clamp` | — | — | yes | Oversampled `HardClipper` |
| `TruePeakLimiter` | Catmull-Rom + 2-sample delay | — | — | yes | Look-ahead OS true-peak limiter |
| Gain | Static dB→linear | yes | smoothing | — | `ParameterSmoother` |
| Bypass | Early-out, 0 delay | — | yes | — | Delay-aligned dry + crossfade |
| Offline render | No latency trim / QC | — | yes | — | Trim + `ExportQc` |
| Latency model | Documented only | — | yes | — | Reported == measured impulse |

## Master Safety Chain v1

```text
Input Gain
→ Saturation (optional)
→ Soft XOR Hard Clipper (optional; never both auto)
→ True-Peak Limiter
→ Output Gain / meters
→ Export QC (offline)
```

Classes: `MasterSafetyChain`, settings/meters in `MasterSafetyChain.h`.

## Oversampler

| Property | Value |
|---|---|
| Class | `mastering::dsp::Oversampler` |
| Factors | 1×, 2×, 4×, 8× (default production-like: **4×**) |
| Architecture | Polyphase FIR (windowed-sinc prototype split into phases) |
| Phase | **Linear-phase** |
| Taps/phase | 48 (`kTapsPerPhase`); prototype length = `factor × 48` |
| Cutoff | ≈ `0.45 × π / factor` (OS rad/sample) → passband ≈ 0.45·Nyquist base |
| Window | Hann on prototype |
| Stopband | Windowed-sinc class; not equiripple Parks-McClellan |
| Passband ripple | Modest (Hann); DC phases renormalized |
| Interpolation gain | Phases DC-normalized to ~1; full prototype unity-DC for decimation |
| Downsampling | Full prototype FIR on OS history (newest-first, reversed taps) |
| Float | **float** audio path; internal FIR accumulators use **double** |
| Allocations | Only in `prepare()` / factor change; process paths use preallocated scratch |

### Latency formula

```text
M = kTapsPerPhase = 48          # taps per polyphase branch
L = factor                      # 1, 2, 4, or 8
N = L × M                       # full prototype FIR length

upDelayBase   = (M - 1) / 2
downDelayBase = (N - 1) / (2 × L)
totalBase     = upDelayBase + downDelayBase
reported      = round(totalBase)          # host integer samples
fractionalResidual = totalBase - reported # |residual| < 0.5
```

Numeric examples (measured impulse peak == reported):

| L | N | totalBase | reported | residual |
|--:|--:|---:|---:|---:|
| 2 | 96 | 47.25 | 47 | 0.25 |
| 4 | 192 | 47.375 | 47 | 0.375 |
| 8 | 384 | 47.4375 | 47 | 0.4375 |

Limiter host latency:

```text
hostLatencyBaseSamples = oversamplerLatencyBase + lookAheadSamplesBase
```

Do **not** report `lookAheadSamples × oversamplingFactor` as host latency.

### Limiter architecture (post-hardening)

```text
input gain
→ upsample
→ OS look-ahead ring (detect predicted peak + delay audio)
→ gain computer (instant attack / release envelope, stereo-linked)
→ apply gain to delayed OS samples
→ safety clamp at user ceiling (last resort; counted)
→ downsample
→ bypass mix with base-rate delayed dry
→ finalize flush
```

Detector target uses a **documented 0.12 dB reconstruction guard** (FIR sidelobes / meter TP vs OS peak). This replaced the previous hidden −1 dB OS headroom.

## Parameter smoothing

| Property | Value |
|---|---|
| Type | One-pole toward target (`ParameterSmoother`) |
| Typical duration | 15–20 ms (bypass ~15 ms; gains ~20 ms) |
| Sample-rate | Coefficient `exp(-1 / (t * sr))` |
| Reset | Snaps current = target |
| Automation | `setTarget` from non-RT; `next()` per sample on RT |

## Saturation

- Mode v1: **tanh** with optional static auto-gain `1/tanh(drive)`
- Parameters: drive, mix, output trim, bypass, OS factor, autoGainStatic
- Processing in OS domain when factor > 1
- Bypass: delayed dry + wet crossfade (same latency)

## Soft clipper

Continuous soft-knee transfer (symmetric):

```text
|x| ≤ thr - knee/2     → y = x
|x| ≥ thr + knee/2     → y = sign(x) * (thr)   (asymptotic ceiling region)
else                   → quadratic blend (C⁰ and C¹ continuous at knee edges)
```

Implemented by `SoftClipper::transfer(x, thresholdLin, kneeLin)`.

## Hard clipper

```text
y = clamp(x * drive, -ceiling, +ceiling) * outputTrim
```

Applied in the **oversampled** domain; base-rate `std::clamp` is not used as the mastering clipper.

Sample ceiling ≠ reconstructed true peak: post-downsample ISP can exceed sample ceiling; use the TP limiter for dBTP targets.

## True-peak limiter

1. Input gain (smoothed)
2. Base-rate look-ahead delay (`lookAheadMs`)
3. Upsample → OS peak / ISP-style interpolant detect → instant attack, release coeff
4. Linked stereo gain (max across channels)
5. Safety clamp at **user** ceiling (last resort; `safetyClampActivationCount`)
6. Downsample
7. Bypass mixes with `totalLatency` delayed dry
8. `finalize()` flushes zeros through delay/OS tail

Detector target uses a **documented 0.12 dB reconstruction guard**. Declared two-sided ceiling: overshoot ≤ +0.10 dB, undershoot ≤ 0.20 dB.

## Timing-aligned bypass

Active and bypass share the same reported latency (dry delay line length = processor latency). Crossfade via `bypassSm_`. No polarity invert; no gain change when settled bypassed.

## Offline render latency compensation

Policy (`StemEngine::renderMaster` + validation tool):

- Preserve musical start (trim pure processing latency)
- Flush processor tail via `finalize`
- Do not trim legitimate audio after latency
- Write `.qc.json` / `.qc.md` beside export

## Export QC

`ExportQc::analyse` → PASS / WARNING / FAIL.

FAIL: NaN, Inf, unreadable, corrupt duration, TP ≫ ceiling.

WARNING: excessive GR, clipping duration, high DC, suspicious silence/loudness.

Metalcore loudness targets are **not** universal FAIL criteria.

## Realtime safety

After `prepare()`: no heap, mutex, file, socket, JSON, or UI on the audio path. Master-safety / Dynamic EQ oversized blocks are **chunked** (never silent limiter/EQ bypass).

## Validation tool

```text
tools/dsp-validation-render → artifacts/dsp_validation/
tools/dynamic-processing-validation → artifacts/dynamic_processing_validation/
```

## Listening checklist

See `artifacts/dsp_validation/listening_checklist.md` (also generated by the tool). Unit tests do not prove musical quality.

---

# Milestone 1C — Dynamic EQ & frequency-dependent sidechain

## Audit summary

| Component | Decision |
|---|---|
| `DynamicSeparator` | **Keep** (legacy broadband) |
| `ProcessorChain` compressor/EQ | **Keep** (track chain) |
| New TPT SVF + `EnvelopeDetector` | **Add** |
| `DynamicEqProcessor` / `FrequencyDependentSidechain` | **Add** |

## Dynamic EQ gain computer

```text
levelDb   = 20 * log10(max(envelope, floor))
overshoot = max(0, levelDb - thresholdDb)
# soft knee ~3 dB near threshold (C0)
grDb      = min(maxCutDb, overshoot_eff * (1 - 1/ratio))
filterGainDb = staticGainDb - grDb
```

Threshold scale: **dBFS** (detector envelope). Static and dynamic gains are combined into one filter gain (no double application).

## Detector

- Modes: peak / RMS / hybrid
- Channel aggregation: mono sum / stereo max / linked energy
- Source: internal (dry pre-EQ) or external sidechain
- Optional detector band-pass + SC HPF/LPF
- Attack/release one-pole on linear envelope; optional hold
- Never feeds back from Dynamic EQ output

## Filter topology

**TPT / linear-trapezoidal state-variable filter** (Zavalishin) for bell / low-shelf / high-shelf.

Why: per-sample frequency/Q/gain modulation without unstable RBJ coefficient interpolation.

Latency: **0 samples** (no look-ahead).

## Stereo

- **Linked** (default): one envelope, identical L/R GR
- **Independent**: per-channel detectors/GR
- Mid/Side: **SCAFFOLD / MISSING** (API not blocking)

## Frequency-dependent sidechain

`FrequencyDependentSidechain` wraps one Dynamic EQ band with `detectorSource=external`. Detector Hz/Q and target Hz/Q are independent. No hard-coded metalcore frequencies.

## State / Action readiness

`DynamicEqState` schema v1: band IDs, absolute params, placeholders for track/sidechain IDs, JSON serialize. `setState` twice is idempotent.

## Not claimed

Dynamic EQ is **not** intelligent metalcore processing, resonance detection, or MixAdvisor.
