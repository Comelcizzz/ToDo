# Packaging

## Requirements

- Windows 11 x64
- Visual Studio 2022 Build Tools with C++ desktop workload
- CMake ≥ 3.25
- Node.js ≥ 22
- WebView2 Runtime (usually already installed on Windows 11)
- Valid JUCE licence for commercial distribution of closed-source builds

## Release build

```powershell
npm ci
npm run build
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

Artifacts:

- `build/MasteringAudioAnalyzer_artefacts/Release/VST3/Mastering Audio Analyzer.vst3`
- `build/MasteringAudioSuite_artefacts/Release/Mastering Audio Suite.exe`

## Install layout

```text
C:\Program Files\Mastering Audio Suite\
  Mastering Audio Suite.exe
  README.txt
  docs\

%COMMONPROGRAMFILES%\VST3\
  Mastering Audio Analyzer.vst3\
```

Use the helper script:

```powershell
./scripts/package-windows.ps1 -BuildDir build -OutDir dist/windows
```

The script produces a ZIP installer bundle with the standalone executable, VST3 bundle, and workflow docs.
