# M3B Verification Audit (pre-M3C)

Brutal check of M3B claims vs code before Milestone 3C.

| Capability | Claimed in M3B | Actual implementation | Test evidence | Remaining limitation |
|---|---|---|---|---|
| Full-track analysis | Up to 60s Suite load | Cap `min(length, 60s)` in `generateMetalcoreMixPass`; Goertzel ≤8s | M3B synthetic 32s | Not full song; silent 60s limit |
| Kick event detection | Event-ish evidence | Coarse envelope rise counter | Scenario A freqs | No onset times / false-positive reject |
| Bass note variation | Adaptive F0 | Single dominant peak | Scenario A | No occupancy ridge / note distribution |
| Section-specific freqs | Implied | One global analysis map | Manual alt maps in tests | No per-section Suite analysis |
| Evidence score | Deterministic | Fixed weights; sectionConsistency=1 | Labels tested | Components not exposed in Action |
| Safe ranges | Caps fields | Often overridden literals | Serialization | No derivation trace in UI/Action |
| Section automation | Multi-param API | **gainDb only** applied | SectionAutomation tests | DynEQ/comp/sat/wet/width unwired |
| Reference Profile v2 | Multi-role | Same metrics, role labels | Thin | Ratios often 0; not audio v3 |
| Loudness matching | Documented | REF=LUFS; validation=RMS | Validation reports | Production AUTO/RAW not LUFS-matched |
| RAW render | True dry | StemEngine skips FX | CompareMode | OK |
| AUTO render | Full auto | **Same DSP as CURRENT** | — | AUTO label ≠ apply-all |
| CURRENT render | User state | Processed path | — | OK after Apply |
| Parallel drum | When bus exists | Proxy second compressor | No wet/dry | Not latency-aligned parallel |
| Stereo width | Placeholder noted | Missing | — | No M/S processor |
| RT/offline consistency | Shared classes | Likely | Untested as system | No delta report |
| ActionResolver limits | 7 dB unmask | Implemented narrow | Scenario F | Only unmask neighborhood |
| Action budget | Cap fields | Cosmetic | — | No count/priority budget |

## Detail rows

| Item | File | Class/function | Algorithm | DSP target | Tests | Status |
|---|---|---|---|---|---|---|
| 60s cap | `MainComponent.cpp` | `generateMetalcoreMixPass` | `min(len, sr*60)` | Analysis only | — | **Limit to remove in M3C** |
| Goertzel 8s | `MetalcoreAnalysis.cpp` | `toneMagnitudeDb` | First 8s | Peaks | M3B | **Extend / chunk** |
| Kick events | `countTransients` | Envelope | Count | Evidence | Weak | **Replace with onsets** |
| Bass occupancy | `analyzeBassLow` | Peak pick | Dominant Hz | Kick/bass DynEQ | Partial | **Occupancy profile** |
| Section auto | `SectionAutomation` | `fromActions` | gain only | StemEngine gain | Unit | **Multi-param** |
| Parallel | MixPass Action | makeup compressor | Serial | Bus/child | — | **Real dry/wet** |
| Width | — | — | — | — | — | **Missing** |
| LUFS match | `recalculateReferenceGain` | LUFS delta | REF monitor | — | REF OK | **RAW/AUTO/CURRENT** |
| Resolver | `ActionResolver::resolve` | Cumulative cut | Guitar unmask | Scenario F | Narrow | **Freq-overlap + budget** |

## Mandatory honesty

- Synthetic validation ≠ musical quality proof.
- FL Studio / installer manual validation postponed.
- No ML Lab.
- No universal perfect mix claim.

M3C closes the gaps above with streaming analysis, real parallel/width, LUFS comparison, multi-param automation, and render identity.
