# Metrics (Milestone 1A)

## Standards

| Document | Role |
|---|---|
| ITU-R BS.1770 (current edition) | K-weighting, gating, true-peak Annex 2 method |
| EBU Tech 3341 | Loudness test signals / expected readings |
| EBU Tech 3342 | LRA procedure |
| EBU R 128 | Metering context (LUFS / LU) |

## Implementation

Shared engine: `modules/audio-analysis` `LoudnessMeter` used by offline `AudioAnalyzer` and `RealtimeMeter`.

| Metric | Method | Validity flag |
|---|---|---|
| sample peak | max \|x\| → dBFS | always when audio present |
| true peak | 4× polyphase windowed-sinc (Annex 2 method) | `truePeakValid` |
| momentary LUFS | 400 ms K-weighted mean square | `momentaryLufsIsValid` |
| short-term LUFS | 3 s sliding window | `shortTermLufsIsValid` |
| integrated LUFS | 400 ms / 75% hop + absolute −70 / relative −10 gating | `integratedLufsIsValid` |
| LRA | short-term distribution, −20 LU relative gate, 10–95% | `loudnessRangeIsValid` |
| RMS / crest / correlation | existing helpers | always computed |

`prepare()` may allocate. `process()` must not allocate or lock.

## Official vectors

Do not commit copyrighted WAVs. Use `scripts/fetch-loudness-testdata.sh` → `testdata/official/` (gitignored).

Report columns: source document | vector/file ID | expected | tolerance | implementation | pass/fail | sha256.

Synthetic tones are additional regressions, not the sole compliance evidence.

## Tolerances (synthetic / interim)

| Metric | Synthetic expectation | Tolerance |
|---|---|---|
| sample peak 0.5 FS sine | −6.0206 dBFS | ±0.02 dB |
| integrated −23 dBFS 1 kHz stereo | ≈ −23 LUFS | ±0.5 LU (tighten after official vectors) |
| block-size invariance | same integrated | ±0.05 LU |
| sample-rate invariance 44.1/48/96 | same integrated | ±0.3 LU |
| RT vs offline short-term | match after ≥3 s | ±0.15 LU |

## Host latency

Metering is analysis-only and does not add Analyzer output latency (Analyzer remains pass-through). DSP host latency is Milestone 1B.
