param(
    [string]$BuildDir = "build",
    [string]$OutDir = "dist/windows",
    [string]$Config = "Release"
)

$ErrorActionPreference = "Stop"

$suite = Join-Path $BuildDir "MasteringAudioSuite_artefacts/$Config/Mastering Audio Suite.exe"
$vst3 = Join-Path $BuildDir "MasteringAudioAnalyzer_artefacts/$Config/VST3/Mastering Audio Analyzer.vst3"

if (-not (Test-Path $suite)) {
    throw "Standalone executable not found: $suite"
}
if (-not (Test-Path $vst3)) {
    throw "VST3 bundle not found: $vst3"
}

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$stage = Join-Path $OutDir "MasteringAudioSuite"
Remove-Item -Recurse -Force $stage -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $stage | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $stage "VST3") | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $stage "docs") | Out-Null

Copy-Item $suite $stage
Copy-Item -Recurse $vst3 (Join-Path $stage "VST3")
Copy-Item README.md (Join-Path $stage "README.txt")
if (Test-Path LICENSE) {
    Copy-Item LICENSE (Join-Path $stage "LICENSE.txt")
}
Copy-Item docs/FL_STUDIO_WORKFLOW.md (Join-Path $stage "docs")
Copy-Item docs/PACKAGING.md (Join-Path $stage "docs")
if (Test-Path docs/FEATURE_STATUS.md) {
    Copy-Item docs/FEATURE_STATUS.md (Join-Path $stage "docs")
}
if (Test-Path docs/ARCHITECTURE.md) {
    Copy-Item docs/ARCHITECTURE.md (Join-Path $stage "docs")
}

$zip = Join-Path $OutDir "MasteringAudioSuite-windows-x64.zip"
if (Test-Path $zip) {
    Remove-Item $zip
}
Compress-Archive -Path $stage -DestinationPath $zip
Write-Host "Created $zip"
