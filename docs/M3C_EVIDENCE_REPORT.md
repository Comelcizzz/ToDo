# Milestone 3C — Evidence Report

## S. Gate decision

**MILESTONE 3C ACCEPTED** — verification hardenings + Linux/Windows CI green on tip `fe82d2b` (run https://github.com/Comelcizzz/ToDo/actions/runs/29437233765).

See `docs/M3C_FINAL_VERIFICATION.md` and `docs/M3C_CI_ARTIFACT_MANIFEST.json`.

Mandatory limitations (honest):
- Synthetic validation is **not** proof of professional musical quality or mix taste.
- FL Studio manual validation remains **NOT MANUALLY VERIFIED IN FL STUDIO**.
- Installer manual validation remains **NOT MANUALLY VERIFIED** (Setup.exe was built in CI).
- No ML Lab / production ML / arrangement editing / “universal perfect mix” claim.
- Blind A/B/C package is a packaging/listening aid on synthetic (or supplied) renders — not a quality certificate.
- Practical analysis duration cap is **30 minutes** per stem (`kMaxAnalysisSeconds`); longer material is truncated with explicit warnings + evidence penalty.

M2A remains: `PARTIAL — MANUAL FL STUDIO AND INSTALLER VALIDATION POSTPONED`.

---

## A. Commits and CI

Branch `cursor/mastering-audio-932f`. M3C layers on M3B adaptive engine:
- Core: `StreamingAnalyzer`, `ParallelCompressor`, `StereoWidth`, `EvidenceModel`, `ActionBudget`, `LoudnessMatch`, `RenderIdentity`, multi-param `SectionAutomation`
- Tests: `tests/MetalcoreEngineM3CTests.cpp` (`[milestone3c]`)
- Tooling: extended `metalcore_engine_validation` (90 s streaming session) + `blind_test_package`
- CI: runs `[milestone3c]` after `[milestone3b]`; uploads `artifacts/metalcore_engine_validation/**` including `blind_test/`

## B. Streaming / full-track analysis

`StreamingAnalyzer` chunked analysis up to `kMaxAnalysisSeconds` (30 min). Validation session is **90 s @ 48 kHz**, stem-by-stem generate → write WAV → stream-analyze (limits peak RAM). Progress reaches 1 on complete finalize; cancel flag supported mid-pass. Cache key is a **layered fingerprint** (asset id, size, mtime, SR/channels/duration, head/mid/tail content hash, algorithm/schema/settings/role/section map) — not size+mtime alone.

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

Complementary one-pole Side split (`sideLow + sideHigh == side`). Unity mode (`lowBandMonoEnabled=false`, width=1, 0 dB gains) is M/S identity. Product low-band mono discards `sideLow` only. Correlation guard + mono passthrough retained.

## H. Loudness match (LUFS policy)

`LoudnessMatch::matchBuffers` uses production M1A `LoudnessMeter`:
- ≥3 s + valid integrated → **`integrated-lufs`**
- 1–3 s → short-term LUFS (when windows valid)
- <1 s → **`bounded-rms-short-preview`** (never labeled as LUFS)

90 s validation documents `integrated-lufs` in `loudness_match.json` + `validation.md`. Unit test: 4 s match within 0.5 dB; short preview method string correct.

## I. Action budget

`applyBudget` runs **after** `ActionResolver`. Rejected conflicts are not budgeted. Corrective Actions preferred over reference under pressure. Section Actions have a separate bounded budget. Track/pair/bus cumulative cuts share aggregate keys.

## J. Evidence model

`EvidenceComponents::breakdown()` nonempty; `label()` → Low / Medium / High from deterministic score (not calibrated probability). `deriveSafeRange` clamps `finalProposal` within caps with trace.

## K. Render identity

`graphId` = FNV-1a-64 (deterministic internal ID only — not cryptographic). `artifactSha256` / `sourceAssetSha256` / `manifestSha256` = SHA-256 integrity. Artifact: `render_identity.json` documents algorithms.

## K2. AUTO risk policy

Risk tiers replace universal 0.45: lowTechnical / conservativeCorrective / musicalCreative. Musical/reference default to Preview. Action carries `riskLevel`, `autoApplyEligibility`, `autoApplyReason`, `requiredEvidence`, `actualEvidence`.

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

- `MainComponent::generateMetalcoreMixPass` streams each stem via `StreamingAnalyzer` (4096 chunks) up to `kMaxAnalysisSeconds` (30 min) with `analyzing XX%` progress — **no silent 60 s cap**. Truncation warns before/after and applies evidence penalty.
- StemEngine: ParallelCompressor on drum-bus / `parallelEnabled`; StereoWidth when enabled; multi-param section offsets; `compareMatchGainDb_` LUFS makeup for fair A/B (does not mutate committed DSP).
- AUTO = risk-eligible auto-applied Actions only (not universal 0.45); CURRENT = committed; RAW = dry.
- Schema **v5** persists parallel/width + budget/identity/truncation fields.

## P. Performance

Validation reports wall time and realtime-factor estimate for the 90 s synthetic session. Not a production DAW stress benchmark.

## Q. Realtime / offline consistency

`RealtimeOfflineCompare` Suite-level battery (DynEQ, VocalRider, section offsets, parallel, StereoWidth, master safety, full AUTO graph) compares block-512 vs block-2048 with documented latency compensation. Shared core unit tests remain.

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
| `[milestone3c]` Catch2 suite | PASS (local) |
| Streaming >60 s + cancel + layered cache | PASS |
| Events / bass occupancy / multi-param automation | PASS |
| Parallel + complementary width DSP | PASS |
| LoudnessMatch integrated + short RMS policy | PASS |
| ActionBudget / Evidence / RenderIdentity SHA-256 / SafeRange | PASS |
| Risk-aware AUTO (not universal 0.45) | PASS |
| Suite RT/offline consistency battery | PASS |
| 30-min truncation reporting | PASS |
| `metalcore_engine_validation` 90 s | PASS |
| Blind package + answer key + render_identity | PASS |
| Linux + Windows CI green on verification tip | **PASS** (`fe82d2b` / run 29437233765) |
| Musical quality / FL / installer / ML | N/A or postponed |

**Gate:** **MILESTONE 3C ACCEPTED** — see `docs/M3C_FINAL_VERIFICATION.md`.
