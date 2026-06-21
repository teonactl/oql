; OQL — Inno Setup installer script
; Uso da CI: ISCC /DMyAppVersion="1.2.3" /DOutputFile="oql-beta-windows" installer.iss

#ifndef MyAppVersion
  #define MyAppVersion "0.1.0"
#endif
#ifndef OutputFile
  #define OutputFile "oql-windows-setup"
#endif

#define MyAppName      "OQL"
#define MyAppExeName   "oql.exe"
#define MyAppPublisher "OQL"

[Setup]
; GUID nuovo (era legato al vecchio branding "OpenQLab"): con l'AppId
; precedente, Inno Setup riusava la cartella di installazione registrata
; dalla vecchia versione (es. C:\Program Files\OpenQLab) invece di
; DefaultDirName, anche dopo aver rinominato l'app in OQL.
AppId={{3A2321AF-5A0B-4F01-AB6C-A95E4E8F954C}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL=https://github.com/teonactl/oql
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
SetupIconFile=resources\oql.ico
AllowNoIcons=yes
OutputDir=.
OutputBaseFilename={#OutputFile}
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
ArchitecturesInstallIn64BitMode=x64
MinVersion=10.0
DisableProgramGroupPage=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "italian"; MessagesFile: "compiler:Languages\Italian.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"

[Files]
Source: "dist\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "vc_redist.x64.exe"; DestDir: "{tmp}"; Flags: deleteafterinstall

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; Filename: "{uninstallexe}"
Name: "{commondesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
; Redistributable firmato Microsoft, eseguito al posto di copiare a mano
; msvcp140.dll/vcruntime140.dll: alcuni antivirus quarantinano quelle DLL se
; depositate direttamente da un installer non firmato (pattern simile a
; DLL side-loading usato da malware).
Filename: "{tmp}\vc_redist.x64.exe"; Parameters: "/install /quiet /norestart"; StatusMsg: "Installazione Visual C++ Redistributable..."; Flags: waituntilterminated
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#MyAppName}}"; Flags: nowait postinstall skipifsilent
