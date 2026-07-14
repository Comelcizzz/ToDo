#!/usr/bin/env bash
# Fetch official loudness / LRA test material when redistribution is allowed.
# Does not commit audio into git. Writes under testdata/official/.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEST="$ROOT/testdata/official"
mkdir -p "$DEST"

cat <<'EOF'
Official loudness testdata fetch

1. Obtain EBU Tech 3341 / 3342 signals from the EBU (or other permitted source).
2. Obtain any ITU-R BS.1770 reference material you are licensed to use.
3. Place WAV files in: testdata/official/
4. Update testdata/official/MANIFEST.md with:
   source | vector/file ID | expected | tolerance | sha256

This script intentionally does not download copyrighted files automatically.
EOF

if [[ ! -f "$DEST/MANIFEST.md" ]]; then
  echo "Missing MANIFEST.md" >&2
  exit 1
fi

echo "Manifest present at $DEST/MANIFEST.md"
echo "WAV count: $(find "$DEST" -name '*.wav' 2>/dev/null | wc -l)"
