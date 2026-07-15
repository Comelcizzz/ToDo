# Packaging

## Requirements

- Windows 11 x64
- Visual Studio 2022 Build Tools with C++ desktop workload
- CMake ≥ 3.25
- Node.js ≥ 22
- WebView2 Runtime (usually already installed on Windows 11)
- Microsoft.Web.WebView2 NuGet SDK package for static loader builds
- Valid JUCE licence for commercial distribution of closed-source builds

## Release build

```powershell
npm ci
npm run build
nuget install Microsoft.Web.WebView2 -OutputDirectory build/webview2
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
  -DJUCE_WEBVIEW2_PACKAGE_LOCATION=build/webview2
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

Artifacts:

- `build/MasteringAudioAnalyzer_artefacts/Release/VST3/Mastering Audio Analyzer.vst3`
- `build/MasteringAudioMixNode_artefacts/Release/VST3/Mastering Audio Mix Node.vst3`
- `build/MasteringAudioSuite_artefacts/Release/Mastering Audio Suite.exe`
- `dist/windows/MasteringAudioSuite-Portable-x64.zip`
- `dist/windows/MasteringAudioSuite-Setup-x64.exe` (Inno Setup; unsigned; NOT VERIFIED without manual install)

## Install layout

```text
C:\Program Files\Mastering Audio Suite\
  Mastering Audio Suite.exe
  README.txt
  docs\

%COMMONPROGRAMFILES%\VST3\
  Mastering Audio Analyzer.vst3\
  Mastering Audio Mix Node.vst3\
```

Use the helper script:

```powershell
./scripts/package-windows.ps1 -BuildDir build -OutDir dist/windows
```

The script produces a ZIP installer bundle with the standalone executable, VST3 bundle, and workflow docs.
