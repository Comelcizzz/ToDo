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

## Milestone 1B — Master safety DSP

| Feature | Status | Notes |
|---|---|---|
| Reusable oversampler (1/2/4/8×) | **IMPLEMENTED** | Linear-phase polyphase FIR; see `docs/DSP.md` |
| Oversampled saturation (tanh) | **IMPLEMENTED** | Static auto-gain optional; delay-aligned bypass |
| Oversampled soft clipper | **IMPLEMENTED** | Soft knee; C¹ transfer |
| Oversampled hard clipper | **IMPLEMENTED** | Clip in OS domain |
| Look-ahead true-peak limiter | **IMPLEMENTED** | OS detect + LA; ceiling tol +0.15 dB |
| Latency reporting == impulse | **IMPLEMENTED** | Δ ≤ 2 samples in validation |
| Timing-aligned bypass | **IMPLEMENTED** | Dry delay = wet latency + crossfade |
| Parameter smoothing | **IMPLEMENTED** | One-pole ~15–20 ms |
| Master Safety Chain v1 | **IMPLEMENTED** | Input→sat?→soft\|hard?→TP lim |
| Offline latency compensation | **IMPLEMENTED** | Trim leading latency; finalize tail |
| Export QC | **IMPLEMENTED** | PASS/WARNING/FAIL JSON+MD |
| `tools/dsp-validation-render` | **IMPLEMENTED** | `artifacts/dsp_validation/` |
| Aliasing reduction vs 1× | **IMPLEMENTED** | Measured in aliasing_report |
| Audio-thread alloc/lock/IO free | **IMPLEMENTED** | After prepare; factor change may alloc in setSettings |
| Multiband limiter / Dynamic EQ | **MISSING** | Milestone 1C+ |
| Mix Node VST3 | **MISSING** | Not started |
| Auto-release heuristics | **MISSING** | Fixed release only |
| ITU FIR inside limiter GR | **PARTIAL** | OS peak + ISP interpolants + −1 dB OS headroom clamp |

## Supporting features

| Feature | Status | Notes |
|---|---|---|
| Official EBU fetch + SHA verify | IMPLEMENTED | WAVs gitignored |
| Official validation artifacts | IMPLEMENTED | `metering-official-validation.*` |
| K-weight FR matrix | IMPLEMENTED | `kweight-frequency-response.*` |
| FIR start/end / finalize / reset tests | IMPLEMENTED | `TruePeakFirTailTests` |
| Analyzer UI states | IMPLEMENTED | Full state set + True Peak / LRA labels |
| ZIP packaging | IMPLEMENTED | Not an installer |
| DSP validation CI artifacts | IMPLEMENTED | Linux + Windows uploads |
| Milestone 1C | — | **Not started** (blocked on M1B gate) |

## Gate

**M1A (mono/stereo): ACCEPTED.**  
**M1B: ACCEPTED** — see `docs/M1B_EVIDENCE_REPORT.md`.

Multichannel remains out of scope. Do not start Milestone 1C without explicit confirmation after the M1B evidence report.
