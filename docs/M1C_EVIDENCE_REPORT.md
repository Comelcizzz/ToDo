# Milestone 1C — Evidence Report

## A. Commits

| Field | Value |
|---|---|
| Branch | `cursor/mastering-audio-932f` |
| PR | https://github.com/Comelcizzz/ToDo/pull/1 |
| SHA | `68f338bfd881dfe6f2e3389ea1b8d925470ec3ee` |

## B. Architecture

| Component | Class/file | Algorithm | Latency | Status |
|---|---|---|---:|---|
| SVF | `SvfFilter.h` | TPT / linear trapezoidal | 0 | IMPLEMENTED |
| Detector | `EnvelopeDetector` | Peak/RMS/hybrid + BP | 0 | IMPLEMENTED |
| Dynamic EQ | `DynamicEqProcessor` | Static+dynamic filter gain | 0 | IMPLEMENTED |
| FD sidechain | `FrequencyDependentSidechain` | External SC → one band | 0 | IMPLEMENTED |
| Legacy duck | `DynamicSeparator` | Broadband peak | 0 | KEPT |

## C. Detector

- Envelope: one-pole attack/release on linear magnitude; optional hold
- Modes: peak, RMS (~10 ms), hybrid = max
- Aggregation: mono sum / stereo max / linked energy
- Filters: optional BP (detector), HPF, LPF via SVF
- Source: internal dry or external SC; missing SC → silence
- Detection never uses Dynamic EQ output

## D. Dynamic filter

- Topology: TPT SVF (bell / low shelf / high shelf)
- Smoothing: frequency, Q, static gain, threshold, GR, wet/dry, bypass
- Structural (`bandCount`): `setState` / `prepare` only
- Supported types: bell, lowShelf, highShelf

Gain formula:

```text
grDb = min(maxCutDb, softKnee(overshoot) * (1 - 1/ratio))
filterGainDb = staticGainDb - grDb
```

## E. Frequency-selectivity report

From `artifacts/dynamic_processing_validation/spectral_reports/selectivity.md`:

| Processor | Low | Mid | High | BB RMS Δ | Result |
|---|---:|---:|---:|---:|---|
| broadband `DynamicSeparator` | 0.060 | 0.060 | 0.060 | 0.060 | PASS (uniform) |
| FD dynamic band | 0.203 | 0.082 | 0.006 | 0.171 | PASS |

Narrow conflict ducks lows; highs retained vs broadband.

## F. Sidechain report

| Test | Detector | Target | Max GR | Articulation | Result |
|---|---|---|---:|---|---|
| Kick↔Bass | ~70 Hz BP | ~70 Hz bell | metered | high retained | PASS |
| Vocal↔Guitar | ~1.8 kHz | presence | synthetic | — | PASS (render) |
| Snare↔Guitar | ~2.5 kHz | ~2 kHz | synthetic | — | PASS (render) |
| Missing SC | external | band | 0 | safe | PASS |

## G. Stereo report

| Mode | L/R GR difference | Image shift | Result |
|---|---:|---|---|
| Linked | ~0 | none | PASS |
| Independent | separate | allowed | IMPLEMENTED |
| M/S | — | — | SCAFFOLD / MISSING |

## H. Realtime safety

- No alloc/lock/IO/JSON/UI after `prepare`
- Oversized blocks chunked
- Continuous params via `setContinuousParameters`; topology via `setState`/`prepare`
- Evidence: `[milestone1c]` safety tests

## I. Performance

RTF = audio_s / wall_s. Hardware: cloud x86_64.

Example (from `benchmark_smoke.md`): 1 band / 48 kHz / 128 / external SC ≈ **300×** RTF; 4 bands still realtime-safe on this host. Smoke only.

## J. Validation artifacts

```text
artifacts/dynamic_processing_validation/
  kick_bass_*.wav, vocal_guitar_*.wav, snare_guitar_*.wav
  loudness_matched/, spectral_reports/, gain_reduction/
  validation.json, validation.md, benchmark_smoke.md
```

Hashes: FNV-1a 64 in `validation.md`.

## K. Remaining limitations

- No automatic frequency selection / resonance detector
- No metalcore rules / Mix Pass / section awareness
- No Mix Node
- M/S processing SCAFFOLD only
- No multiband compressor; no upward expansion
- No look-ahead Dynamic EQ
- Synthetic validation ≠ musical quality
- Mono/stereo only

## L. Gate

**`M1C ACCEPTED`**

Do not start Mix Node, Metalcore Mix Pass, installer, or ML Lab without a separate confirmation after this report.
