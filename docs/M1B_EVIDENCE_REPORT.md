# Milestone 1B — Evidence Report

## A. Commits

| Field | Value |
|---|---|
| Branch | `cursor/mastering-audio-932f` |
| Base | `master` |
| PR | https://github.com/Comelcizzz/ToDo/pull/1 |
| SHA | `5cb73f61ed207b783af467f08cde26e065ccc725` |

### Changed / added files (M1B)

- `modules/dsp/include|src/.../Oversampler.*`
- `modules/dsp/include|src/.../NonlinearProcessors.*` (sat / soft / hard)
- `modules/dsp/include|src/.../DynamicTools.*` (look-ahead TP limiter)
- `modules/dsp/include|src/.../MasterSafetyChain.*`
- `modules/dsp/include|src/.../ExportQc.*`
- `modules/dsp/include/.../ParameterSmoother.h`
- `apps/mix-desktop/StemEngine.*` (master safety + offline QC/latency trim)
- `tests/OversamplerTests.cpp`, `MasterSafetyDspTests.cpp`, `TruePeakLimiterM1BTests.cpp`
- `tests/RenderPathTests.cpp`
- `tools/dsp-validation-render/main.cpp`
- `CMakeLists.txt`, `.github/workflows/ci.yml`
- `docs/DSP.md`, `docs/FEATURE_STATUS.md`, `docs/ARCHITECTURE.md`, this report

## B. Architecture

| Component | Class/file | Algorithm | Latency | Status |
|---|---|---|---|---|
| Oversampler | `Oversampler` | Linear-phase polyphase FIR | `(N-1)/L` base samples | IMPLEMENTED |
| Saturation | `SaturationProcessor` | tanh + static auto-gain | OS latency | IMPLEMENTED |
| Soft clip | `SoftClipper` | Soft knee C¹ | OS latency | IMPLEMENTED |
| Hard clip | `HardClipper` | OS hard clamp | OS latency | IMPLEMENTED |
| TP limiter | `TruePeakLimiter` | LA + OS detect + OS clamp | OS + look-ahead | IMPLEMENTED |
| Chain | `MasterSafetyChain` | Input→sat?→clip?→lim | Sum of active stages | IMPLEMENTED |
| Export QC | `ExportQc` | Meter + rules | n/a | IMPLEMENTED |

## C. Oversampling

- Filter: windowed-sinc FIR, Hann, polyphase up / full FIR down
- Phase: linear
- Taps/phase: 48
- Attenuation: Hann-sinc class (not equiripple)
- Latency @ 4×: reported 48 base samples (measured 47, Δ≤2 PASS)
- Factors: 1/2/4/8
- Limitations: float I/O; factor change may allocate in `setSettings`; mono/stereo only

## D. Aliasing report

From `artifacts/dsp_validation/aliasing_report.md` (9 kHz sine @ 48 kHz; DFT spurious/total):

| Processor | Factor | Aliasing metric | Delta vs 1× | Result |
|---|---:|---:|---:|---|
| saturation | 1 | 0.08985 | 0 | baseline |
| saturation | 4 | 1.93e-06 | −0.08985 | PASS |
| soft_clip | 1 | 0.09735 | 0 | baseline |
| soft_clip | 4 | 1.42e-05 | −0.09734 | PASS |
| hard_clip | 1 | 0.09735 | 0 | baseline |
| hard_clip | 4 | 1.42e-05 | −0.09734 | PASS |

## E. Limiter report

Declared tolerance: **+0.15 dB** (set before tests).

| Signal | Ceiling | Output TP | Delta | Max GR | Result |
|---|---:|---:|---:|---:|---|
| dense | −0.1 | −0.59 | −0.49 | ~7.2 | PASS |
| dense | −1.0 | −1.49 | −0.49 | ~7.4 | PASS |

Catch suite `[milestone1b][limiter]`: ceilings −0.1 / −0.3 / −1.0 / −2.0 on hot sine + ISP-style cases — PASS.

## F. Latency report

| Processor | Reported | Measured | Delta | Result |
|---|---:|---:|---:|---|
| oversampler 1× | 0 | 0 | 0 | PASS |
| oversampler 2/4/8× | 48 | 47 | −1 | PASS |
| TP limiter 4× | 144 | 142 | −2 | PASS |

## G. QC renders

| Path | Notes |
|---|---|
| `artifacts/dsp_validation/*.wav` | dry, sat/soft/hard 1×&4×, limiter −0.1/−1.0 |
| `artifacts/dsp_validation/loudness_matched/` | RMS-matched A/B |
| `artifacts/dsp_validation/qc_report.{json,md}` | limiter −1.0 export QC |
| SR / bit depth | 48 kHz / 32-bit float |
| Duration | ~2.0 s (+ finalize tail before trim) |
| QC status | WARNING (hot programme LUFS > −5 warning band; no NaN/Inf; TP under ceiling) |

## H. Realtime safety

- Chunked processing to prepared max block (no OS scratch overrun)
- Oversized block: pass-through + `degraded` (no crash / no trim)
- No mutex / file / socket / JSON / UI in process paths
- Allocations confined to `prepare` / OS factor change in `setSettings`
- Evidence: `[milestone1b]` safety cases + code review of process loops

## I. Performance

Hardware context (cloud Linux agent): Intel Xeon, 4 cores, x86_64, Linux 6.12; Release build, g++.

See `artifacts/dsp_validation/benchmark_smoke.md`.

Example (48 kHz, stereo, master safety chain, 2000 blocks):

| Block | OS | RTF (audio/wall) |
|---:|---:|---:|
| 64 | 1 | ~26.8× |
| 64 | 4 | ~9.9× |
| 1024 | 4 | ~9.8× |

No arbitrary CPU pass/fail gate.

## J. CI

| Job | Expectation |
|---|---|
| Linux `core` | ctest + `[milestone1b]` + `dsp_validation_render` |
| Windows `windows-products` | products + tests + validation tool + ZIP |
| Artifacts | `dsp-validation-artifacts`, `dsp-validation-artifacts-windows`, `dsp-benchmark-summary`, `mastering-audio-suite-windows` |
| Test count | 14 `[milestone1b]` cases / ~20k assertions locally green |

## K. Remaining limitations

- Mono/stereo only (no 5.1)
- Limiter uses OS sample+ISP estimate + −1 dB OS headroom clamp (not full ITU FIR TP detector inside GR computer)
- Soft and hard clip never auto-enabled together
- No auto-release heuristics beyond fixed release
- No multiband limiter / Dynamic EQ / Mix Node / Milestone 1C
- Float primary path (double only inside FIR accumulators)
- 8× intended for high-quality/offline when CPU allows

## L. Gate decision

**`M1B ACCEPTED`**

Acceptance checklist:

1. Reusable oversampler + tests — yes  
2. Reported ≈ measured latency — yes (Δ≤2)  
3. Timing-aligned bypass — yes  
4–6. Oversampled sat/soft/hard — yes  
7. Aliasing measurably lower vs 1× — yes  
8–10. Look-ahead TP limiter + ceiling within tolerance — yes  
11–12. Finalize + offline latency trim — yes  
13–14. No NaN/Inf; Export QC — yes  
15–16. RT safety + SR/block matrix in tests — yes  
17–18. CI artifacts wired — yes  
19–20. `docs/DSP.md` + honest `FEATURE_STATUS.md` — yes  

Do **not** start Milestone 1C / Mix Node / installer / Metalcore Mix Pass without a separate confirmation after this report.
