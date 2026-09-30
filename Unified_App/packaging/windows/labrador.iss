; Inno Setup script for the EspoTek Labrador Unified App (Windows installer).
;
; FOSS replacement for the old Advanced Installer (.aip) pipeline. Produces a
; single self-contained "Labrador-for-Windows.exe" that installs the app (a
; fully static exe: MinGW runtime + libusb baked in; the x64 or x86 build
; matching the host - the Qt app always shipped 32-bit, so 32-bit Windows
; stays supported), its bundled assets + firmware hex, and the three unpacked
; USB driver packages, which it installs silently during setup.
;
; Built in CI with:
;   ISCC.exe /DMyAppVersion=... /DStagingDir=... /DOutputDir=... labrador.iss
; The staging dir must contain: labrador64.exe, labrador32.exe, assets\,
; driver\<package>\ (unpacked dpinst packages).  appicon.ico must sit next to this script (the workflow copies it
; in).

#ifndef MyAppVersion
  #define MyAppVersion "2.0.0"
#endif
#ifndef StagingDir
  #define StagingDir "staging"
#endif
#ifndef OutputDir
  #define OutputDir "installer"
#endif

#define MyAppName "EspoTek Labrador Unified App (Beta)"
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
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
UninstallDisplayIcon={app}\{#MyAppExeName}
OutputDir={#OutputDir}
OutputBaseFilename=Labrador-for-Windows
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
SetupIconFile=appicon.ico
; x64-capable hosts (including ARM64 via emulation) install in 64-bit mode
; and get the x64 exe; 32-bit x86 hosts install the x86 exe.
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
Source: "{#StagingDir}\labrador64.exe"; DestDir: "{app}"; DestName: "labrador.exe"; Flags: ignoreversion; Check: Is64BitInstallMode
Source: "{#StagingDir}\labrador32.exe"; DestDir: "{app}"; DestName: "labrador.exe"; Flags: ignoreversion solidbreak; Check: not Is64BitInstallMode
Source: "{#StagingDir}\assets\*"; DestDir: "{app}\assets"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#StagingDir}\driver\*"; DestDir: "{app}\driver"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\Uninstall {#MyAppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
; USB drivers, installed silently while Setup runs (Setup is elevated, so no
; extra prompts).  The staging step unpacks the three driver packages the Qt
; installer's prerequisites installed, so existing installs keep the drivers
; they already have (nothing moves to WinUSB):
;   Driver_Install      libusbK      running board          03EB:BA94
;   Bootloader_Install  libusb-win32 DFU bootloader         03EB:2FE4 (firmware updates, #450)
;   Gobindar_Install    libusbK      misconfigured board    03EB:A000 (recovery dialog)
; Each package: dpscat.exe self-signs the .cat and trusts the certificate,
; then dpinst pre-stages the package (/SW no wizard, /SA no Add/Remove entry).
; The board may be plugged in or not; Windows binds the driver on enumeration.
Filename: "{app}\driver\Driver_Install\dpscat.exe"; WorkingDir: "{app}\driver\Driver_Install"; StatusMsg: "Installing USB driver: Labrador board..."; Flags: runhidden waituntilterminated
Filename: "{app}\driver\Driver_Install\dpinst64.exe"; Parameters: "/SW /SA"; WorkingDir: "{app}\driver\Driver_Install"; StatusMsg: "Installing USB driver: Labrador board..."; Flags: runhidden waituntilterminated; Check: Is64BitInstallMode
Filename: "{app}\driver\Driver_Install\dpinst32.exe"; Parameters: "/SW /SA"; WorkingDir: "{app}\driver\Driver_Install"; StatusMsg: "Installing USB driver: Labrador board..."; Flags: runhidden waituntilterminated; Check: not Is64BitInstallMode
Filename: "{app}\driver\Bootloader_Install\dpscat.exe"; WorkingDir: "{app}\driver\Bootloader_Install"; StatusMsg: "Installing USB driver: firmware update (bootloader)..."; Flags: runhidden waituntilterminated
Filename: "{app}\driver\Bootloader_Install\dpinst64.exe"; Parameters: "/SW /SA"; WorkingDir: "{app}\driver\Bootloader_Install"; StatusMsg: "Installing USB driver: firmware update (bootloader)..."; Flags: runhidden waituntilterminated; Check: Is64BitInstallMode
Filename: "{app}\driver\Bootloader_Install\dpinst32.exe"; Parameters: "/SW /SA"; WorkingDir: "{app}\driver\Bootloader_Install"; StatusMsg: "Installing USB driver: firmware update (bootloader)..."; Flags: runhidden waituntilterminated; Check: not Is64BitInstallMode
Filename: "{app}\driver\Gobindar_Install\dpscat.exe"; WorkingDir: "{app}\driver\Gobindar_Install"; StatusMsg: "Installing USB driver: board recovery..."; Flags: runhidden waituntilterminated
Filename: "{app}\driver\Gobindar_Install\dpinst64.exe"; Parameters: "/SW /SA"; WorkingDir: "{app}\driver\Gobindar_Install"; StatusMsg: "Installing USB driver: board recovery..."; Flags: runhidden waituntilterminated; Check: Is64BitInstallMode
Filename: "{app}\driver\Gobindar_Install\dpinst32.exe"; Parameters: "/SW /SA"; WorkingDir: "{app}\driver\Gobindar_Install"; StatusMsg: "Installing USB driver: board recovery..."; Flags: runhidden waituntilterminated; Check: not Is64BitInstallMode
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#MyAppName}}"; Flags: postinstall skipifsilent nowait
