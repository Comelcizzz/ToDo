from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path

from mastering_audio_ml.dataset import DatasetError, TARGET_BOUNDS, load_rows
from mastering_audio_ml.model import ExplainableMixModel
from mastering_audio_ml.train import train_and_evaluate


def research_document(project_index: int) -> dict:
    roles = ["kick", "bass", "rhythm-guitar", "snare"]
    tracks = []
    for track_index, role in enumerate(roles):
        rms = -26.0 + track_index + project_index * 0.1
        role_gain = {"kick": 2.0, "bass": 1.0, "rhythm-guitar": -1.0, "snare": 0.5}[role]
        tracks.append(
            {
                "index": track_index,
                "role": role,
                "metrics": {
                    "samplePeakDbfs": rms + 8.0,
                    "estimatedTruePeakDbtp": rms + 7.5,
                    "rmsDbfs": rms,
                    "integratedLufs": rms - 1.0,
                    "crestFactorDb": 8.0,
                    "stereoCorrelation": 0.7,
                    "transientDensityHz": 2.0 + track_index,
                    "durationSeconds": 180.0,
                    "sampleRate": 48_000,
                    "channels": 2,
                    "spectrum": {
                        "subDb": -20.0 - track_index,
                        "bassDb": -18.0 - track_index,
                        "lowMidDb": -22.0,
                        "midDb": -24.0,
                        "presenceDb": -25.0 + track_index,
                        "airDb": -30.0,
                    },
                },
                "approvedSettings": {
                    "gainDb": role_gain - (rms + 24.0) * 0.2,
                    "pan": 0.0,
                    "polarityInverted": False,
                    "processing": {
                        "bypass": False,
                        "inputGainDb": 0.0,
                        "amount": 0.7,
                        "saturation": 0.1 if role != "bass" else 0.2,
                        "clipCeilingDb": -1.0,
                        "outputGainDb": 0.0,
                        "equalizer": {
                            "highPassHz": 30.0 if role in {"kick", "bass"} else 80.0,
                            "lowShelfGainDb": 0.0,
                            "presenceGainDb": 0.5 if role == "snare" else 0.0,
                            "highShelfGainDb": 0.0,
                        },
                        "compressor": {
                            "thresholdDb": -18.0,
                            "ratio": 3.0 if role == "bass" else 2.0,
                            "attackMs": 20.0,
                            "releaseMs": 100.0,
                            "makeupDb": 0.0,
                        },
                    },
                },
            }
        )
    return {
        "schemaVersion": 1,
        "kind": "mastering-audio-research-example",
        "privacy": {
            "audioIncluded": False,
            "pathsIncluded": False,
            "namesIncluded": False,
            "exportIsUserInitiated": True,
        },
        "project": {
            "anonymousId": f"project-{project_index}",
            "sampleRate": 48_000,
            "trackCount": len(tracks),
            "selectedVariant": "balanced",
            "userApproved": True,
            "tracks": tracks,
        },
    }


class PipelineTests(unittest.TestCase):
    def test_dataset_rejects_examples_that_include_audio(self) -> None:
        document = research_document(0)
        document["privacy"]["audioIncluded"] = True
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "unsafe.json"
            path.write_text(json.dumps(document), encoding="utf-8")
            with self.assertRaises(DatasetError):
                load_rows([path])

    def test_training_uses_project_holdout_and_saves_bounded_model(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            dataset = root / "examples.jsonl"
            dataset.write_text(
                "\n".join(json.dumps(research_document(index)) for index in range(8)),
                encoding="utf-8",
            )
            rows = load_rows([dataset])
            model_path = root / "model.joblib"
            report_path = root / "report.json"

            report = train_and_evaluate(rows, model_path, report_path)

            self.assertEqual(report["dataset"]["projects"], 8)
            self.assertGreaterEqual(report["dataset"]["holdoutProjects"], 1)
            self.assertTrue(model_path.exists())
            self.assertTrue(report_path.exists())

            model = ExplainableMixModel.load(model_path)
            prediction = model.predict(rows[:1])[0]
            for target, value in prediction.items():
                lower, upper = TARGET_BOUNDS[target]
                self.assertGreaterEqual(value, lower)
                self.assertLessEqual(value, upper)
            self.assertTrue(model.top_influences("gainDb"))


if __name__ == "__main__":
    unittest.main()
