# Milestone 3B — Evidence Report

## S. Gate decision

**MILESTONE 3B ACCEPTED** (automated evidence).

Honest caveats (mandatory):
- Synthetic validation is **not** proof of professional musical quality.
- FL Studio manual validation postponed (`NOT MANUALLY VERIFIED IN FL STUDIO`).
- Installer manual validation postponed (`NOT MANUALLY VERIFIED`).
- No ML Lab / production ML / arrangement editing / universal perfect-mix claim.

M2A remains: `PARTIAL — MANUAL FL STUDIO AND INSTALLER VALIDATION POSTPONED`.

---

## A. Commits and CI

Branch `cursor/mastering-audio-932f`. Key commits:
- Adaptive Mix Pass + analysis/resolver/rider (`1d0f630`+)
- Scenario tests + `metalcore_engine_validation` + CI (`90a8acf`+)
- Suite StemEngine: section automation, vocal rider, true RAW/AUTO/CURRENT render modes

CI runs `[milestone3b]`, long synthetic engine validation, and continues Suite/Analyzer/Mix Node/installer/ZIP packaging.

## B. M3A truth audit

See `docs/M3A_TRUTH_AUDIT.md`. M3A had real DynEQ substrate but hardcoded 65/70 Hz, snare-as-drum-bus proxy, crest→compressor “riding”, always-on snare unmask, and AUTO≡CURRENT.

## C. Adaptive frequency detection

`MetalcoreAnalysis` uses Goertzel peak picking / prominence in role-specific bands on actual sample buffers (Suite loads up to 60 s per stem on Mix Pass generate). Kick/bass detector & target Hz come from detected regions + sub-ownership overlap — not fixed 65/70 unless explicit synthetic fallback flag.

## D. Kick/bass ownership

`decideSubOwnership` decision tree with `decisionTrace`: Kick owns sub / Bass owns sub / Shared but separated / Uncertain. DynEQ Action only when evidence sufficient; includes detected regions, overlap, detector/target Q, max cut, attack/release, evidence score.

## E. Guitar L/R detectors

Separate mud / harsh resonance / fizz detectors. Processing-level resolver chooses track-left, track-right, or pair. Right-only harshness does not force Guitar Bus cut.

## F. Vocal chain

Real stages as Actions/DSP:
- `vocalRider` → `dsp::VocalRider` slow trajectory (not compressor)
- `compressor` level stage
- `compressorPeak` when needed
- `vocalDeEss` / `vocalResonance` as DynEQ states
- Clean vs scream different targets/guards

## G. Drum chain

`drumBusUnavailable` advisory when no bus audio — **does not** apply bus glue to snare. Glue/parallel only when bus target exists.

## H. Unmasking

Vocal/guitar gated by vocal activity. Snare/guitar event-oriented (short attack). Cumulative unmask guardrail via `ActionResolver` (max combined ~7 dB).

## I. Processing-level resolver

`processingLevelFor` + Action fields `processingLevel` / `decisionTrace`.

## J. Action conflict resolution

`ActionResolver`: deterministic stage order, same-parameter reject, static-vs-dynamic sequencing, cumulative unmask reduce. Conflict report in `action_conflicts.json`.

## K. Section automation

`SectionAutomation` evaluates gain offsets with crossfades. StemEngine playback + offline render apply offsets. Save/reopen via mixPassActions `sectionScope` + schema v4 fields.

## L. Reference Profile v2

`ReferenceProfile` roles: overall, low-end, vocal-balance, etc. Loudness-matched comparison assumption documented; no blind EQ copy.

## M. RAW / AUTO / CURRENT renders

StemEngine:
- **RAW**: dry stems, no processor/DynEQ/rider/master safety
- **AUTO/CURRENT**: full chain + section automation + safety
- **REF**: reference monitor with LUFS match gain

Validation tool writes core-path RAW/AUTO sums + loudness-matched AUTO (RMS/integrated-style match gain reported). Not labeled “proxy” when full path is used.

## N. Long synthetic benchmark

`metalcore_engine_validation`: ≥30 s session with sections and intentional conflicts. Artifacts under `artifacts/metalcore_engine_validation/`.

## O. Realtime / offline consistency

Shared DynEQ + ProcessorChain + VocalRider + SectionAutomation evaluation between Suite realtime and offline render. Mix Node FL manual still postponed; core processor unit tests remain.

## P. Performance

Validation report includes stem count, section count, action count, timing / realtime-factor estimate on synthetic session.

## Q. Artifacts

```
artifacts/metalcore_engine_validation/
  raw.wav, auto.wav, loudness_matched/
  action_graph.json, action_conflicts.json, hierarchy.json
  section_automation.json, masking_before_after.json
  reference_profile.json, render_hashes.json, validation.md
```

## R. Remaining limitations

- Adaptive analysis is deterministic signal processing, not ML and not a guarantee of a release-ready metalcore mix.
- Parallel compression is bounded Action encoding where a dedicated wet/dry bus path is still limited by ProcessorChain.
- Width/stereo processors remain placeholders where noted.
- Real-session listening required before any musical quality claim.
- FL / installer still not manually verified.

## S. Gate

**MILESTONE 3B ACCEPTED**
