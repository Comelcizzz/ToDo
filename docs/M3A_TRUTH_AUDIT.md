# M3A Truth Audit (pre-M3B)

Honest classification of Milestone 3A capabilities.  
`IMPLEMENTED` in M3A docs meant “action objects + DSP wiring exist,” **not** adaptive musical detection.

| M3A capability | Real DSP | Heuristic | Proxy | Hardcoded | Missing |
|---|---:|---:|---:|---:|---:|
| Kick fundamental detection | | | | ✓ | |
| Bass fundamental detection | | | | ✓ | |
| Kick/bass masking gate | | ✓ | | | |
| Kick/bass DynEQ ducking (once applied) | ✓ | | | params | |
| Complementary EQ | | | | ✓ | |
| Guitar mud detection | | ✓ | | | |
| Guitar harshness detection | | ✓ | | | |
| Guitar fizz detection | | | ✓ (alias of harshness) | | |
| Vocal riding | | | ✓ (crest→one compressor) | | |
| Serial vocal compression | | | ✓ (single stage) | | |
| De-essing | | | | | ✓ |
| Vocal resonance control | | | | | ✓ |
| Vocal/guitar unmasking | ✓ (DynEQ) | gate | | freqs | |
| Snare/guitar unmasking | ✓ (DynEQ) | | | always-on + freqs | |
| Drum-bus compression | | | ✓ (applied to snare) | | |
| Parallel compression | | | | | ✓ |
| Section offsets | | | ✓ (metadata; Apply global) | +1 dB | |
| Reference comparison | | | ✓ (`refVocalToBed=0`) | | |
| RAW monitor (Suite) | ✓ | | | | |
| AUTO monitor (Suite) | | | ✓ (= CURRENT) | | |
| Loudness matching (REF) | ✓ | | | | |
| Loudness matching (Mix Pass claim) | | | ✓ | | |
| Save/reopen schema | ✓ | | | | |
| QC (export technical) | ✓ | | | | |
| Confidence values | | | | ✓ | |
| Action conflict resolution | | | | | ✓ |
| Processing-level resolver | | | ✓ (pair mirror if) | | |
| Section automation in render | | | | | ✓ |
| Deterministic Action order | | | | push-order | |

## Detail rows (file / algorithm / limitation)

| Item | File | Class / function | Algorithm | DSP target | Test | Limitation |
|---|---|---|---|---|---|---|
| Kick F0 | `MetalcoreMixPass.cpp` `generateActions` | Fixed detector 65 Hz / target 70 Hz | Bass DynEQ | `[milestone3a]` shape | No F0 estimator |
| Bass F0 | same | Uses `spectrum.subDb` only | same | same | Band energy ≠ fundamental |
| Masking | same | `\|subKick−subBass\|` gate + clamp cut | DynEQ maxCut | emission | Not time-aligned masking |
| Complementary EQ | `kickBassComplementaryEq` | +1.5 @ 4 kHz, −0.5 shelf | Kick static EQ | none specific | Fixed recipe |
| FD ducking | `DynamicEqProcessor` + StemEngine | Real GR from SC detector | Bass / guitars | M1C + M3A | Params from Mix Pass hardcoded |
| Mud | `guitarLowMidMud` | `lowMidDb > −18` → shelf −2 @ 220 | Guitar EQ | none | Fixed band/cut |
| Harshness | `guitarHarshness` | `airDb > −12` → shelf −1.5 @ 8k | Guitar EQ | none | HF energy only |
| Fizz | copy text only | Same as harshness | same | none | Not separate |
| Vocal ride | `vocalRiding` | crest > 16 → one compressor | Vocal compressor | none | Not a ride trajectory |
| Serial comp | same | One stage | same | none | Not serial |
| De-ess / resonance | — | — | — | — | Missing |
| Vocal unmask | `vocalGuitarUnmask` | presence delta < 3; DynEQ 2.8k/3k | Guitar DynEQ | emission | Fixed carve |
| Snare unmask | `snareGuitarUnmask` | Always if roles present; 2.2k/2.4k | Guitar DynEQ | emission | No conflict gate |
| Drum bus glue | `drumBusGlue` | Comp on **snare** stem | Snare, not bus | none | Explicit proxy |
| Parallel | — | — | — | — | Missing |
| Section offset | `sectionVocalLevel` | +1 dB chorus; scope id only | Global gain | none | Not time-rendered |
| Reference | `referenceVocalToBed` | `refVocalToBed = 0.0` | Vocal gain | none | Placeholder |
| RAW Suite | `StemEngine` compare raw | Skip processor/DynEQ | Playback | validation WAV | `renderMaster` no RAW flag |
| AUTO Suite | `CompareMode::autoProcessed` | Same path as current | — | — | Not auto-apply |
| Save/reopen | `ProjectDocument` schema v3 | JSON round-trip | File | `[milestone3a]` | Section auto not in DSP |
| QC | `ExportQc` | TP/ceiling/NaN | Master export | M1B | Technical only |
| Confidence | `makeAction` | Literals 0.55–0.80 | Metadata | none | Uncalibrated |
| Conflicts | — | Last Apply wins | — | — | Missing |
| Level resolver | `applyAction` pair mirror | Hardcoded problem list | Pair tracks | hierarchy | No bus DSP |
| Section render | — | `sectionScope` ignored | — | — | Missing |

## Rule for M3B

Do **not** call a capability “real detection” unless frequencies/thresholds are derived from audio evidence with a decision trace. DynEQ wiring alone is Real DSP substrate, not adaptive Mix Pass quality.

Synthetic validation remains **not** proof of professional musical quality.  
FL Studio / installer manual validation remain postponed.  
ML Lab not started.
