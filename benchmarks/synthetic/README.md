# Synthetic readiness fixture (non-copyrighted)

This folder holds **non-copyrighted** synthetic descriptions for CI and import
readiness smoke tests. Personal stems live under `benchmarks/personal/` (gitignored).

The import validator fixture is constructed in-process by
`makeReadinessFixtureManifest()` — it intentionally includes:

- mixed mono/stereo stems
- mismatched sample rates
- missing L/R pair partner
- shorter / silent stems
- unexpected start offsets
- reference + target mix paths
- expected-problem annotations (evaluation-only; never fed into generation)

Do not place copyrighted audio here.
