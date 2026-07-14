# Metrics (Milestone 1A)

See also [`M1A_AUDIT.md`](M1A_AUDIT.md) for the pre-hardening map of commit `4b95074`.

## Standards

| Document | Role |
|---|---|
| ITU-R BS.1770 | K-weighting, gating, true-peak *method* |
| EBU Tech 3341 / 3342 | Official loudness / LRA vectors (local only) |
| EBU R 128 | Metering context |

## True peak (documented approximation)

- Phases: 4
- Taps/phase: 24 (96-tap prototype)
- Kernel: Hann-windowed sinc, cutoff π/4 in upsampled domain
- Normalization: each phase DC-normalized to sum≈1
- State: 24-sample history/channel across blocks
- Warmup: first 24 samples → `warmingUp`
- **Not** ITU-published Annex 2 coefficient tables

## Loudness windows

| Metric | Window | Hop/update | Valid when |
|---|---|---|---|
| Momentary | 400 ms | ≈100 ms (4 hops) | after ≥400 ms |
| Short-term | 3 s | every sample (sliding) | after ≥3 s |
| Integrated | program | 400 ms / 75% | ≥1 abs-gated block; RT=`provisional` until `finalize()` |
| LRA | ST-derived | finalize | enough gated ST samples; **PARTIAL** |

## UI semantics

- Before window: `Warming up` / `Unavailable` — never fake 0 LUFS
- Streaming integrated: `Provisional`
- After finalize / offline: `Valid`
- Dropped analysis frames: `Degraded` + counter

## Official vectors

`testdata/official/manifest.json` + `scripts/fetch-loudness-testdata.sh`. WAVs not committed. M1A acceptance requires present files with matching SHA-256 and passing tolerances.

## Artifacts

- `metering-validation.json`
- `metering-validation.md`
