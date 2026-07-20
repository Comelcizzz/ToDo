# ML research system

The ML subsystem studies user-approved mixes and measures whether learned
relationships can improve the deterministic `MixAdvisor`. It does not run on
the audio thread and does not replace production DSP.

## Privacy and dataset

In the desktop app, finish a mix and select **Export ML example**. Export is
always explicit. The JSON contains:

- anonymized project fingerprint and track roles;
- loudness, peak, spectrum, transient, and stereo metrics;
- the selected mix variant;
- user-approved gain, pan, EQ, compressor, saturation, and Amount settings.

It never contains audio, project/track names, IDs, or file paths. The exporter
and Python loader both enforce these constraints. Review a file before sharing
it; the dataset remains local unless you choose to move it.

An export is a training label only when the settings represent an approved
mix. Exporting arbitrary or unfinished settings will reduce model quality.

## Training

Use a separate Python environment:

```bash
python -m venv .venv-ml
source .venv-ml/bin/activate       # Windows: .venv-ml\Scripts\activate
python -m pip install ./ml

mastering-audio-train research-data/ \
  --model-out artifacts/mix-model.joblib \
  --report-out artifacts/evaluation.json
```

Training requires at least 20 approved tracks from four projects. Splits are
grouped by project, so stems from one song cannot appear in both training and
holdout sets. The evaluation report compares every target with a role-median
baseline and lists the strongest model coefficients.

Generate an offline research recommendation:

```bash
mastering-audio-predict new-mix.research.json \
  --model artifacts/mix-model.joblib \
  --output artifacts/recommendations.json
```

Predictions are clipped to production-safe ranges. They are research output,
not automatically applied to a project.

## Promotion criteria

A learned relationship may change `MixAdvisor` only after:

1. beating the role-median and existing rule-based baselines on unseen projects;
2. passing mono, true-peak, transient, and render regression checks;
3. improving blinded, loudness-matched listening tests;
4. remaining explainable by role and measured audio features;
5. being validated on properly licensed stems from the target genre.

The initial model is multi-output ridge regression. This is intentional:
coefficients are inspectable, training is reproducible, and a more complex
model must demonstrate a real holdout advantage before adoption.

## Limitations

- No trained model is shipped because the repository contains no licensed
  professional stem-to-mix dataset.
- User-approved settings are preferences, not objective ground truth.
- Metrics cannot capture every timing, arrangement, or timbral decision.
- A model trained only on metalcore should not be treated as genre-independent.
