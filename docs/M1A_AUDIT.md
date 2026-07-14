# Milestone 1A implementation audit (commit `4b95074`)

Audit of the initial M1A implementation before hardening. Status after this audit: **PARTIAL**.

## Implementation map

| Metric | Class/function | Algorithm | State | Window | Standard/approximation |
|---|---|---|---|---|---|
| sample peak | `LoudnessMeter::processTruePeak` → `samplePeak_` | max \|x\| | running max | entire stream since reset | Exact sample peak |
| true peak | `LoudnessMeter::processTruePeak` | 4× polyphase, 12 taps/phase, Hann-windowed sinc, per-phase sum-normalized | 12-sample history/channel | FIR group delay (~6 base samples) | **Approximation** of BS.1770 Annex 2 (not ITU published coeffs) |
| momentary LUFS | `emitHop` → `pushBlockMeanSquare` | K-weight → hop MS → average 4 hops ≈ 400 ms | hop ring + last block MS | 400 ms, hop ≈ 100 ms | BS.1770 block length; hop via 4 sub-hops |
| short-term LUFS | `process` shortTermEnergyRing_ | sliding mean of weighted power | ring of 3 s frames | 3.0 s, update every sample | BS.1770 short-term length |
| integrated LUFS | `recomputeIntegrated` | 400 ms blocks / 75% hop; abs −70 LUFS; rel −10 LU; mean of gated MS | circular `blockMeanSquares_` (≤6 h) | program since reset | BS.1770 gating; **RT value not marked provisional** |
| LRA | `recomputeLoudnessRange` (finalize only) | ST approx from consecutive blocks; abs gate; −20 LU rel; 10–95% | allocates temp vectors | ST-derived | **PARTIAL** vs EBU Tech 3342 |
| RMS | `RealtimeMeter` / `AudioAnalyzer::analyze` | sqrt(mean x²) | atomics / offline sum | whole buffer/stream | Exact RMS (not loudness) |
| crest | derived | samplePeak dB − RMS dB | derived | same as peak/RMS | Exact given inputs |
| stereo correlation | `RealtimeMeter` / offline loop | Pearson on L/R | atomics / offline sums | whole stream | Exact Pearson |

## Shared vs divergent

| Metric | Offline + RT shared? | Shared pieces | Divergent pieces |
|---|---|---|---|
| sample / true peak | Yes | `LoudnessMeter` | Offline finalize flushes hops; RT may leave partial hop |
| momentary / ST / integrated | Yes (same engine) | K-weight, hops, gates | Offline calls `finalize()`; RT integrated computed live without provisional flag |
| LRA | Offline-only path | block energies | Only in `finalize()`; RT snapshot leaves LRA invalid |
| RMS / crest / correlation | Parallel (not in LoudnessMeter) | formulas | Separate accumulators in RealtimeMeter vs AudioAnalyzer |

## Validity / pre-valid / reset / SR / channels

| Metric | Valid when | Before valid | reset() | sample-rate change | channel-count change |
|---|---|---|---|---|---|
| sample peak | `totalFrames > 0` | 0 linear / −120 dB display | clears max | requires `prepare()` | uses active channelCount |
| true peak | `truePeakReady && totalFrames > 0` | same | clears history+max | coeffs rebuilt in `prepare` | history per channel; unused ch ignored |
| momentary | after first full 400 ms block | `momentaryValid=false` (UI may still see −120) | clears | windows resized in `prepare` | weights updated live; **filters not reset** |
| short-term | after 3 s filled | invalid | clears ring | resized in `prepare` | same |
| integrated | after ≥1 abs-gated block | invalid / −120 | clears blocks | cleared via prepare/reset | same |
| LRA | finalize + enough ST samples | invalid | cleared | n/a until finalize | n/a |
| RMS/crest/corr | after samples | silence defaults | atomics cleared | prepare/reset | channelCount stored |

### Gaps found (blocking acceptance)

1. True peak is **not** proven against Annex 2 / official ISP vectors; custom 12-tap sinc.
2. Integrated RT lacks **provisional** vs finalized distinction.
3. UI shows `—` but not Warming up / Provisional / Degraded explicitly enough for all states; can flash −120 if flags mishandled.
4. LRA allocates in finalize; algorithm is approximate → keep **PARTIAL**.
5. Double `processBlock` may `setSize` scratch if block grows → alloc risk on audio thread.
6. No dropped-analysis / overflow instrumentation on Analyzer publish path.
7. Official vectors not fetched; no `manifest.json` machine suite.
8. No full block-size matrix (incl. 1,7,127,511) or 88.2/192 kHz report artifact.
9. Channel-count change mid-stream does not reset K-weight / TP state (can smear).
10. K-weight has no frequency-response unit tests yet.

## Gate decision (pre-hardening)

**M1A REMAINS PARTIAL** until items above are closed or honestly limited with tests + docs.
