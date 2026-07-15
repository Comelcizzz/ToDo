# M1A Compliance Evidence Report

## Verdict

**M1A ACCEPTED for mono/stereo metering.**  
Multichannel / 5.1 remains **OUT OF SCOPE**. **Milestone 1B not started.**

Compliance commit: `b3dabc23f27465246990c8d64d456cdaedabf5ab`  
PR: https://github.com/Comelcizzz/ToDo/pull/1

## Per-metric status

| Metric | Algorithm status | Official vectors | Synthetic matrix | RT/offline match | Final status |
|---|---|---|---|---|---|
| Sample Peak | max\|x\| | n/a | yes | yes | **IMPLEMENTED** |
| Momentary LUFS | BS.1770 400 ms | ebu3341-12 PASS | yes | yes | **IMPLEMENTED** |
| Short-term LUFS | BS.1770 3 s | ebu3341-9 PASS | yes | yes | **IMPLEMENTED** |
| Integrated LUFS | gated; 6h cap→degraded (no sliding) | ebu3341-1…5,7,8 PASS | yes | provisional→finalize | **IMPLEMENTED** |
| True Peak | 4×/24 Hann-sinc **Variant A** | ebu3341-15…23 PASS | yes | FIR tail finalize | **IMPLEMENTED** |
| LRA | Tech 3342 percentiles | ebu3342-1…6 PASS | yes | finalize | **IMPLEMENTED** |
| RMS / Crest / Correlation | Analyzer | n/a | yes | yes | **IMPLEMENTED** |

ebu3341-6 (5.0): **SKIP / OUT OF SCOPE**.

## Official vector table (summary)

`present=24 missing=0 skipped=1 passed=24 failed=0`  
Manifest SHA-256 verified. Full table: `testdata/official/metering-official-validation.md`

| Vector ID | Metric | Expected | Actual | Delta | Tolerance | Result |
|---|---|---:|---:|---:|---:|---|
| ebu3341-1 | integratedLufs | -23 | -22.9968 | 0.003 | 0.1 | PASS |
| ebu3341-2 | integratedLufs | -33 | -33.0031 | -0.003 | 0.1 | PASS |
| ebu3341-3 | integratedLufs | -23 | -23.0574 | -0.057 | 0.1 | PASS |
| ebu3341-4 | integratedLufs | -23 | -23.0574 | -0.057 | 0.1 | PASS |
| ebu3341-5 | integratedLufs | -23 | -23.0223 | -0.022 | 0.1 | PASS |
| ebu3341-6 | integratedLufs | -23 | — | — | — | SKIP |
| ebu3341-7 | integratedLufs | -23 | -23.0355 | -0.035 | 0.1 | PASS |
| ebu3341-8 | integratedLufs | -23 | -23.0406 | -0.041 | 0.1 | PASS |
| ebu3341-9 | shortTermLufs | -23 | -23.0295 | -0.030 | 0.1 | PASS |
| ebu3341-12 | momentaryLufs | -23 | -23.0037 | -0.004 | 0.1 | PASS |
| ebu3341-15…23 | truePeakDbtp | −6/+3/0 | within +0.2/−0.4 | — | asymmetric | PASS |
| ebu3342-1…6 | loudnessRangeLu | 10/5/20/15/5/15 | within ±1 LU | — | 1.0 | PASS |

## Key fixes in this revision

1. K-weighting → pre-warped bilinear matching BS.1770-4 published 48 kHz shelf (was RBJ; ~0.22 LU bias).
2. Official fetch/verify fail-hard + manifest + manual offline mode.
3. Sample peak vs true-peak split; FIR zero-tail on `finalize()`; oversize block drops analysis (full audio pass-through).
4. UI: True Peak (Variant A), LRA states, degraded drop counter.

## Windows CI

### Hardening `3afea35`

| Field | Value |
|---|---|
| Exact SHA | `3afea3596d487f39676b81bd4f778a1c71d025bc` |
| Run ID | [29377025608](https://github.com/Comelcizzz/ToDo/actions/runs/29377025608) |
| windows-products | **success** |
| Test count | 38/38 |
| Artifact | `mastering-audio-suite-windows` |
| Analyzer VST3 | `…/VST3/Mastering Audio Analyzer.vst3/Contents/x86_64-win/Mastering Audio Analyzer.vst3` |

### Compliance `b3dabc2`

| Field | Value |
|---|---|
| Exact SHA | `b3dabc23f27465246990c8d64d456cdaedabf5ab` |
| Run ID | [29378138212](https://github.com/Comelcizzz/ToDo/actions/runs/29378138212) |
| windows-products | **success** |
| Test count | **53/53** |
| Official vectors (Linux core) | present=24 passed=24 |
| Artifacts | `mastering-audio-suite-windows`, `metering-compliance-artifacts`, `metering-compliance-artifacts-windows` |
| Analyzer VST3 | same path as above |

## Remaining notes (not blockers for mono/stereo M1A)

- Surround/5.1 intentionally unsupported.
- RT allocation hook proven on Linux test binary; Windows relies on design + no-alloc process path.
- Integrated capacity hard stop at ~6 h with `degraded` (no silent sliding).
