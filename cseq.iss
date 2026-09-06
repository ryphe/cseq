[Setup]
AppId={{C8C1AF95-C753-4E51-AF9D-9119755A8394}
AppName=cseq
AppVersion=1.32
AppPublisher=ryphe
DefaultDirName={autopf}\cseq
UsePreviousAppDir=no
DefaultGroupName=cseq
SetupIconFile=cseq.ico
UninstallDisplayIcon={app}\cseq.ico
Compression=lzma2/ultra64
SolidCompression=yes
OutputDir=.
OutputBaseFilename=cseq_setup_1.32
WizardStyle=modern
ChangesAssociations=yes
DirExistsWarning=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "cseq.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "cseq.ico"; DestDir: "{app}"; Flags: ignoreversion

[Registry]
Root: HKCR; Subkey: ".csq"; ValueType: string; ValueName: ""; ValueData: "cseq.Project"; Flags: uninsdeletekey
Root: HKCR; Subkey: ".csq"; ValueType: string; ValueName: "PerceivedType"; ValueData: "document"; Flags: uninsdeletekey

Root: HKCR; Subkey: "cseq.Project"; ValueType: string; ValueName: ""; ValueData: "cseq Project"; Flags: uninsdeletekey
Root: HKCR; Subkey: "cseq.Project\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\cseq.exe,0"; Flags: uninsdeletekey

Root: HKCR; Subkey: "cseq.Project\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\cseq.exe"" ""%1"""; Flags: uninsdeletekey

[Icons]
Name: "{group}\cseq"; Filename: "{app}\cseq.exe"; IconFilename: "{app}\cseq.ico"
Name: "{group}\{cm:UninstallProgram,cseq}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\cseq"; Filename: "{app}\cseq.exe"; IconFilename: "{app}\cseq.ico"; Tasks: desktopicon

[UninstallDelete]
Type: files; Name: "{app}\cseq.exe"
Type: files; Name: "{app}\cseq.ico"
Type: files; Name: "{app}\cseq_thumb.dll"
Type: files; Name: "{app}\cseq_thumb.propdesc"
Type: files; Name: "{app}\unins*.exe"
Type: files; Name: "{app}\unins*.dat"

[Run]
Filename: "{app}\cseq.exe"; Description: "{cm:LaunchProgram,cseq}"; Flags: nowait postinstall skipifsilent
