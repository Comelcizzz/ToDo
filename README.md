# Mastering Audio Suite

Local Windows-first metalcore mastering workflow:

1. Record and arrange in FL Studio.
2. Insert **Mastering Audio Analyzer** (VST3) on mixer buses and assign roles.
3. Export Split Mixer Tracks.
4. Open **Mastering Audio Suite**, import stems, analyze, apply bounded auto-mix, then export a mastered WAV.

Everything runs offline. No accounts, no cloud upload.

## Products

| Target | Artifact | Role |
|---|---|---|
| VST3 analyzer | `Mastering Audio Analyzer.vst3` | Pass-through metering, role tagging, local bridge report |
| Desktop suite | `Mastering Audio Suite.exe` | Stem mixer, explainable auto-mix, reference A/B, master render |

Shared C++ core modules:

- `modules/audio-analysis` — LUFS, peak/true-peak estimate, crest, spectrum, stereo, transients
- `modules/dsp` — EQ / compressor / saturation / clipper chain with amount blend
- `modules/assistant` — role-aware explainable mix plan with guardrails
- `modules/project-bridge` — `.masuite` project schema and stem role inference

## Quick start (developer)

```bash
npm ci
npm run build
CXX=g++ cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Windows release packaging is described in [docs/PACKAGING.md](docs/PACKAGING.md).
FL Studio routing/export steps are in [docs/FL_STUDIO_WORKFLOW.md](docs/FL_STUDIO_WORKFLOW.md).

## Licensing

- Application code: GPL-3.0-or-later for this repository snapshot
- JUCE 8 is fetched by CMake and requires a valid [JUCE licence](https://juce.com/legal/juce-8-licence/) for closed-source distribution
- VST3 SDK (via JUCE) is MIT-licensed
