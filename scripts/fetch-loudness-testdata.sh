#!/usr/bin/env bash
# Fetch / verify official loudness test material.
# Does not commit WAVs. Fails on checksum mismatch when hashes are configured.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEST="$ROOT/testdata/official"
MANIFEST="$DEST/manifest.json"
mkdir -p "$DEST"

if [[ ! -f "$MANIFEST" ]]; then
  echo "error: missing $MANIFEST" >&2
  exit 1
fi

echo "Official loudness testdata helper"
echo "Manifest: $MANIFEST"
echo
echo "This repository does not auto-download EBU/ITU copyrighted WAV files."
echo "Place licensed files into $DEST using the filenames in manifest.json."
echo

python3 - "$MANIFEST" "$DEST" <<'PY'
import hashlib, json, sys
from pathlib import Path
manifest_path = Path(sys.argv[1])
dest = Path(sys.argv[2])
data = json.loads(manifest_path.read_text())
missing = []
mismatched = []
present = []
for vector in data.get("vectors", []):
    name = vector.get("localFilename")
    expected = (vector.get("sha256") or "").strip().lower()
    if not name:
        continue
    path = dest / name
    if not path.exists():
        missing.append(name)
        continue
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    if expected and digest != expected:
        mismatched.append((name, expected, digest))
    else:
        present.append((name, digest))

print(f"present={len(present)} missing={len(missing)} mismatched={len(mismatched)}")
for name, digest in present:
    print(f"OK {name} sha256={digest}")
for name in missing:
    print(f"MISSING {name}")
for name, expected, digest in mismatched:
    print(f"MISMATCH {name} expected={expected} actual={digest}")

if mismatched:
    sys.exit(2)
print("Verify finished. Missing official files are allowed for compile;")
print("M1A compliance remains PARTIAL until present files match configured SHA-256.")
PY
