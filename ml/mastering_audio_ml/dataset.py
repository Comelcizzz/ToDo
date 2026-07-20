from __future__ import annotations

import json
import math
from pathlib import Path
from typing import Any, Iterable

KIND = "mastering-audio-research-example"

NUMERIC_FEATURES = [
    "trackCount",
    "sameRoleCount",
    "meanRmsDbfs",
    "meanIntegratedLufs",
    "relativeRmsDb",
    "samplePeakDbfs",
    "estimatedTruePeakDbtp",
    "rmsDbfs",
    "integratedLufs",
    "crestFactorDb",
    "stereoCorrelation",
    "transientDensityHz",
    "durationSeconds",
    "channels",
    "subDb",
    "bassDb",
    "lowMidDb",
    "midDb",
    "presenceDb",
    "airDb",
    "subVsBassDb",
    "bassVsMidDb",
    "presenceVsMidDb",
]

TARGETS = [
    "gainDb",
    "pan",
    "highPassHz",
    "lowShelfGainDb",
    "presenceGainDb",
    "highShelfGainDb",
    "compressorThresholdDb",
    "compressorRatio",
    "compressorAttackMs",
    "compressorReleaseMs",
    "saturation",
    "amount",
]

TARGET_BOUNDS = {
    "gainDb": (-18.0, 12.0),
    "pan": (-1.0, 1.0),
    "highPassHz": (15.0, 400.0),
    "lowShelfGainDb": (-6.0, 6.0),
    "presenceGainDb": (-6.0, 6.0),
    "highShelfGainDb": (-6.0, 6.0),
    "compressorThresholdDb": (-60.0, 0.0),
    "compressorRatio": (1.0, 20.0),
    "compressorAttackMs": (0.1, 200.0),
    "compressorReleaseMs": (5.0, 2_000.0),
    "saturation": (0.0, 1.0),
    "amount": (0.0, 1.0),
}


class DatasetError(ValueError):
    """Raised when research input violates the privacy or data schema."""


def _number(value: Any, field: str) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise DatasetError(f"{field} must be numeric")
    result = float(value)
    if not math.isfinite(result):
        raise DatasetError(f"{field} must be finite")
    return result


def _nested(source: dict[str, Any], *keys: str) -> Any:
    current: Any = source
    for key in keys:
        if not isinstance(current, dict) or key not in current:
            raise DatasetError(f"missing field: {'.'.join(keys)}")
        current = current[key]
    return current


def _validate_document(document: dict[str, Any]) -> None:
    if document.get("schemaVersion") != 1 or document.get("kind") != KIND:
        raise DatasetError("unsupported research example")
    privacy = document.get("privacy")
    if not isinstance(privacy, dict):
        raise DatasetError("privacy declaration is required")
    for field in ("audioIncluded", "pathsIncluded", "namesIncluded"):
        if privacy.get(field) is not False:
            raise DatasetError(f"privacy.{field} must be false")
    project = document.get("project")
    if not isinstance(project, dict) or not isinstance(project.get("tracks"), list):
        raise DatasetError("project.tracks must be an array")


