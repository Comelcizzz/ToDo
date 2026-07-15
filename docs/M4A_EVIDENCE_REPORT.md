# MILESTONE 4A — Evidence Report

**Gate: MILESTONE 4A ACCEPTED**

Infrastructure milestone only. Musical quality is **not** proven. Real stems have not been evaluated. ML Lab has not started.

Tip commit: `23e0f5bc2aea5fcd299adafbb5bea5a846334b2d` (`23e0f5b`)  
Push CI: https://github.com/Comelcizzz/ToDo/actions/runs/29442392596 (core + windows-products success)  
PR CI: https://github.com/Comelcizzz/ToDo/actions/runs/29442395468 (core + windows-products success)

---

## A. Commits and CI

- Branch: `cursor/mastering-audio-932f`
- Tip: `23e0f5b` — path-resolution fix for CTest cwd
- Prior: `617d625` — M4A infrastructure implementation
- Local: `[milestone4a]` 16/16 passed; M3A/B/C regression passed
- CI tip green on Linux (`core`) and Windows (`windows-products`)
- Products still built: Suite, Analyzer VST3, Mix Node VST3, installer, portable ZIP

## B. Product / build versioning

`modules/product-version/` provides unified identity:

| Field | Value |
|---|---|
| Semantic | `0.4.0` |
| Prerelease | `alpha.m4a` |
| Short SHA | build-time git SHA |
| Profile schema | `1` |
| Engine revision | `4` |
| Project schema | `6` |

Full string example: `0.4.0-alpha.m4a+23e0f5b`

Surfaces:
- Suite (`Main.cpp` application version + WebView `productVersion`)
- Analyzer / Mix Node (WebView `productVersion`; JUCE `VERSION ${PROJECT_VERSION}`)
- Installer (`MyAppVersion` → `0.4.0`)
- npm packages → `0.4.0`
- Experiment / objective / reproducibility manifests stamp engine version
- Action `decisionTrace` includes `engine=…;profile=<id>@<rev>;profileSchema=…`

## C. Benchmark library

```
benchmarks/
  synthetic/          # tracked readiness docs
  profiles/           # tracked MetalcoreProfile JSON
  experiments/        # tracked placeholder
  personal/           # LOCAL-ONLY
    sessions/<id>/{manifest,stems,references,targets,renders,reports,listening,cache}
```

Personal audio is never copied into the repo or CI artifacts.

## D. Session manifest

`BenchmarkSessionManifest` fields include schemaVersion, sessionId, projectName, alias, BPM, optional tempo map, sample rate, bit depth, expected duration, time signature, stems, roles, channel positions, L/R pairs, layers, buses, sections, references, optional target/manual mix, character notes, excluded processors, optional expected-problem annotations, and `localOnly`.

Per-stem: asset ID, path, fingerprint, role/sub-role, mono/stereo, pair ID, parent bus, gain, pan, polarity, start offset, notes.

## E. Import validation

Checks file existence, readable audio, WAV/AIFF, sample rate, channels, duration, finite samples, clipping, DC, leading/trailing silence, inconsistent lengths, unexpected offsets, SR mismatch, duplicate file/asset ID, missing pair partner, missing role, invalid bus routing, cycles, reference validity.

Results: PASS / WARNING / ERROR / USER DECISION REQUIRED. Ambiguous alignment is never auto-fixed; suggestions only (preserve offset, user-approved offset, pad, truncate, longest range).

## F. MetalcoreProfile schema

Typed fields covering detectors, evidence weights/thresholds, role caps, risk/AUTO eligibility, budgets, conflict limits, kick/bass, guitar mud/harsh/fizz, vocal guardrails, drum transient guardrails, unmasking limits, section offsets, reference deviation, loudness policy, stereo/correlation, master safety. Validation, defaults, unknown-field handling, deterministic serialization. Defaults checked into `benchmarks/profiles/`.

## G. Profile hierarchy and hard caps

