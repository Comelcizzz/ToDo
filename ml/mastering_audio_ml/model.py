from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable

import joblib
import numpy as np
from sklearn.feature_extraction import DictVectorizer
from sklearn.linear_model import Ridge
from sklearn.pipeline import Pipeline
from sklearn.preprocessing import StandardScaler

from .dataset import NUMERIC_FEATURES, TARGETS, TARGET_BOUNDS

MODEL_FORMAT_VERSION = 1


def _features(row: dict[str, Any]) -> dict[str, float | str]:
    result: dict[str, float | str] = {
        "role": str(row["role"]),
        "selectedVariant": str(row.get("selectedVariant", "balanced")),
    }
    result.update({name: float(row[name]) for name in NUMERIC_FEATURES})
    return result


@dataclass(frozen=True)
class FeatureInfluence:
    feature: str
    coefficient: float


class ExplainableMixModel:
    """Multi-output ridge model with inspectable global feature coefficients."""

    def __init__(self, alpha: float = 4.0) -> None:
        self.alpha = alpha
        self.pipeline = Pipeline(
            [
                ("vectorizer", DictVectorizer(sparse=True)),
                ("scaler", StandardScaler(with_mean=False)),
                ("regressor", Ridge(alpha=alpha)),
            ]
        )

    def fit(self, rows: Iterable[dict[str, Any]]) -> "ExplainableMixModel":
        materialized = list(rows)
        if not materialized:
            raise ValueError("cannot train on an empty dataset")
        features = [_features(row) for row in materialized]
        targets = np.asarray(
            [[float(row[target]) for target in TARGETS] for row in materialized],
            dtype=np.float64,
        )
        self.pipeline.fit(features, targets)
        return self

    def predict(self, rows: Iterable[dict[str, Any]]) -> list[dict[str, float]]:
        materialized = list(rows)
        if not materialized:
            return []
        predictions = self.pipeline.predict([_features(row) for row in materialized])
        result: list[dict[str, float]] = []
        for prediction in np.atleast_2d(predictions):
            bounded: dict[str, float] = {}
            for target, value in zip(TARGETS, prediction, strict=True):
                lower, upper = TARGET_BOUNDS[target]
                bounded[target] = float(np.clip(value, lower, upper))
            result.append(bounded)
        return result

    def top_influences(
        self, target: str, limit: int = 8
    ) -> list[FeatureInfluence]:
        if target not in TARGETS:
            raise ValueError(f"unknown target: {target}")
        vectorizer: DictVectorizer = self.pipeline.named_steps["vectorizer"]
        regressor: Ridge = self.pipeline.named_steps["regressor"]
        coefficients = np.asarray(regressor.coef_)[TARGETS.index(target)]
        names = vectorizer.get_feature_names_out()
        order = np.argsort(np.abs(coefficients))[::-1][:limit]
        return [
            FeatureInfluence(str(names[index]), float(coefficients[index]))
            for index in order
        ]

    def save(self, destination: str | Path) -> None:
        destination = Path(destination)
        destination.parent.mkdir(parents=True, exist_ok=True)
        joblib.dump(
            {
                "formatVersion": MODEL_FORMAT_VERSION,
                "targets": TARGETS,
                "numericFeatures": NUMERIC_FEATURES,
                "alpha": self.alpha,
                "pipeline": self.pipeline,
            },
            destination,
        )

    @classmethod
    def load(cls, source: str | Path) -> "ExplainableMixModel":
        payload = joblib.load(source)
        if payload.get("formatVersion") != MODEL_FORMAT_VERSION:
            raise ValueError("unsupported model format")
        if payload.get("targets") != TARGETS:
            raise ValueError("model targets do not match this pipeline")
        model = cls(alpha=float(payload["alpha"]))
        model.pipeline = payload["pipeline"]
        return model
