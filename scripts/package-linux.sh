#!/usr/bin/env bash
set -euo pipefail

BUILD_DIR=${1:-build/products}
OUT_DIR=${2:-dist/linux}
CONFIG=${3:-Debug}

mkdir -p "$OUT_DIR"
STAGE="$OUT_DIR/MasteringAudioSuite"
rm -rf "$STAGE"
mkdir -p "$STAGE/VST3" "$STAGE/docs"

SUITE="$BUILD_DIR/MasteringAudioSuite_artefacts/$CONFIG/Mastering Audio Suite"
VST3="$BUILD_DIR/MasteringAudioAnalyzer_artefacts/$CONFIG/VST3/Mastering Audio Analyzer.vst3"

cp "$SUITE" "$STAGE/"
cp -R "$VST3" "$STAGE/VST3/"
cp README.md "$STAGE/README.txt"
cp docs/FL_STUDIO_WORKFLOW.md docs/PACKAGING.md "$STAGE/docs/"

(
  cd "$OUT_DIR"
  rm -f MasteringAudioSuite-linux-x64.zip
  zip -qr MasteringAudioSuite-linux-x64.zip MasteringAudioSuite
)

echo "Created $OUT_DIR/MasteringAudioSuite-linux-x64.zip"