Precedence: hard caps ← defaults ← project ← session. Overrides cannot exceed hard caps. Minimum profiles only: `modern-metalcore-balanced`, `modern-metalcore-aggressive`, `custom`. Aggressive remains technical-safe/bounded.

## H. Experiment runner

Compares profile A vs B (and metadata for engine A/B). Definition: experiment ID, sessions, engines, profiles, changed params, seed, render modes, metrics, output folder. Result: actions, graphs, metrics, diffs, listening package path, manifest, hashes. Suite: start/cancel/progress via state.

## I. Action Graph diff

Reports Action added/removed, target/processor/frequency/Q/amount/evidence/risk/AUTO eligibility/section scope changes.

## J. Ablation framework

Variants: full AUTO; without Action; only Action; without category (kick/bass, guitars, vocals, drums, sections, reference-derived, stereo, master safety when safe). Limits: top-N / high-risk / low-evidence / user-selected; `maxVariants`.

## K. Objective reports

Technical (duration, SR, LUFS-I/S, LRA, TP, sample peak, DC, NaN/Inf, clipping, integrity) and processing (counts, cumulative EQ cut, GR, parallel wet, width, section offsets, AUTO vs Preview-only). No single mix quality score.

## L. Expected-problem evaluation

Optional manifest annotations used only for detection evaluation table and recall/FP/target/section accuracy. Engine does not read them during normal generation (isolation test).

## M. Listening package v2

Variants: RAW, AUTO A/B, CURRENT, optional TARGET/REF. Loudness-matched relevant comparisons. Random A–E labels; identity not in evaluator-facing names/metadata. Answer key separate. Categories include kick impact through overall preference; whole song / excerpts / loop points supported. Listening not required now.

## N. Listening result schema

`ListeningEvaluation`: evaluation ID, session, anonymous labels, evaluator alias, optional headphones/environment, category scores, pairwise preference, confidence, notes, fatigue, timestamp, answer key only after completion. Local storage for future ML Lab use; M4A does not train.

## O. User edit capture

Structured local events for edit/reject (and extensible to preview/apply/frequency/amount/section/target): original proposal, final value, change type, session/project, engine/profile version, evidence, timestamp. Not called training data.

## P. Privacy / local-data handling

Personal sessions/stems/references/renders/evaluations: local-only, not committed, not uploaded, excluded from diagnostics/CI/crash reports/installer. Suite: local-data location, open folder, clear renders, clear cache. Manifest/evaluation metadata exportable without audio.

## Q. Reproducibility

Runs store asset fingerprints, engine/profile/project schema versions, settings, seed, Action Graph, order, processor states, section automation, render graph ID, WAV/report SHA-256, OS/CPU/build type. Deterministic Action Graph on rerun. Audio tolerance documented across platforms.

## R. Linux/Windows comparison

See `docs/M4A_LINUX_WINDOWS_COMPARISON.md`. Tip CI green on both OS; Action Graph equality required; audio float tolerance allowed.

## S. Golden regressions

Versioned golden expectations for synthetic sessions: problem categories, target level, section, allowed freq/amount ranges, forbidden Actions, max Action count. Catches hardcoded 65/70, snare-as-drum-bus fallback, low-evidence musical AUTO, guitar Right→unnecessary Guitar Bus cut, global section Actions, RAW/AUTO identity, LUFS match breaks, action explosion.

## T. Performance / storage

Configurable `RenderRetentionPolicy`. Benchmark matrix (1/5 sessions, 16/32 stems, 48/96 kHz, profile A/B, ablation top-5) is supported by the runner; detailed timing numbers are for offline personal runs, not claimed as a quality score.

## U. Artifacts

Tracked profiles, synthetic readiness README, M4A tests, evidence + cross-platform docs. Personal audio excluded from artifacts.

## V. Remaining limitations

- no real-stem musical quality result yet
- no ML Lab
- FL Studio manual validation postponed
- installer manual validation postponed
- M2A remains PARTIAL
- no universal perfect-mix claim

---

# MILESTONE 4A ACCEPTED

Infrastructure accepted. Do not start ML Lab without separate confirmation.
