# M4A Linux / Windows deterministic comparison

**Scope:** synthetic Action Graph / profile / metrics comparison.  
**Not:** bit-identical audio, musical quality, or personal stems.

## Policy

| Component | Expectation |
|---|---|
| Action Graph (problem/target/processor/amount order) | Equal across Linux/Windows for same engine+profile+manifest |
| Profile interpretation | Equal after hard-cap clamp |
| Analysis summaries (categorical) | Equal |
| Render duration (samples) | Equal for identical graphs |
| Objective metrics (LUFS/TP/crest) | Small float tolerance allowed |
| WAV sample delta | Platform float tolerance; **not** required bit-identical |

## Template report

| Component | Linux | Windows | Delta | Result |
|---|---|---|---|---|
| Action Graph JSON | (CI tip) | (CI tip) | structural | PASS if equal |
| Profile schema/revision | 1 / balanced@1 | 1 / balanced@1 | 0 | PASS |
| Action count | (run) | (run) | 0 | PASS if equal |
| Deterministic rerun | true | true | — | PASS |
| Render duration | (samples) | (samples) | 0 | PASS if equal |
| LUFS-I | (value) | (value) | ≤0.05 LU | PASS if within tol |
| True peak | (value) | (value) | ≤0.1 dB | PASS if within tol |
| WAV max abs delta | — | — | documented | INFO (not bit-identical) |

Fill this table from Linux/Windows CI `[milestone4a]` + synthetic validation artifacts on each tip green run.

## Reproducibility

Experiment runs store engine version (`0.4.0-alpha.m4a+<sha>`), profile ID/revision,
asset fingerprints, Action Graph, seeds, and SHA-256 of reports. Rerunning the same
inputs must yield a matching Action Graph (deterministic Action IDs).
