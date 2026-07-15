#!/usr/bin/env bash
# Download / verify EBU Loudness Test Set (Tech 3341 / 3342).
# WAV files stay local (gitignored). Fails hard on checksum/HTML mismatches.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEST="$ROOT/testdata/official"
WAV_DIR="$DEST/wav"
MANIFEST="$DEST/manifest.json"
ZIP_NAME="ebu-loudness-test-setv05.zip"
PRIMARY_URL="https://web.archive.org/web/20250123210734if_/https://tech.ebu.ch/files/live/sites/tech/files/shared/testmaterial/ebu-loudness-test-setv05.zip"
EBU_LIVE_URL="https://tech.ebu.ch/files/live/sites/tech/files/shared/testmaterial/ebu-loudness-test-setv05.zip"

mkdir -p "$WAV_DIR"
MODE="${1:-auto}" # auto | download | verify | manual

usage() {
  cat <<EOF
Usage: $0 [auto|download|verify|manual]

  auto      Try download then verify (default)
  download  Fetch ZIP from Wayback (fallback) / print live EBU URL
  verify    Verify local WAV SHA-256 against manifest.json
  manual    Print exact manual import instructions and exit 0

Manual import:
  1. Download EBU Loudness test set v5.0 from:
     https://tech.ebu.ch/publications/ebu_loudness_test_set
     (or Wayback mirror used by this script)
  2. Place the ZIP at: $DEST/$ZIP_NAME
  3. Or extract WAVs into: $WAV_DIR/
  4. Run: $0 verify
EOF
}

is_zip() {
  local f="$1"
  python3 - "$f" <<'PY'
import sys
from pathlib import Path
p=Path(sys.argv[1])
if not p.exists() or p.stat().st_size < 1000:
    raise SystemExit(1)
b=p.read_bytes()[:4]
raise SystemExit(0 if b[:2]==b'PK' else 1)
PY
}

download_zip() {
  local out="$DEST/$ZIP_NAME"
  if [[ -f "$out" ]] && is_zip "$out"; then
    echo "ZIP already present: $out"
    return 0
  fi
  echo "Attempting download..."
  echo "Primary (Wayback): $PRIMARY_URL"
  if curl -fL --retry 3 --retry-delay 4 -A "Mozilla/5.0" \
      --max-time 600 -o "$out.partial" "$PRIMARY_URL"; then
    mv "$out.partial" "$out"
  else
    rm -f "$out.partial"
    echo "Wayback download failed."
    echo "Live EBU URL (may be Cloudflare-blocked in CI): $EBU_LIVE_URL"
    echo "Trying live URL..."
    if curl -fL --retry 2 -A "Mozilla/5.0" --max-time 600 -o "$out.partial" "$EBU_LIVE_URL"; then
      mv "$out.partial" "$out"
    else
      rm -f "$out.partial"
      echo "error: automatic download failed." >&2
      usage
      exit 1
    fi
  fi
  # Reject HTML/error pages masquerading as ZIP
  local ctype
  ctype=$(file -b --mime-type "$out" || true)
  if ! is_zip "$out"; then
    echo "error: downloaded file is not a ZIP (mime=$ctype). Refusing to continue." >&2
    head -c 200 "$out" || true
    rm -f "$out"
    exit 1
  fi
  echo "Downloaded ZIP OK ($(wc -c < "$out") bytes)"
}

extract_zip() {
  local zip="$DEST/$ZIP_NAME"
  [[ -f "$zip" ]] || { echo "error: missing $zip" >&2; exit 1; }
  is_zip "$zip" || { echo "error: $zip is not a ZIP" >&2; exit 1; }
  echo "Extracting to $WAV_DIR"
  unzip -o -q "$zip" -d "$WAV_DIR"
}

verify_manifest() {
  [[ -f "$MANIFEST" ]] || { echo "error: missing $MANIFEST" >&2; exit 1; }
  python3 - "$MANIFEST" "$WAV_DIR" <<'PY'
import hashlib, json, sys
from pathlib import Path
manifest = json.loads(Path(sys.argv[1]).read_text())
wav_dir = Path(sys.argv[2])
missing, mismatched, present, skipped = [], [], [], []
for v in manifest.get("vectors", []):
    name = v.get("localFilename") or v.get("originalFilename")
    expected = (v.get("sha256") or "").strip().lower()
    scope = v.get("scope", "mono-stereo")
    if scope == "out-of-scope":
        skipped.append(name)
        continue
    if not name:
        continue
    path = wav_dir / name
    if not path.exists():
        missing.append(name)
        continue
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    if expected and digest != expected:
        mismatched.append((name, expected, digest))
    else:
        present.append((name, digest if expected else "(hash not pinned)"))

print(f"present={len(present)} missing={len(missing)} mismatched={len(mismatched)} out_of_scope_skipped={len(skipped)}")
for name, digest in present:
    print(f"OK {name} sha256={digest}")
for name in missing:
    print(f"MISSING {name}")
for name, exp, got in mismatched:
    print(f"MISMATCH {name}\n  expected={exp}\n  actual  ={got}")
if mismatched:
    sys.exit(2)
if missing:
    print("error: required vectors missing — cannot claim official compliance.", file=sys.stderr)
    sys.exit(1)
print("All required manifest vectors present and hashes match.")
PY
}

case "$MODE" in
  -h|--help) usage; exit 0 ;;
  manual) usage; exit 0 ;;
  download)
    download_zip
    extract_zip
    verify_manifest
    ;;
  verify)
    verify_manifest
    ;;
  auto)
    if [[ ! -d "$WAV_DIR" ]] || [[ -z "$(ls -A "$WAV_DIR" 2>/dev/null || true)" ]]; then
      download_zip
      extract_zip
    else
      echo "WAV directory already populated: $WAV_DIR"
    fi
    verify_manifest
    ;;
  *)
    echo "Unknown mode: $MODE" >&2
    usage
    exit 1
    ;;
esac
