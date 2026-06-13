; FrameProbe installer (Inno Setup 6)
; ---------------------------------------------------------------------------
; Build the app first, then compile this script with ISCC:
;   ISCC.exe /DBuildDir="..\build\Release" installer\FrameProbe.iss
; The CI workflow does this automatically and uploads FrameProbeSetup.exe.
;
; PresentMon (optional, enables frame-time capture) is bundled only if a copy
; is present in BuildDir\tools at packaging time — it is not redistributed in
; the repo. See tools\README.md.

#ifndef BuildDir
  #define BuildDir "..\build\Release"
#endif
#define MyAppName "FrameProbe"
#define MyAppVersion "1.0.0"
#define MyAppPublisher "geovie"
#define MyAppExe "FrameProbe.exe"

[Setup]
AppId={{8E0F7A12-BFB3-4FE8-B9A5-48FD50A15A9A}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
; Per-user install by default => no admin prompt; user may elevate for all-users.
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
OutputDir=.
OutputBaseFilename=FrameProbeSetup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
UninstallDisplayIcon={app}\{#MyAppExe}
AppPublisherURL=https://github.com/geovie-vie/legaminglescriptlelelele

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#BuildDir}\{#MyAppExe}"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\README.md";            DestDir: "{app}"; DestName: "README.txt"; Flags: ignoreversion isreadme
Source: "..\tools\README.md";      DestDir: "{app}\tools"; DestName: "README.txt"; Flags: ignoreversion
; Optional bundled tools — included only if they exist at packaging time.
Source: "{#BuildDir}\tools\PresentMon*.exe"; DestDir: "{app}\tools"; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#BuildDir}\tools\nvidia-smi.exe";  DestDir: "{app}\tools"; Flags: ignoreversion skipifsourcedoesntexist

[Icons]
Name: "{group}\{#MyAppName}";        Filename: "{app}\{#MyAppExe}"
Name: "{group}\Uninstall {#MyAppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}";  Filename: "{app}\{#MyAppExe}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExe}"; Description: "{cm:LaunchProgram,{#MyAppName}}"; Flags: nowait postinstall skipifsilent
