# Feature status

Statuses: `IMPLEMENTED` | `PARTIAL` | `STUB` | `MOCK` | `MISSING` | `SCAFFOLD` | `NOT VERIFIED IN FL`

## Scope

| Layout | Status |
|---|---|
| Mono / stereo | In scope |
| Multichannel / 5.1 / surround | **OUT OF SCOPE / MISSING** |

## Milestone 1A — Metering

Mono/stereo LUFS / TP / LRA: **IMPLEMENTED**. Multichannel OUT OF SCOPE.

## Milestone 1B — Master safety DSP

Oversampler + sat/soft/hard + LA TP limiter + Export QC: **IMPLEMENTED**. **M1B ACCEPTED**.

## Milestone 1C — Dynamic EQ / FD sidechain

Dynamic EQ + FD sidechain + detector: **IMPLEMENTED**. **M1C ACCEPTED**.

## Milestone 2A — Mix Node VST3 + Suite + installer

| Feature | Status | Notes |
|---|---|---|
| Separate Mix Node VST3 target | **IMPLEMENTED** | `Mastering Audio Mix Node.vst3` |
| Analyzer remains analysis-only | **IMPLEMENTED** | zero-latency pass-through |
| Chain: In→Static EQ→DynEQ→Sat→Out | **IMPLEMENTED** | shared `MixNodeChain` |
| Host sidechain bus | **IMPLEMENTED** | mono/stereo SC layouts |
| Versioned identity + role presets | **IMPLEMENTED** | |
| Action protocol preview/commit/cancel/undo | **IMPLEMENTED** | unit tested |
| Host state persistence (no preview) | **IMPLEMENTED** | unit tested |
| Suite Mix Nodes panel | **IMPLEMENTED** | developer UI |
| Mix Node plugin UI | **IMPLEMENTED** | compact WebView |
| Automation parameters | **IMPLEMENTED** | APVTS |
| Early Inno Setup installer | **PARTIAL** | script + CI attempt; **NOT MANUALLY VERIFIED** |
| Portable ZIP includes Analyzer + Mix Node | **IMPLEMENTED** | packaging script |
| FL Studio manual checklist | **IMPLEMENTED** doc | all rows NOT TESTED |
| FL Studio end-to-end | **NOT VERIFIED IN FL** | postponed; does not block M3A |
| ML Lab in installer | **MISSING** | intentionally excluded |
| M/S processing | **SCAFFOLD / MISSING** | |

**Gate:** `MILESTONE 2A REMAINS PARTIAL — AUTOMATED IMPLEMENTATION PASSED, MANUAL FL STUDIO AND INSTALLER VALIDATION POSTPONED`

## Milestone 3A — Standalone Metalcore Mix Pass V1

| Feature | Status | Notes |
|---|---|---|
| Stem import WAV/AIFF | **IMPLEMENTED** | Suite desktop |
| Track / pair / bus hierarchy | **IMPLEMENTED** | |
| Rhythm Guitar L/R + pair | **IMPLEMENTED** | no auto align |
| BPM + manual sections | **IMPLEMENTED** | |
| Actionable MixPassAction | **IMPLEMENTED** | |
| Save/reopen schema | **IMPLEMENTED** | v3→v4 in M3B |
| Synthetic M3A validation | **IMPLEMENTED** | not musical proof |
| Detection quality | **HEURISTIC / HARDCODED** | see `docs/M3A_TRUTH_AUDIT.md` |

**Gate:** **M3A ACCEPTED** as automated vertical slice (not professional mix quality).

## Milestone 3B — Adaptive Mix Engine + Action quality

