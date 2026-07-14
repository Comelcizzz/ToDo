# Milestone 1 plan (gated)

Do not implement until Milestone 0 verification is accepted.

## Split

### Milestone 1A — Standards-aligned Metering

Scope: sample peak; true-peak meter; momentary / short-term / integrated LUFS; LRA; shared realtime/offline core; block-size + sample-rate invariance; official vectors; `docs/METRICS.md`.

Acceptance: official vectors within tolerance; RT/offline match; no audio-thread alloc/locks; Analyzer remains transparent for finite audio; metrics report validity/window state.

### Milestone 1B — Oversampling and Master Safety DSP

Scope: reusable oversampler; soft saturation; soft/hard clip; true-peak limiter; look-ahead; latency reporting; gain compensation; export QC; audible validation renders.

Acceptance: impulse latency matches reported latency; active/bypass timing aligned; TP after render ≤ ceiling within tolerance; no NaN/Inf; oversampled nonlinear paths reduce aliasing vs 1×; loudness-matched validation renders exist.

Host latency:

```text
hostLatencyBaseSamples = ceil(totalInternalDelaySeconds × baseSampleRate)
```

Do **not** report `baseLookAhead × oversamplingFactor` as host latency.

### Milestone 1C — Dynamic Processing Infrastructure

Scope: dynamic EQ; band-limited detector; external sidechain; frequency-dependent ducking; parameter smoothing; stereo link; L/R independent; M/S-ready architecture.

Acceptance: frequency-response / detector / attack-release tests; sidechain affects configured band only; no broadband duck when conflict is narrowband; no audio-thread alloc/locks.

## Official loudness / LRA material

Use current ITU-R BS.1770, EBU Tech 3341, EBU Tech 3342, and EBU R 128 as metering context.

Do **not** commit copyrighted vectors unless redistribution is clearly allowed.

Preferred approach:

1. `scripts/fetch-loudness-testdata.sh` downloads / verifies official signals into `testdata/official/` (gitignored).
2. `testdata/official/MANIFEST.md` lists source document, vector/file ID, expected result, tolerance, and SHA-256.
3. CI uses cache or skips official vectors when files are absent (synthetic tests still run).
4. Report table per vector: source | ID | expected | tolerance | implementation | pass/fail.

Synthetic tones remain additional regression coverage, not the only compliance tests.

## Golden / invariance matrix (1A)

Block sizes: 32, 64, 128, 256, 512, 1024.  
Sample rates: 44.1 / 48 / 96 kHz.  
RT vs offline must match within documented tolerance after sufficient analysis window.
