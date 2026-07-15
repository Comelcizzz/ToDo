; Mastering Audio Suite — early developer installer (Inno Setup 6)
; UNSIGNED. Do not call verified without manual Windows install test.

#ifndef SourceDir
  #define SourceDir "dist\\windows\\MasteringAudioSuite"
#endif
#ifndef OutDir
  #define OutDir "dist\\windows"
#endif

#define MyAppName "Mastering Audio Suite"
#define MyAppVersion "0.4.0"
#define MyAppPublisher "Mastering Audio"
#define MyAppExeName "Mastering Audio Suite.exe"

[Setup]
AppId={{A7C3E91B-2F4D-4B8A-9C11-MASTERINGAUDIO2A}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={autopf}\Mastering Audio Suite
DefaultGroupName=Mastering Audio Suite
DisableProgramGroupPage=yes
LicenseFile=
OutputDir={#OutDir}
OutputBaseFilename=MasteringAudioSuite-Setup-x64
Compression=lzma
SolidCompression=yes
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
UninstallDisplayName={#MyAppName}
VersionInfoVersion={#MyAppVersion}
SetupLogging=yes
; Developer build is intentionally UNSIGNED (no SignTool).

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a &Desktop shortcut"; GroupDescription: "Additional icons:"; Flags: unchecked

[Files]
; Suite application
Source: "{#SourceDir}\Mastering Audio Suite.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceDir}\README.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceDir}\LICENSE.txt"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#SourceDir}\docs\*"; DestDir: "{app}\docs"; Flags: ignoreversion recursesubdirs createallsubdirs
; VST3 plugins → Common Program Files\VST3
Source: "{#SourceDir}\VST3\Mastering Audio Analyzer.vst3\*"; DestDir: "{commoncf64}\VST3\Mastering Audio Analyzer.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#SourceDir}\VST3\Mastering Audio Mix Node.vst3\*"; DestDir: "{commoncf64}\VST3\Mastering Audio Mix Node.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Code]
function InitializeSetup(): Boolean;
begin
  Result := True;
  // WebView2 Runtime check (best-effort)
  if not RegKeyExists(HKLM, 'SOFTWARE\WOW6432Node\Microsoft\EdgeUpdate\Clients\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}')
     and not RegKeyExists(HKLM, 'SOFTWARE\Microsoft\EdgeUpdate\Clients\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}') then
  begin
    MsgBox('WebView2 Runtime was not detected. Suite UI may fail until WebView2 is installed.', mbInformation, MB_OK);
  end;
end;

[UninstallDelete]
; Do not delete user projects/settings under %AppData%\Mastering Audio

[Messages]
FinishedLabel=Setup has finished installing Mastering Audio Suite, Analyzer VST3, and Mix Node VST3. This build is UNSIGNED.
