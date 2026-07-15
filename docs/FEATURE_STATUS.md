# Feature status

Statuses: `IMPLEMENTED` | `PARTIAL` | `STUB` | `MOCK` | `MISSING`

## Scope

| Layout | Status |
|---|---|
| Mono / stereo | In scope |
| Multichannel / 5.1 / surround | **OUT OF SCOPE / MISSING** |

## Per-metric status (M1A)

| Metric | Algorithm status | Official vectors | Synthetic matrix | RT/offline match | Final status |
|---|---|---|---|---|---|
| Sample Peak | max\|x\| | n/a | yes | yes | **IMPLEMENTED** |
| Momentary LUFS | BS.1770 400 ms | Tech 3341-12 PASS | yes | yes | **IMPLEMENTED** |
| Short-term LUFS | BS.1770 3 s | Tech 3341-9 PASS | yes | yes | **IMPLEMENTED** |
| Integrated LUFS | gated programme; 6 h cap → degraded | Tech 3341-1…5,7,8 PASS | yes | provisional until finalize | **IMPLEMENTED** |
| True Peak | 4×/24 Hann-sinc (Variant A) | Tech 3341-15…23 PASS | yes | finalize flushes FIR tail | **IMPLEMENTED** |
| LRA | Tech 3342 percentiles | Tech 3342-1…6 PASS | yes | finalize-only | **IMPLEMENTED** |
| RMS / Crest / Correlation | Analyzer | n/a | yes | yes | **IMPLEMENTED** |

Surround / 5.1 case Tech 3341-6: **SKIP / OUT OF SCOPE**.

## Supporting features

| Feature | Status | Notes |
|---|---|---|
| Official EBU fetch + SHA verify | IMPLEMENTED | WAVs gitignored; `present=24 missing=0` |
| Official validation artifacts | IMPLEMENTED | `metering-official-validation.*` |
| K-weight FR matrix | IMPLEMENTED | `kweight-frequency-response.*` |
| FIR start/end / finalize / reset tests | IMPLEMENTED | `TruePeakFirTailTests` |
| Audio-callback safety | PARTIAL | Linux alloc hook + oversize drop; no lock/IO/JSON on callback |
| Analyzer UI states | IMPLEMENTED | Full state set + True Peak / LRA labels |
| ZIP packaging | IMPLEMENTED | Not an installer |
| Milestone 1B / 1C | — | **Not started** |

## Gate

Official vectors loaded + hashed; LUFS/LRA/TP PASS; FIR tail finalized; matrices + artifacts present; UI honest; Windows CI green for compliance commit `b3dabc2`.

**Overall M1A (mono/stereo): ACCEPTED.** Multichannel remains out of scope. Do not start Milestone 1B without explicit confirmation.
