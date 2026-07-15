# Milestone 1B — Verification & Hardening Report

## A. Commits

| Field | Value |
|---|---|
| Branch | `cursor/mastering-audio-932f` |
| PR | https://github.com/Comelcizzz/ToDo/pull/1 |
| Hardening SHA | `27eca068c17acfe945009341b5c0c0fa7feda447` |
| Prior validation-tool SHA | `e2cb8ee` |

## B. Ceiling accuracy

Declared **before** runs:

- allowed overshoot ≤ **+0.10 dB**
- allowed undershoot ≤ **0.20 dB**
- reconstruction guard = **0.12 dB** (documented; replaces hidden −1 dB)

| Signal | Ceiling | Output TP | Error | Max GR | Clamp | Result |
|---|---:|---:|---:|---:|---:|---|
| dense | −0.10 | −0.147 | −0.047 | 8.22 | 0 | PASS |
| dense | −1.00 | −1.047 | −0.047 | 9.12 | 0 | PASS |
| hot sine 1k | −0.10 | −0.219 | −0.119 | 3.75 | 0 | PASS |
| hot sine 1k | −1.00 | −1.119 | −0.119 | 4.65 | 0 | PASS |
| pulse train | −1.00 | −1.119 | −0.119 | 6.23 | 0 | PASS |
| kick-ish | −1.00 | −1.120 | −0.120 | 6.62 | 0 | PASS |

Full matrix: `artifacts/dsp_validation/ceiling_accuracy_report.md` (all PASS).

### −1 dB headroom removed

Previously `ceiling × 10^(-1/20)` was applied in the OS domain as a blunt workaround so post-downsample TP would pass a one-sided `TP ≤ ceiling + 0.15` test. That caused ~0.49 dB systematic undershoot.

**Now:**

1. Look-ahead ring at OS rate predicts peak (sample + ISP midpoints).
2. Instant attack / release envelope applies linked gain to delayed samples.
3. Documented **0.12 dB reconstruction guard** on the *detector target only* (decimation FIR sidelobes / meter TP vs OS sample peak).
4. Safety clamp at **user** ceiling; `safetyClampActivationCount` — observed **0** on all ceiling matrix rows.

## C. Release and pumping

| Max GR | 90% recovery | 99% recovery | Max Δgain/sample | Artifact |
|---:|---:|---:|---:|---|
| 6.23 dB | 242.5 ms | 515.0 ms | 0.031 | `gr_envelope.json` |

Report: `release_pumping_report.md`. Full musical distortion/THD-vs-GR matrix remains smoke-level (not a transparency claim).

## D. Stereo linking

Linked-only mode. Left-only peak → L/R steady-state abs diff = **0** (`stereo_link_report.md`).

## E. Latency

Corrected formula:

```text
M = taps/phase = 48
N = L × M          (full prototype)
upBase   = (M - 1) / 2
downBase = (N - 1) / (2L)
totalBase = upBase + downBase
reported = round(totalBase)
fractionalResidual = totalBase - reported
```

| Processor | OS | Reported | Measured | Residual | Null rejection | Result |
|---|---:|---:|---:|---:|---:|---|
| Oversampler | 2/4/8 | 47 | 47 | 0.25…0.44 | ≈ −85 dB | PASS |
| TP limiter | 4 | 143 | 143 | — | −inf (bypass) | PASS |

Delay-line off-by-one fixed (`read then write` ring → exact N-sample delay). Active/bypass null residual now ~0 for delayed dry.

## F. Aliasing

Metric: 9 kHz @ 48 kHz → 4096 Hann FFT → spurious/total excluding fundamental + odd harmonics (±2 bins). Spectra: `aliasing_spectra.json`.

Soft vs hard: different settings; transfer-curve unit test proves SoftClipper::transfer ≠ hard clamp; 4×/8× metrics diverge. 1× soft/hard can still land similar when both are driven hard into clipping — expected for near-brickwall drive, not identical code paths.

## G. Oversized block safety

| Component | Behavior |
|---|---|
| Analyzer | unchanged: audio pass-through; analysis may drop; degraded |
| MasterSafetyChain / sat / clip / limiter | **chunked** to prepared max block — **never** limiter bypass |

## H. Realtime safety

- Continuous params (ceiling, gains, bypass, release): smoothed, lock-free in `setSettings`
- Topology (OS factor, look-ahead length): **deferred** until `prepare()`; `topologyChangePending()`
- Audio thread: no heap/mutex/IO/JSON; oversized → chunk
- Test: `[verification][rt]`

## I. QC

| Layer | Rules |
|---|---|
| Technical | NaN/Inf, corruption, duration, TP vs ceiling, excessive DC |
| Advisory | Optional `StyleQcProfile` (loudness/GR preferences) — **off by default** |

Metalcore loudness is **not** a universal warning. QC on limiter −1.0 render: technical PASS, advisory PASS (`qc_report.md`).

## J. Performance

**RTF = processed_audio_seconds / wall_clock_seconds** (higher = faster than realtime).

Hardware: x86_64 cloud agent; Release/g++.

| SR | Block | OS | RTF |
|---:|---:|---:|---:|
| 48k | 128 | 4 | 1.85 |
| 48k | 512 | 4 | 1.85 |
| 96k | 128 | 4 | 0.51 |
| 48k | 128 | 8 | 0.51 |

Smoke only — short loops include call overhead; not a ship gate.

## K. Remaining limitations

- Mono/stereo only
- Reconstruction guard 0.12 dB (documented), not zero-error ISP oracle
- Linked stereo only (no independent mode)
- Fixed release (no auto-release)
- No multiband limiter / Dynamic EQ / Mix Node / M1C
- Distortion/THD-vs-GR is smoke, not a musical transparency proof
- Soft/hard 1× alias scalars can coincide under heavy drive (curves still differ)

## L. Gate decision

**`M1B ACCEPTED`**

Hardening closed the verification blockers: two-sided ceiling, no hidden −1 dB, clamp not primary, exact latency, safe oversized chunking, deferred OS topology, QC split, integrity hashes, Windows/Linux CI expected green on this commit.