def _flatten_document(document: dict[str, Any]) -> list[dict[str, Any]]:
    _validate_document(document)
    project = document["project"]
    if project.get("userApproved") is not True:
        return []

    tracks = project["tracks"]
    if not tracks:
        return []

    rms_values = [
        _number(_nested(track, "metrics", "rmsDbfs"), "metrics.rmsDbfs")
        for track in tracks
    ]
    lufs_values = [
        _number(_nested(track, "metrics", "integratedLufs"), "metrics.integratedLufs")
        for track in tracks
    ]
    mean_rms = sum(rms_values) / len(rms_values)
    mean_lufs = sum(lufs_values) / len(lufs_values)
    role_counts: dict[str, int] = {}
    for track in tracks:
        role = str(track.get("role", "custom"))
        role_counts[role] = role_counts.get(role, 0) + 1

    rows: list[dict[str, Any]] = []
    for index, track in enumerate(tracks):
        metrics = _nested(track, "metrics")
        spectrum = _nested(track, "metrics", "spectrum")
        settings = _nested(track, "approvedSettings")
        processing = _nested(track, "approvedSettings", "processing")
        equalizer = _nested(track, "approvedSettings", "processing", "equalizer")
        compressor = _nested(track, "approvedSettings", "processing", "compressor")

        role = str(track.get("role", "custom"))
        sub = _number(spectrum.get("subDb"), "spectrum.subDb")
        bass = _number(spectrum.get("bassDb"), "spectrum.bassDb")
        mid = _number(spectrum.get("midDb"), "spectrum.midDb")
        presence = _number(spectrum.get("presenceDb"), "spectrum.presenceDb")

        row: dict[str, Any] = {
            "projectId": str(project.get("anonymousId", "unknown")),
            "trackIndex": int(track.get("index", index)),
            "role": role,
            "selectedVariant": str(project.get("selectedVariant", "balanced")),
            "trackCount": float(len(tracks)),
            "sameRoleCount": float(role_counts[role]),
            "meanRmsDbfs": mean_rms,
            "meanIntegratedLufs": mean_lufs,
            "relativeRmsDb": rms_values[index] - mean_rms,
            "subVsBassDb": sub - bass,
            "bassVsMidDb": bass - mid,
            "presenceVsMidDb": presence - mid,
        }
        for field in (
            "samplePeakDbfs",
            "estimatedTruePeakDbtp",
            "rmsDbfs",
            "integratedLufs",
            "crestFactorDb",
            "stereoCorrelation",
            "transientDensityHz",
            "durationSeconds",
            "channels",
        ):
            row[field] = _number(metrics.get(field), f"metrics.{field}")
        for field in ("subDb", "bassDb", "lowMidDb", "midDb", "presenceDb", "airDb"):
            row[field] = _number(spectrum.get(field), f"spectrum.{field}")

        row["gainDb"] = _number(settings.get("gainDb"), "approvedSettings.gainDb")
        row["pan"] = _number(settings.get("pan"), "approvedSettings.pan")
        row["highPassHz"] = _number(equalizer.get("highPassHz"), "equalizer.highPassHz")
        row["lowShelfGainDb"] = _number(
            equalizer.get("lowShelfGainDb"), "equalizer.lowShelfGainDb"
        )
        row["presenceGainDb"] = _number(
            equalizer.get("presenceGainDb"), "equalizer.presenceGainDb"
        )
        row["highShelfGainDb"] = _number(
            equalizer.get("highShelfGainDb"), "equalizer.highShelfGainDb"
        )
        row["compressorThresholdDb"] = _number(
            compressor.get("thresholdDb"), "compressor.thresholdDb"
        )
        row["compressorRatio"] = _number(compressor.get("ratio"), "compressor.ratio")
        row["compressorAttackMs"] = _number(
            compressor.get("attackMs"), "compressor.attackMs"
        )
        row["compressorReleaseMs"] = _number(
            compressor.get("releaseMs"), "compressor.releaseMs"
        )
        row["saturation"] = _number(processing.get("saturation"), "processing.saturation")
        row["amount"] = _number(processing.get("amount"), "processing.amount")
        rows.append(row)
    return rows


def _documents_from_path(path: Path) -> Iterable[dict[str, Any]]:
    if path.is_dir():
        for child in sorted(path.rglob("*.json")):
            yield from _documents_from_path(child)
        for child in sorted(path.rglob("*.jsonl")):
            yield from _documents_from_path(child)
        return

    if path.suffix.lower() == ".jsonl":
        with path.open(encoding="utf-8") as source:
            for line_number, line in enumerate(source, 1):
                if line.strip():
                    try:
                        yield json.loads(line)
                    except json.JSONDecodeError as error:
                        raise DatasetError(f"{path}:{line_number}: {error}") from error
        return

    try:
        with path.open(encoding="utf-8") as source:
            yield json.load(source)
    except json.JSONDecodeError as error:
        raise DatasetError(f"{path}: {error}") from error


def load_rows(paths: Iterable[str | Path]) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    path_count = 0
    for raw_path in paths:
        path = Path(raw_path)
        path_count += 1
        if not path.exists():
            raise DatasetError(f"input does not exist: {path}")
        for document in _documents_from_path(path):
            rows.extend(_flatten_document(document))
    if path_count == 0:
        raise DatasetError("at least one input path is required")
    if not rows:
        raise DatasetError("no user-approved research tracks found")
    return rows
