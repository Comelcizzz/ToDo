# Milestone 3A — Evidence Report

## Gate decision

**MILESTONE 3A ACCEPTED** (automated evidence).

Manual FL Studio / installer validation remains postponed and does **not** block this gate.
Status lock for M2A remains:

`MILESTONE 2A REMAINS PARTIAL — AUTOMATED IMPLEMENTATION PASSED, MANUAL FL STUDIO AND INSTALLER VALIDATION POSTPONED`

---

## A. Commits

See PR / branch `cursor/mastering-audio-932f` tip after M3A land. Key themes:

- Project schema v3: pairs, buses, sections, `MixPassAction`, DynEQ persistence
- `MetalcoreMixPass` action generation + Preview/Apply/Reject/Edit/Undo
- Suite wiring + usable Mix Pass UI
- StemEngine DynEQ realtime + offline render; RAW compare mode
- `[milestone3a]` tests + `mixpass_validation` + `benchmarks/personal/`
- CI artifacts for Mix Pass (Linux + Windows)

## B. Architecture

```
Suite (standalone)
  StemEngine (mix / DynEQ / RAW|AUTO|CURRENT|REF)
  MetalcoreMixPass (hierarchy + MixPassAction graph)
  ProjectDocument schema v3
Analyzer VST3 (analysis-only, expanded roles)
Mix Node VST3 (unchanged M2A path; FL NOT MANUALLY VERIFIED)
```

Each Mix Pass suggestion is a typed `MixPassAction` with absolute DSP targets (not text-only).

## C. Roles, pairs and buses

Supported roles include kick, snare, drum bus, bass, rhythm guitar L/R, guitar bus, clean/scream vocal, vocal bus, synth/FX/music buses, master.

`ensureHierarchy()` creates Guitar/Drum/Bass/Vocal/Music buses, keeps L/R separate, links a Rhythm Guitar Pair, assigns parent buses. **No automatic time/phase alignment of guitar doubles.**

## D. Action generation

`MetalcoreMixPass::generateActions()` emits Actions with: problem type, target, processor, absolute state, current/proposed, safe range, confidence, explanation, source metrics, section scope, Preview/Apply/Reject/Edit/Undo.

## E. Kick/bass

Frequency-dependent Dynamic EQ on bass keyed from kick (detector ~65 Hz, target ~70 Hz), bounded max cut, envelope attack/release from BPM, complementary static EQ. Broadband `DynamicSeparator` only when DynEQ is off.

## F. Guitar L/R

Separate L/R analysis: section-relative level balance, low-mid mud, harshness/fizz proxies. Processing level can be track or pair-linked. No auto align.

## G. Vocal/guitar

Vocal→guitar DynEQ unmask, vocal riding/serial compression proxy, clean vs scream via manual roles, section-relative chorus vocal offset.

## H. Snare/guitar

Snare-keyed DynEQ unmask on guitars (crack/body region).

## I. Sections

Manual markers: intro, verse, pre-chorus, chorus, breakdown, bridge, outro, custom. Global processing is base; sections add small relative offsets.

## J. Reference comparison

Loudness-matched REF monitoring. Mix Pass may add `referenceVocalToBed` when a reference is present. Does **not** blindly copy reference EQ curves.

## K. Preview/Apply/Undo

Suite commands: `mixpass-preview|apply|reject|edit|cancel-preview|undo|redo`. Apply is absolute and idempotent.

## L. Save/reopen

Schema v3 serializes pairs/buses/sections/`mixPassActions` + DynEQ state. Round-trip covered by tests.

## M. Render and QC

`StemEngine::renderMaster` applies processor + DynEQ (kick→bass FD and other SC DynEQ). Export writes `.qc.json` / `.qc.md`. Validation tool writes RAW/AUTO/loudness-matched proxies + applied-actions report.

## N. Tests and CI

- Catch tags `[milestone3a]`
- `mixpass_validation` → `artifacts/mixpass_validation/` + `benchmarks/personal/`
- Linux + Windows CI continue Suite / Analyzer / Mix Node / portable ZIP / optional Inno

## O. Artifacts

- `artifacts/mixpass_validation/` (RAW/AUTO WAVs, reports)
- `benchmarks/personal/manifest.json` (+ generated synthetic stems in CI; copyrighted audio forbidden in repo)

## P. Performance

Synthetic 2 s Mix Pass validation is offline/core-only (no JUCE host). DynEQ realtime path remains zero look-ahead (M1C).

## Q. Remaining limitations

- Synthetic validation ≠ musical quality proof
- Drum-bus glue may proxy onto snare when no discrete drum-bus stem
- Section offsets are small relative proposals, not full automation curves
- FL Mix Node sidechain/PDC/automation: **NOT MANUALLY VERIFIED IN FL STUDIO**
- Installer: **NOT MANUALLY VERIFIED**
- ML Lab not started
- Full drum mic trees / dense orchestral sessions out of M3A scope

## R. Gate decision

**MILESTONE 3A ACCEPTED** on automated criteria 1–19 (FL/installer remain honestly unverified and postponed).
