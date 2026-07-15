# Milestone 3C — Evidence Report

## S. Gate decision

**MILESTONE 3C ACCEPTED** (automated evidence: `[milestone3c]` + `metalcore_engine_validation` locally).

Mandatory limitations (honest):
- Synthetic validation is **not** proof of professional musical quality or mix taste.
- FL Studio manual validation remains **NOT MANUALLY VERIFIED IN FL STUDIO**.
- Installer manual validation remains **NOT MANUALLY VERIFIED**.
- No ML Lab / production ML / arrangement editing / “universal perfect mix” claim.
- Blind A/B/C package is a packaging/listening aid on synthetic (or supplied) renders — not a quality certificate.
- Practical analysis duration cap is **30 minutes** per stem (`kMaxAnalysisSeconds`); longer material is truncated with that documented limit.

M2A remains: `PARTIAL — MANUAL FL STUDIO AND INSTALLER VALIDATION POSTPONED`.

---

## A. Commits and CI

Branch `cursor/mastering-audio-932f`. M3C layers on M3B adaptive engine:
- Core: `StreamingAnalyzer`, `ParallelCompressor`, `StereoWidth`, `EvidenceModel`, `ActionBudget`, `LoudnessMatch`, `RenderIdentity`, multi-param `SectionAutomation`
- Tests: `tests/MetalcoreEngineM3CTests.cpp` (`[milestone3c]`)
- Tooling: extended `metalcore_engine_validation` (90 s streaming session) + `blind_test_package`
- CI: runs `[milestone3c]` after `[milestone3b]`; uploads `artifacts/metalcore_engine_validation/**` including `blind_test/`

## B. Streaming / full-track analysis

`StreamingAnalyzer` chunked analysis up to `kMaxAnalysisSeconds` (30 min). Validation session is **90 s @ 48 kHz**, stem-by-stem generate → write WAV → stream-analyze (limits peak RAM). Progress reaches 1 on complete finalize; cancel flag supported mid-pass. Cache key = `trackId|fileSize|mtimeHash`.

Evidence: `streaming_analysis.json`, `[milestone3c]` long-buffer + cancel tests.

## C. Kick / snare events

Onset detector compares input to a fast envelope (rise vs envelope *before* update) with **min spacing** (kick 0.2 s). Low-band F0 uses a longer window than the transient hop so 50–60 Hz fundamentals resolve. Synthetic kick pulses every 0.5 s produce roughly expected event counts; quiet noise stays near zero false positives (tolerance ≤2).

## D. Bass occupancy

30–180 Hz occupancy bins (~5 Hz). Changing 50 Hz → 80 Hz tones place energy in corresponding bins; `stableFundamentalHz` set from event median or peak bin.

## E. Section automation multi-param

`SectionAutomation::fromActions` maps section-scoped Actions to parameter lanes including `dynMaxCutDb` (not gain-only). `evaluateParameterOffset` nonzero inside section, ~0 outside, smooth crossfade at edges.

## F. Parallel compressor

Real dry/wet parallel path, **0 sample latency** (shared time base). Wet>0 changes RMS vs dry; wet=0 ≈ dry; bypass returns toward dry. Used on AUTO bus in validation.

## G. Stereo width

M/S encode/decode, low-band mono (side LPF residual discarded below cutoff), correlation guard, mono passthrough. Mid/side energy roughly preserved above cutoff; low side reduced below cutoff.

## H. Loudness match (LUFS policy)

`LoudnessMatch::matchBuffers`:
- ≥3 s + valid integrated → **`integrated-lufs`**
- 1–3 s → short-term LUFS
- <1 s → **`bounded-rms-short-preview`** (never labeled as LUFS)

90 s validation documents `integrated-lufs` in `loudness_match.json` + `validation.md`. Unit test: 4 s match within 0.5 dB; short preview method string correct.

## I. Action budget

`applyBudget` sorts by evidence×priority, enforces per-track / DynEQ / cumulative cut / unmask caps. 20 synthetic candidates reduce to budget; high evidence kept preferentially.

