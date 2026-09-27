; SPDX-License-Identifier: GPL-3.0-only
; Copyright (c) 2026 ATF1502 programmer contributors
;
; Inno Setup 6 script. build-dist.sh stages the application in
; dist\atf150x-programmer and compiles this script with:
;   ISCC /DAppVersion=X.Y.Z /DStageDir=... /DOutputDir=... setup.iss

#ifndef AppVersion
  #error AppVersion must be supplied with /DAppVersion=X.Y.Z
#endif
#ifndef StageDir
  #define StageDir "..\..\dist\atf150x-programmer"
#endif
#ifndef OutputDir
  #define OutputDir "..\..\dist"
#endif

#define AppName "ATF150x Programmer"
#define AppExecutable "atf150x-programmer.exe"
#define ProjectUrl "https://github.com/ifilot/atf150x-programmer"

[Setup]
AppId={{A29C8AF4-5E6C-425F-B09E-B36285E6BCD4}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher=ATF150x programmer contributors
AppPublisherURL={#ProjectUrl}
AppSupportURL={#ProjectUrl}/issues
AppUpdatesURL={#ProjectUrl}/releases/latest
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
OutputDir={#OutputDir}
OutputBaseFilename=atf150x-programmer-v{#AppVersion}-windows-x86_64-setup
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
SetupIconFile=..\resources\icons\atf150x-programmer.ico
UninstallDisplayIcon={app}\{#AppExecutable}
LicenseFile={#StageDir}\LICENSE.txt
CloseApplications=yes
RestartApplications=no
VersionInfoVersion={#AppVersion}.0
VersionInfoCompany=ATF150x programmer contributors
VersionInfoDescription={#AppName} installer
VersionInfoProductName={#AppName}
VersionInfoProductVersion={#AppVersion}

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop icon"; GroupDescription: "Additional icons:"; Flags: unchecked

[Files]
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExecutable}"
Name: "{group}\Uninstall {#AppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExecutable}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#AppExecutable}"; Description: "Launch {#AppName}"; Flags: nowait postinstall skipifsilent
