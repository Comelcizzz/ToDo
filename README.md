# Mastering Audio Suite

Local Windows-first metalcore mixing workflow:

1. Record and arrange in FL Studio.
2. Insert **Mastering Audio Analyzer** (VST3) on mixer tracks/buses and assign roles.
3. Export Split Mixer Tracks.
4. Open **Mastering Audio Suite**, import stems, review explainable mix actions, then export a WAV.

Everything runs offline. No accounts, no cloud upload.

## Honest capability notes

- The Analyzer publishes validity-gated **Momentary / Short-term / Integrated LUFS** and **True Peak** from a shared BS.1770-style engine (Milestone 1A). Official EBU vectors are fetched locally (not committed).
- For **finite** audio, Analyzer processing is bit-transparent. NaN/Inf are sanitized to 0.
- MixAdvisor **Apply** sets absolute Action targets (idempotent). **Reject** is pending-only; it does not undo an Apply.
- The current limiter is **not** a fully oversampled true-peak limiter yet (Milestone 1B).
- Portable ZIP packaging is **not** an installer. ML under `ml/` is a research CLI, not an ML Lab GUI.

## Products

| Target | Artifact | Role |
|---|---|---|
| VST3 analyzer | `Mastering Audio Analyzer.vst3` | Pass-through metering, role tagging, local bridge report |
| Desktop suite | `Mastering Audio Suite.exe` | Stem mixer, explainable actions, reference A/B, master render |

Shared C++ core modules:

- `modules/audio-analysis` — analysis metrics (see honest notes above)
- `modules/dsp` — EQ / compressor / saturation / clipper; approximate limiter tools
- `modules/assistant` — role-aware mix plans with absolute Action targets
- `modules/project-bridge` — `.masuite` schema v2
- `modules/ipc` — bridge payload validation
- `modules/research-export` + `ml/` — private metadata export and offline ML research

## Quick start (developer)

```bash
npm ci
npm run build
CXX=g++ cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Windows packaging: [docs/PACKAGING.md](docs/PACKAGING.md).  
FL workflow: [docs/FL_STUDIO_WORKFLOW.md](docs/FL_STUDIO_WORKFLOW.md).  
Architecture / status: [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md), [docs/FEATURE_STATUS.md](docs/FEATURE_STATUS.md).  
ML research: [docs/ML_RESEARCH.md](docs/ML_RESEARCH.md).

## Licensing

- Application code: GPL-3.0-or-later — see [LICENSE](LICENSE)
- JUCE 8 requires a valid [JUCE licence](https://juce.com/legal/juce-8-licence/) for closed-source distribution
- VST3 SDK (via JUCE) terms apply
