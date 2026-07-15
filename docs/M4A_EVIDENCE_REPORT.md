# MILESTONE 4A — Evidence Report

**Gate (pre-CI tip):** implementation complete locally; Linux/Windows CI tip pending green → final gate below.

## A. Commits and CI

- Branch: `cursor/mastering-audio-932f`
- Tests: `[milestone4a]` (16 cases) + M3A/B/C regression green locally
- CI: Linux + Windows workflows run `[milestone4a]` (added)
- Products built locally: Suite, Analyzer VST3, Mix Node VST3

## B. Product / build versioning

Unified `ProductVersion` (`modules/product-version/`):

- Semantic: `0.4.0`
- Prerelease: `alpha.m4a`
- Short commit SHA from `MASTERING_AUDIO_GIT_SHA`
- Profile schema version: `1`
- Engine revision: `4`
- Project schema: `6`

Example display: `0.4.0-alpha.m4a+<sha>`

Visible in: Suite app version + WebView `productVersion`, CMake/JUCE plugin `VERSION 0.4.0`, Inno `0.4.0`, npm `0.4.0`, experiment/objective/reproducibility manifests.

## C. Benchmark library

```
benchmarks/synthetic/   # tracked readiness docs
benchmarks/profiles/    # balanced / aggressive / custom JSON
benchmarks/experiments/ # tracked placeholder
benchmarks/personal/    # local-only sessions (gitignored content)
```

Personal stems/references/renders/cache never committed.

## D. Session manifest

Versioned `BenchmarkSessionManifest` with schemaVersion, sessionId, stems, roles,
pairs, buses, sections, references, target, annotations, `localOnly`.

## E. Import validation

`ImportValidator` classifies PASS / WARNING / ERROR / USER DECISION REQUIRED.
Detects SR mismatch, missing pair, silent/short stems, offsets, routing issues.
**No automatic guitar double alignment** (`no-auto-guitar-align` issue).

## F. MetalcoreProfile schema

Typed `MetalcoreProfile` (detectors, evidence weights, role caps, budget, AUTO,
guitar/vocal/drum/reference/loudness/stereo/masterSafety). Schema + validation +
defaults + migration-friendly deserialize + deterministic serialize.
Tracked JSON under `benchmarks/profiles/`.

## G. Profile hierarchy and hard caps

1. Engine hard safety caps  
2. Default modern-metalcore profile  
3. Project overrides  
4. Session experimental overrides  
5. User prefs (not ML)

`clampProfileToHardCaps` prevents bypass. Presets: balanced, aggressive, custom.

## H. Experiment runner

Headless `ExperimentRunner::compareProfiles` + Suite commands `run-profile-experiment` /
`cancel-experiment`. Emits Action diff, metrics diff, reproducibility notes, worksheet.

## I. Action Graph diff

`diffActionGraphs` reports added/removed/changed (amount, processor, evidence, etc.).
Metric change is **not** auto-labeled improvement.

## J. Ablation framework

`buildAblationPlan` supports remove Action / category / only-Action with master-safety retain and maxVariants limit.

## K. Objective reports

Technical + processing metrics via `ObjectiveReport`. **No MixQualityScore.**

## L. Expected-problem evaluation

Annotations evaluate detection recall/FP only. Generation API does not accept annotations
(test proves identical Action Graph with/without annotation objects).

## M. Listening package v2

Random A–E labels, separate answer key, form categories (kick impact … overall preference).
Identity leak check included.

## N. Listening result schema

`ListeningEvaluation` with scores, pairwise preference, fatigue, reveal-after-completion flag.

## O. User edit capture

`UserEditEvent` / `LocalUserEditLog` on Suite edit/reject. Flagged `notTrainingData` /
`NOT ML training` until ML Lab + consent.

## P. Privacy / local-data

- Personal paths gitignored  
- Suite shows local-only + path; open folder / clear cache / clear renders  
- Research export remains metrics-only  
- CI does not use personal stems  

## Q. Reproducibility

Experiment/repro manifests include engine/profile versions, asset fingerprints, Action Graph.
Action IDs are content-hashed (deterministic).

## R. Linux/Windows comparison

Documented in `docs/M4A_LINUX_WINDOWS_COMPARISON.md` (tolerance policy; not bit-identical audio).

## S. Golden regressions

`GoldenRegression` catches hardcoded 65/70 traces, musical AUTO at low evidence, forbidden fallbacks.

## T. Performance / storage

`RenderRetentionPolicy` (max WAVs, ablation variants, keep days). Suite writes retention JSON with experiments.
Full multi-session timing battery remains optional offline (not blocking infrastructure gate).

## U. Artifacts

- `benchmarks/profiles/*.json`
- `[milestone4a]` Catch2 suite
- Suite Benchmark / Privacy / Experiment commands
- This report + Linux/Windows comparison doc

## V. Remaining limitations

- **No real-stem musical quality result yet**
- **No ML Lab / no training**
- FL Studio manual validation postponed
- Installer manual validation postponed
- **M2A remains PARTIAL**
- No universal perfect-mix claim
- Import wizard is functional validation + Suite hooks (not a full multi-page redesign)
- Cross-platform audio bit-identity not claimed

---

## Gate

Pending green tip CI on Linux + Windows with `[milestone4a]`:

- If tip green with all acceptance criteria met as infrastructure: **MILESTONE 4A ACCEPTED**
- Else: **MILESTONE 4A REMAINS PARTIAL**

Musical quality is **not** claimed by this milestone.
