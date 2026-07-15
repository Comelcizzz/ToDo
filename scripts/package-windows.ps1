param(
    [string]$BuildDir = "build",
    [string]$OutDir = "dist/windows",
    [string]$Config = "Release"
)

$ErrorActionPreference = "Stop"

$suite = Join-Path $BuildDir "MasteringAudioSuite_artefacts/$Config/Mastering Audio Suite.exe"
$analyzer = Join-Path $BuildDir "MasteringAudioAnalyzer_artefacts/$Config/VST3/Mastering Audio Analyzer.vst3"
$mixNode = Join-Path $BuildDir "MasteringAudioMixNode_artefacts/$Config/VST3/Mastering Audio Mix Node.vst3"

if (-not (Test-Path $suite)) {
    throw "Standalone executable not found: $suite"
}
if (-not (Test-Path $analyzer)) {
    throw "Analyzer VST3 bundle not found: $analyzer"
}
if (-not (Test-Path $mixNode)) {
    throw "Mix Node VST3 bundle not found: $mixNode"
}

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$stage = Join-Path $OutDir "MasteringAudioSuite"
Remove-Item -Recurse -Force $stage -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $stage | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $stage "VST3") | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $stage "docs") | Out-Null

Copy-Item $suite $stage
Copy-Item -Recurse $analyzer (Join-Path $stage "VST3")
Copy-Item -Recurse $mixNode (Join-Path $stage "VST3")
Copy-Item README.md (Join-Path $stage "README.txt")
if (Test-Path LICENSE) {
    Copy-Item LICENSE (Join-Path $stage "LICENSE.txt")
}
Copy-Item docs/FL_STUDIO_WORKFLOW.md (Join-Path $stage "docs") -ErrorAction SilentlyContinue
Copy-Item docs/FL_MIX_NODE_VALIDATION.md (Join-Path $stage "docs") -ErrorAction SilentlyContinue
Copy-Item docs/PACKAGING.md (Join-Path $stage "docs")
if (Test-Path docs/FEATURE_STATUS.md) {
    Copy-Item docs/FEATURE_STATUS.md (Join-Path $stage "docs")
}
if (Test-Path docs/ARCHITECTURE.md) {
    Copy-Item docs/ARCHITECTURE.md (Join-Path $stage "docs")
}

$portableZip = Join-Path $OutDir "MasteringAudioSuite-Portable-x64.zip"
$legacyZip = Join-Path $OutDir "MasteringAudioSuite-windows-x64.zip"
foreach ($zip in @($portableZip, $legacyZip)) {
    if (Test-Path $zip) { Remove-Item $zip }
    Compress-Archive -Path $stage -DestinationPath $zip
    Write-Host "Created $zip"
}

# Optional Inno Setup installer (when iscc is available).
$iss = Join-Path $PSScriptRoot "windows-installer.iss"
$iscc = Get-Command iscc -ErrorAction SilentlyContinue
if (-not $iscc) {
    $isccPath = "C:\Program Files (x86)\Inno Setup 6\ISCC.exe"
    if (Test-Path $isccPath) {
        $iscc = @{ Source = $isccPath }
    }
}
$installerDir = Join-Path $OutDir "installer"
New-Item -ItemType Directory -Force -Path $installerDir | Out-Null
if ($iscc -and (Test-Path $iss)) {
    try {
        $isccExe = if ($iscc.Source) { $iscc.Source } else { "iscc" }
        & $isccExe $iss "/DSourceDir=$stage" "/DOutDir=$OutDir"
        if ($LASTEXITCODE -ne 0) { throw "ISCC exited $LASTEXITCODE" }
        Write-Host "Inno Setup installer built"
    } catch {
        Write-Warning "Inno Setup compile failed: $_"
        @(
            "Mastering Audio Suite early installer",
            "Status: Inno Setup compile FAILED on this runner.",
            "Portable ZIP is still produced.",
            "Manual Windows install verification: REQUIRED."
        ) | Set-Content (Join-Path $installerDir "INSTALLER_STATUS.txt")
    }
} else {
    Write-Host "Inno Setup (iscc) not available — portable ZIP only."
    @(
        "Mastering Audio Suite early installer",
        "Status: Inno Setup script present (scripts/windows-installer.iss).",
        "This runner did not compile Setup.exe (iscc missing).",
        "Portable ZIP: MasteringAudioSuite-Portable-x64.zip",
        "Manual Windows install verification: REQUIRED before calling installer verified."
    ) | Set-Content (Join-Path $installerDir "INSTALLER_STATUS.txt")
}
