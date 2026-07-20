from __future__ import annotations

import argparse
import json
import statistics
from pathlib import Path
from typing import Any, Sequence

import numpy as np
from sklearn.metrics import mean_absolute_error
from sklearn.model_selection import GroupShuffleSplit

from .dataset import TARGETS, DatasetError, load_rows
from .model import ExplainableMixModel

MINIMUM_TRACKS = 20
MINIMUM_PROJECTS = 4


def _baseline_predictions(
    training_rows: list[dict[str, Any]],
    evaluation_rows: list[dict[str, Any]],
) -> np.ndarray:
    global_medians = {
        target: statistics.median(float(row[target]) for row in training_rows)
        for target in TARGETS
    }
    role_medians: dict[str, dict[str, float]] = {}
    for role in {str(row["role"]) for row in training_rows}:
        role_rows = [row for row in training_rows if str(row["role"]) == role]
        role_medians[role] = {
            target: statistics.median(float(row[target]) for row in role_rows)
            for target in TARGETS
        }
    return np.asarray(
        [
            [
                role_medians.get(str(row["role"]), global_medians)[target]
                for target in TARGETS
            ]
            for row in evaluation_rows
        ],
        dtype=np.float64,
    )


def train_and_evaluate(
    rows: list[dict[str, Any]],
    model_path: str | Path,
    report_path: str | Path,
    *,
    alpha: float = 4.0,
    random_state: int = 932,
) -> dict[str, Any]:
    groups = np.asarray([str(row["projectId"]) for row in rows])
    unique_groups = sorted(set(groups))
    if len(rows) < MINIMUM_TRACKS:
        raise DatasetError(
            f"at least {MINIMUM_TRACKS} approved tracks are required; got {len(rows)}"
        )
    if len(unique_groups) < MINIMUM_PROJECTS:
        raise DatasetError(
            f"at least {MINIMUM_PROJECTS} projects are required; got {len(unique_groups)}"
        )

    splitter = GroupShuffleSplit(
        n_splits=1, test_size=0.25, random_state=random_state
    )
    training_indices, test_indices = next(splitter.split(rows, groups=groups))
    training_rows = [rows[index] for index in training_indices]
    test_rows = [rows[index] for index in test_indices]

    evaluation_model = ExplainableMixModel(alpha=alpha).fit(training_rows)
    predicted = evaluation_model.predict(test_rows)
    predicted_matrix = np.asarray(
        [[prediction[target] for target in TARGETS] for prediction in predicted]
    )
    expected = np.asarray(
        [[float(row[target]) for target in TARGETS] for row in test_rows]
    )
    baseline = _baseline_predictions(training_rows, test_rows)

    target_metrics: dict[str, Any] = {}
    for index, target in enumerate(TARGETS):
        model_mae = float(mean_absolute_error(expected[:, index], predicted_matrix[:, index]))
        baseline_mae = float(mean_absolute_error(expected[:, index], baseline[:, index]))
        target_metrics[target] = {
            "modelMae": model_mae,
            "roleMedianBaselineMae": baseline_mae,
            "improvementOverBaseline": baseline_mae - model_mae,
            "topInfluences": [
                {
                    "feature": influence.feature,
                    "coefficient": influence.coefficient,
                }
                for influence in evaluation_model.top_influences(target)
            ],
        }

    report = {
        "schemaVersion": 1,
        "modelType": "multi-output-ridge",
        "guardrailPolicy": "all predictions are clipped to production-safe ranges",
        "dataset": {
            "tracks": len(rows),
            "projects": len(unique_groups),
            "trainingTracks": len(training_rows),
            "holdoutTracks": len(test_rows),
            "holdoutProjects": len(set(groups[test_indices])),
        },
        "targets": target_metrics,
        "limitations": [
            "This model learns user-approved settings, not objective audio quality.",
            "A project-grouped holdout prevents tracks from one song leaking across splits.",
            "Blind listening tests are required before changing production rules.",
        ],
    }

    final_model = ExplainableMixModel(alpha=alpha).fit(rows)
    final_model.save(model_path)
    report_path = Path(report_path)
    report_path.parent.mkdir(parents=True, exist_ok=True)
    report_path.write_text(json.dumps(report, indent=2), encoding="utf-8")
    return report


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Train an explainable model from approved metadata-only mix examples."
    )
    parser.add_argument("inputs", nargs="+", help="Research JSON/JSONL files or directories")
    parser.add_argument("--model-out", required=True, help="Destination .joblib file")
    parser.add_argument("--report-out", required=True, help="Destination evaluation JSON")
    parser.add_argument("--alpha", type=float, default=4.0, help="Ridge regularization")
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    arguments = _parser().parse_args(argv)
    try:
        rows = load_rows(arguments.inputs)
        report = train_and_evaluate(
            rows,
            arguments.model_out,
            arguments.report_out,
            alpha=arguments.alpha,
        )
    except DatasetError as error:
        raise SystemExit(f"dataset error: {error}") from error
    print(json.dumps(report["dataset"], indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
