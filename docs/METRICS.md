# Metrics (Milestone 1A)

See also [`FEATURE_STATUS.md`](FEATURE_STATUS.md).

## Supported layouts (official M1A scope)

| Layout | Status |
|---|---|
| Mono | Supported |
| Stereo | Supported |
| 5.0 / 5.1 / surround | **OUT OF SCOPE** — not claimed; weights/validation `MISSING` |

Do not advertise untested multichannel metering.

## Standards

| Document | Role |
|---|---|
| ITU-R BS.1770-4 | K-weighting (published 48 kHz coeffs / bilinear elsewhere), gating, true-peak method |
| EBU Tech 3341 / 3342 | Official loudness / LRA / true-peak vectors (local only) |
| EBU R 128 | Metering context |

## Sample peak

- Formula: `samplePeak = max(|x[n]|)` over channels × samples since reset
- Global maximum aggregation; no reconstruction
- Validity after first finite sample; `invalidInput` if NaN/Inf sanitized
- Reset clears accumulator

## True peak (Variant A — standards-validated)

**Algorithm:** 4× polyphase, 24 taps/phase, Hann-windowed sinc, per-phase DC normalization (method-aligned approximation; not ITU Annex 2 published tables).

**Official Tech 3341 cases 15–23: PASS** within published +0.2/−0.4 dB tolerances →

- UI label: **True Peak**
- API: `truePeakIsEstimate=false`, state `valid` when warmed up
- Status: **IMPLEMENTED** (mono/stereo)

### Path structure

1. `accumulateSamplePeak` — sample peak only  
2. `reconstructTruePeak` — FIR polyphase on per-channel history  
3. Per-channel history (24 samples), zero-state before first sample  
4. Global max of reconstructed |y|  
5. Offline `finalize()` → `flushTruePeakTail()` (24 zeros) so EOS peaks are not lost  
6. Realtime without finalize may miss FIR group-delay tail  
7. Warmup: 24 samples → `warmingUp`  
8. Second `finalize()` idempotent  

## Loudness

| Metric | Window | Hop | Notes |
|---|---|---|---|
| Momentary | 400 ms | ≈100 ms | Official case 12 PASS |
| Short-term | 3 s | sample sliding | Official case 9 PASS |
| Integrated | programme | 400 ms / 75% | Absolute −70 / relative −10; RT `provisional` until `finalize()`; cases 1–5,7,8 PASS |
| LRA | ST history | Tech 3342 abs −70 / rel −20; 10th/95th percentile | Cases 1–6 PASS; RT typically unavailable until finalize |

K-weighting uses pre-warped bilinear design matching BS.1770-4 published shelf coefficients at 48 kHz.

## Integrated memory policy

- Capacity: `216000` mean-square blocks at ≈100 ms hop → **≈6.0 hours** at any supported sample rate  
- `integratedCapacitySeconds = blockCapacity × hopFrames / sampleRate`  
- On full: **no silent sliding**; reject new blocks; `programmeCapacityExceeded`; state `degraded`  
- Music-length files remain exact programme integration  

## UI states

Unavailable · Warming up · Provisional · Valid · Unverified · Stale · Degraded · Invalid input  

After reset/project switch: prior numbers must not display (`stale` / cleared).

## Official vectors

- `testdata/official/manifest.json` — IDs, SHA-256, expected, tolerance, licensing  
- `scripts/fetch-loudness-testdata.sh` — `auto|download|verify|manual` (ZIP magic, HTTP fail-hard, no HTML-as-ZIP, no WAV commit)  
- Reports: `metering-official-validation.json` / `.md`  

## Other artifacts

- `metering-validation.json` / `.md` — synthetic RT/offline matrix  
- `kweight-frequency-response.json` / `.md` — FR at 44.1/48/88.2/96/192 kHz  
