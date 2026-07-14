# Feature status

Statuses: `IMPLEMENTED` | `PARTIAL` | `STUB` | `MOCK` | `MISSING`

Updated after M1A hardening (post-`4b95074` audit).

| Feature | Status | Evidence | Limitation |
|---|---|---|---|
| Analyzer float/double pass-through | IMPLEMENTED | bit-transparency tests; double scratch capped in prepare | Oversize blocks clamp analysis |
| Sample peak | IMPLEMENTED | LoudnessMeter + tests | — |
| True-peak meter | PARTIAL | 4× polyphase windowed-sinc; block-boundary/atypical size tests | Not ITU published coeffs; no official ISP vectors in CI |
| Momentary LUFS | PARTIAL | 400 ms window; warmingUp until ready | Official Tech 3341 WAV absent |
| Short-term LUFS | PARTIAL | 3 s window; warmingUp until ready | Official vectors absent |
| Integrated LUFS | PARTIAL | BS.1770 gating; RT=`provisional`, finalize=`valid` | Official vectors absent; RT provisional until finalize |
| LRA | PARTIAL | Percentile algorithm on ST approx; finalize-only | Not Tech 3342 certified; no official LRA vectors |
| RT/offline shared core | IMPLEMENTED | Same LoudnessMeter | Integrated provisional differs until finalize |
| Block/SR invariance matrix | PARTIAL | `metering-validation.json/.md` artifact | Expanded SR/block matrix for TP; LUFS subset |
| Official EBU/ITU vectors | MISSING | `manifest.json` + fetch script | Files not redistributed; compliance blocked |
| Analyzer UI states | IMPLEMENTED | Warming up / Provisional / Unavailable / Degraded + dropped frames | No screenshot pack in CI |
| Audio-thread safety | PARTIAL | No alloc/lock/IO in process after prepare; publish on UI timer | No ASan audio-thread guard in CI yet |
| ZIP packaging | IMPLEMENTED | CI artifact | Not installer |
| Actual installer | MISSING | — | — |
| Mix Node | MISSING | — | — |
| ML CLI | PARTIAL | research only | — |
| ML Lab GUI | MISSING | — | — |

## Gate

**M1A REMAINS PARTIAL** — official vectors not obtained/passed in this environment.
