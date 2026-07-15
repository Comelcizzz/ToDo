# Personal benchmark library (local-only)

**Do not commit copyrighted stems, references, or renders.**

## Layout

```
benchmarks/
  synthetic/          # tracked readiness docs / non-copyrighted fixtures
  profiles/           # tracked MetalcoreProfile JSON (balanced / aggressive / custom)
  experiments/        # tracked .gitkeep only
  personal/           # LOCAL-ONLY (gitignored session content)
    sessions/
      <session-id>/
        manifest.json
        stems/
        references/
        targets/
        renders/
        reports/
        listening/
        cache/
    README.md
    manifest.json     # optional workspace stub (tracked)
```

Personal audio paths stay on disk under the Suite local-data folder
(`~/…/MasteringAudioSuite/benchmarks/personal` on desktop) or this tree when used offline.

## Rules

- `localOnly` is always true for personal sessions.
- Personal audio is excluded from CI, diagnostics, crash reports, and installer packages.
- Expected-problem annotations are evaluation-only and never fed into Action generation.
- Guitar doubles are never auto time/phase aligned.
- Musical quality is **not** proven by synthetic CI.

Drop local WAV/AIFF stems into a session folder, run Import wizard check in Suite, then save a versioned `BenchmarkSessionManifest`.