| Feature | Status | Notes |
|---|---|---|
| M3A truth audit | **IMPLEMENTED** | `docs/M3A_TRUTH_AUDIT.md` |
| Adaptive kick/bass F0 peaks | **IMPLEMENTED** | Goertzel peaks; no fixed 65/70 production path |
| Sub ownership decision trace | **IMPLEMENTED** | |
| Complementary EQ evidence gate | **IMPLEMENTED** | prefer cut; no blind boost |
| Guitar mud/harsh/fizz detectors | **IMPLEMENTED** | separate |
| Processing-level resolver | **IMPLEMENTED** | track/pair/bus |
| ActionResolver conflicts + order | **IMPLEMENTED** | |
| VocalRider real DSP | **IMPLEMENTED** | |
| Serial/peak vocal stages | **IMPLEMENTED** | separate Actions |
| De-ess / resonance DynEQ | **IMPLEMENTED** | |
| Clean vs scream logic | **IMPLEMENTED** | |
| Vocal activity–gated unmask | **IMPLEMENTED** | |
| Event-based snare unmask | **IMPLEMENTED** | |
| No snare-as-drum-bus fallback | **IMPLEMENTED** | `drumBusUnavailable` |
| Section automation render | **IMPLEMENTED** | StemEngine + offline |
| Reference Profile v2 + roles | **IMPLEMENTED** | |
| True RAW/AUTO/CURRENT paths | **IMPLEMENTED** | Suite render modes |
| Loudness match documented | **IMPLEMENTED** | REF LUFS; validation RMS match |
| Long synthetic + scenarios A–G | **IMPLEMENTED** | `[milestone3b]` |
| ML Lab | **MISSING** | not started |
| FL / installer manual | **NOT VERIFIED** | postponed |

**Gate:** see `docs/M3B_EVIDENCE_REPORT.md`.

## Milestone 3C — Full-track streaming + DSP polish + evidence

| Feature | Status | Notes |
|---|---|---|
| StreamingAnalyzer full-track | **IMPLEMENTED** | chunked; cancel; layered cache fingerprint; ≤30 min |
| Kick/snare events + min spacing | **IMPLEMENTED** | heuristic; synthetic tests |
| Bass occupancy bins | **IMPLEMENTED** | 30–180 Hz |
| Section automation multi-param | **IMPLEMENTED** | dynMaxCutDb and related lanes |
| ParallelCompressor real wet/dry | **IMPLEMENTED** | 0 latency; validation AUTO bus |
| StereoWidth complementary M/S | **IMPLEMENTED** | complementary one-pole side split; unity/mono tests |
| LoudnessMatch LUFS policy | **IMPLEMENTED** | M1A LoudnessMeter integrated / short-term / bounded-RMS |
| ActionBudget | **IMPLEMENTED** | post-resolver; rejected conflicts not budgeted |
| EvidenceModel + safe ranges | **IMPLEMENTED** | Low/Medium/High labels |
| RenderIdentity graphId vs SHA-256 | **IMPLEMENTED** | FNV graph ID; SHA-256 artifact integrity |
| Risk-aware AUTO policy | **IMPLEMENTED** | not universal 0.45; musical → Preview |
| Suite RT/offline compare | **IMPLEMENTED** | StemEngine-equivalent DSP battery |
| 30-min truncation reporting | **IMPLEMENTED** | UI warn + evidence penalty + report fields |
| Blind A/B/C package | **IMPLEMENTED** | randomized; answer_key hidden |
| 90s metalcore_engine_validation | **IMPLEMENTED** | StreamingAnalyzer evidence |
| `[milestone3c]` tests | **IMPLEMENTED** | Catch2 + verification suite |
| Suite UI unlimited streaming | **IMPLEMENTED** | Mix Pass uses StreamingAnalyzer; ≤kMaxAnalysisSeconds |
| StemEngine parallel/width + LUFS A/B | **IMPLEMENTED** | compareMatchGainDb_ monitor makeup |
| Project schema v5 | **IMPLEMENTED** | parallel/width + budget/identity/truncation fields |
| Musical quality | **NOT CLAIMED** | synthetic ≠ mix proof |
| Final Linux/Windows CI gate | **ACCEPTED** | tip `fe82d2b` run `29437233765` green |
| ML Lab | **MISSING** | not started |
| FL / installer manual | **NOT VERIFIED** | postponed |

**Gate:** **MILESTONE 3C ACCEPTED** — `docs/M3C_FINAL_VERIFICATION.md` (CI tip `fe82d2b`, run `29437233765`).

## Manual validation checklist

Kept for a later full Metalcore Mix Pass manual pass: `docs/FL_MIX_NODE_VALIDATION.md`.