## J. Evidence model

`EvidenceComponents::breakdown()` nonempty; `label()` → Low / Medium / High from deterministic score (not calibrated probability). `deriveSafeRange` clamps `finalProposal` within caps with trace.

## K. Render identity

`RenderIdentityBuilder` FNV-1a hashes over render/action/processor/section graphs + asset hashes + loudness match gain. Hash changes when action graph string changes. Artifact: `render_identity.json`.

## L. Safe ranges

`deriveSafeRange(processor, role, evidence, cumulativeRemaining, proposed)` returns proposal inside global/role/evidence/cumulative caps.

## M. Blind test package

`blind_test/A.wav` `B.wav` `C.wav` = randomized RAW / AUTO / CURRENT. `answer_key.json` is evaluator-only (not user-facing names). Also `tools/blind-test-package` for packaging existing renders.

## N. Validation tool / artifacts

`metalcore_engine_validation` writes (among others):

```
artifacts/metalcore_engine_validation/
  stems/*.wav
  raw.wav, auto.wav, current.wav
  loudness_matched/auto_matched.wav
  loudness_match.json
  streaming_analysis.json
  render_identity.json
  render_hashes.json
  action_graph.json, action_conflicts.json
  section_automation.json, hierarchy.json
  reference_profile.json, masking_before_after.json
  blind_test/{A,B,C}.wav, answer_key.json, README.md
  validation.md
```

Exit 0 when smoke gates pass (actions, ≥90 s streaming, integrated-lufs, blind+identity artifacts, no snare drum-glue, conflict/diversity).

## O. Suite / StemEngine integration

- `MainComponent::generateMetalcoreMixPass` streams each stem via `StreamingAnalyzer` (4096 chunks) up to `kMaxAnalysisSeconds` (30 min) with `analyzing XX%` progress — **no silent 60 s cap**.
- StemEngine: ParallelCompressor on drum-bus / `parallelEnabled`; StereoWidth when enabled; multi-param section offsets; `compareMatchGainDb_` LUFS makeup for fair A/B.
- AUTO = project after auto-accept of evidence ≥ 0.45; CURRENT = committed; RAW = dry.
- Schema **v5** persists parallel/width + budget/identity fields.

## P. Performance

Validation reports wall time and realtime-factor estimate for the 90 s synthetic session. Not a production DAW stress benchmark.

## Q. Realtime / offline consistency

Shared DynEQ, ParallelCompressor, StereoWidth, SectionAutomation, LoudnessMatch between core tests and validation offline path. Mix Node / FL realtime still postponed for manual proof.

## R. Limitations (mandatory)

- Synthetic ≠ musical quality.
- Detectors remain heuristic.
- Blind listening on synthetic material does not validate genre taste.
- No claim of broadcast-master readiness from this milestone alone.

## T. FL Studio / installer

**NOT MANUALLY VERIFIED** (postponed; does not block automated M3C gate).

## U. ML Lab

**MISSING** — not started.

## V. Musical quality disclaimer

This report intentionally makes **no** claim that AUTO sounds “better” than RAW/CURRENT. Artifacts exist so humans can listen; automation only proves plumbing, determinism, and policy.

## W. Acceptance criteria checklist

| Criterion | Status |
|---|---|
| `[milestone3c]` Catch2 suite | PASS (local build/run) |
| Streaming >60 s + cancel + cache key | PASS |
| Events / bass occupancy / multi-param automation | PASS |
| Parallel + width DSP unit checks | PASS |
| LoudnessMatch integrated + short RMS policy | PASS |
| ActionBudget / Evidence / RenderIdentity / SafeRange | PASS |
| `metalcore_engine_validation` 90 s + StreamingAnalyzer evidence | PASS |
| Blind package + answer key + render_identity | PASS |
| CI wired for `[milestone3c]` + artifact upload | PASS (workflow updated) |
| Musical quality / FL / installer / ML | N/A or postponed |

**Gate:** **MILESTONE 3C ACCEPTED** when the above automated rows pass locally; otherwise PARTIAL.
