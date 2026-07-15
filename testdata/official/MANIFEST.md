# Official loudness / LRA test material (not committed)

Sources (download locally; do not commit copyrighted WAV unless license allows):

| Source document | Purpose |
|---|---|
| ITU-R BS.1770 (current edition) | K-weighting, gating, true-peak Annex 2 |
| EBU Tech 3341 | Loudness test signals |
| EBU Tech 3342 | LRA test signals |
| EBU R 128 | Metering context |

Place verified files under this directory after running `scripts/fetch-loudness-testdata.sh` (Milestone 1A).

Per-vector report columns required by CI/docs:

| source document | vector/file ID | expected | tolerance | implementation | pass/fail | sha256 |

Synthetic repository tones remain additional regressions only.
