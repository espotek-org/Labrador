; Inno Setup script for the EspoTek Labrador Unified App (Windows installer).
;
; FOSS replacement for the old Advanced Installer (.aip) pipeline. Produces a
; single self-contained "Labrador-for-Windows.exe" that installs the app (a
; fully static exe: MinGW runtime + libusb baked in), its bundled assets +
; firmware hex, and the USB driver installers, and offers to run the primary
; driver installer at the end.
;
; Built in CI with:
;   ISCC.exe /DMyAppVersion=... /DStagingDir=... /DOutputDir=... labrador.iss
; The staging dir must contain: labrador.exe, assets\, driver\.
; appicon.ico must sit next to this script (the workflow copies it in).

#ifndef MyAppVersion
  #define MyAppVersion "2.0.0"
#endif
#ifndef StagingDir
  #define StagingDir "staging"
#endif
#ifndef OutputDir
  #define OutputDir "installer"
#endif

#define MyAppName "EspoTek Labrador"
#define MyAppPublisher "EspoTek"
#define MyAppURL "https://espotek.com"
#define MyAppExeName "labrador.exe"

[Setup]
; A stable, unified-app-specific AppId (distinct from the Qt app's GUID) so the
; two never collide in Add/Remove Programs.
AppId={{7F3B2C10-9E4D-4A6B-B1E2-4C7A9D0F5E31}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
DefaultDirName={autopf}\EspoTek Labrador
DefaultGroupName=EspoTek Labrador
DisableProgramGroupPage=yes
UninstallDisplayIcon={app}\{#MyAppExeName}
OutputDir={#OutputDir}
OutputBaseFilename=Labrador-for-Windows
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
SetupIconFile=appicon.ico
; The unified app is 64-bit only (MSYS2 MINGW64 build).
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[InstallDelete]
; Earlier installers shipped the MinGW runtime + libusb as DLLs next to the
; exe; the exe is fully static now, so clear them out on upgrade.
Type: files; Name: "{app}\*.dll"

[Files]
Source: "{#StagingDir}\labrador.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#StagingDir}\assets\*"; DestDir: "{app}\assets"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#StagingDir}\driver\*"; DestDir: "{app}\driver"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\EspoTek Labrador"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\Uninstall EspoTek Labrador"; Filename: "{uninstallexe}"
Name: "{autodesktop}\EspoTek Labrador"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
; Install the USB driver so Windows recognises the board. The board must be
; unplugged during driver installation; the installer's own UI guides the user.
Filename: "{app}\driver\Driver_Install.exe"; Description: "Install the EspoTek Labrador USB driver (required for the board to work)"; Flags: postinstall skipifsilent
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,EspoTek Labrador}"; Flags: postinstall skipifsilent nowait
