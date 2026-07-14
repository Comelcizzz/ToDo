from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Sequence

from .dataset import TARGETS, DatasetError, load_rows
from .model import ExplainableMixModel


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Generate bounded research recommendations from an exported example."
    )
    parser.add_argument("inputs", nargs="+", help="Research JSON/JSONL files")
    parser.add_argument("--model", required=True, help="Trained .joblib model")
    parser.add_argument("--output", required=True, help="Recommendation JSON output")
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    arguments = _parser().parse_args(argv)
    try:
        rows = load_rows(arguments.inputs)
    except DatasetError as error:
        raise SystemExit(f"dataset error: {error}") from error

    model = ExplainableMixModel.load(arguments.model)
    predictions = model.predict(rows)
    recommendations = []
    for row, prediction in zip(rows, predictions, strict=True):
        recommendations.append(
            {
                "anonymousProjectId": row["projectId"],
                "trackIndex": row["trackIndex"],
                "role": row["role"],
                "predictedSettings": prediction,
                "explanation": {
                    target: [
                        influence.feature
                        for influence in model.top_influences(target, limit=3)
                    ]
                    for target in TARGETS
                },
            }
        )

    payload = {
        "schemaVersion": 1,
        "kind": "mastering-audio-ml-recommendations",
        "researchOnly": True,
        "recommendations": recommendations,
    }
    destination = Path(arguments.output)
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(json.dumps(payload, indent=2), encoding="utf-8")
    print(f"wrote {len(recommendations)} recommendations to {destination}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
