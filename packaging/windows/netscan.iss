; Build from repository root:
;   ISCC.exe /DAppVersion=0.1.0 packaging\windows\netscan.iss

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif

#define AppName       "NetScan"
#define AppPublisher  "NetScan"
#define AppId         "{{8F3A2C1E-4B5D-4E6F-9A0B-1C2D3E4F5A6B}}"
#define AppExeName    "NetScan.exe"
#define ServerExeName "netscan-server.exe"

[Setup]
AppId={#AppId}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL=https://github.com/Knosper
AppSupportURL=https://github.com/Knosper
AppUpdatesURL=https://github.com/Knosper

; LocalAppData\Programs is the standard place for user-scoped desktop apps.
DefaultDirName={localappdata}\Programs\{#AppName}
PrivilegesRequired=lowest
UsePreviousAppDir=no
CloseApplications=yes
RestartApplications=yes
CreateUninstallRegKey=yes

DisableProgramGroupPage=no
DefaultGroupName={#AppName}

OutputBaseFilename=NetScan-Setup-{#AppVersion}
OutputDir=packaging\windows\Output
SourceDir=..\..

Compression=lzma2
SolidCompression=yes

WizardStyle=modern
ShowLanguageDialog=auto
AlwaysShowDirOnReadyPage=yes
SetupIconFile=resources\app.ico
WizardSizePercent=110

UninstallDisplayName={#AppName} {#AppVersion}
UninstallDisplayIcon={app}\{#AppExeName}

LicenseFile=LICENSE

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "german";  MessagesFile: "compiler:Languages\German.isl"

[Tasks]
Name: "desktopicon"; \
  Description: "{cm:CreateDesktopIcon}"; \
  GroupDescription: "{cm:AdditionalIcons}"; \
  Flags: unchecked

[Files]
Source: "release\NetScan.exe"; \
  DestDir: "{app}"; \
  Flags: ignoreversion

Source: "release\{#ServerExeName}"; \
  DestDir: "{app}"; \
  Flags: ignoreversion

Source: "release\resources\web\*"; \
  DestDir: "{app}\resources\web"; \
  Flags: ignoreversion recursesubdirs createallsubdirs

Source: "release\stop.bat"; \
  DestDir: "{app}"; \
  Flags: ignoreversion

Source: "docs\security.md"; \
  DestDir: "{app}\docs"; \
  Flags: ignoreversion

[Icons]
Name: "{group}\{#AppName}"; \
  Filename: "{app}\{#AppExeName}"

Name: "{group}\{cm:UninstallProgram,{#AppName}}"; \
  Filename: "{uninstallexe}"

Name: "{autodesktop}\{#AppName}"; \
  Filename: "{app}\{#AppExeName}"; \
  Tasks: desktopicon

[Run]
Filename: "{app}\{#AppExeName}"; \
  Description: "{cm:LaunchProgram,{#AppName}}"; \
  Flags: nowait postinstall skipifsilent unchecked

[UninstallDelete]
; Remove runtime artifacts so the install root disappears completely after uninstall.
Type: files; Name: "{app}\conf.ini"
Type: files; Name: "{app}\conf.ini.tmp"
Type: files; Name: "{localappdata}\NetScan\conf.ini"
Type: files; Name: "{localappdata}\NetScan\conf.ini.tmp"
Type: files; Name: "{app}\netscan.pid"
Type: files; Name: "{localappdata}\NetScan\netscan.pid"
Type: files; Name: "{app}\stop.bat"
Type: files; Name: "{app}\certs\netscan.crt"
Type: files; Name: "{app}\certs\netscan.key"
Type: filesandordirs; Name: "{app}\certs"
Type: filesandordirs; Name: "{app}\resources"
Type: filesandordirs; Name: "{app}\docs"
Type: dirifempty; Name: "{app}"
Type: dirifempty; Name: "{localappdata}\NetScan"

[Code]
var
  NmapPage: TWizardPage;
  NmapStatusLabel: TNewStaticText;
  NmapDetectedPath: string;

function DetectExistingInstallPath(): string;
var
  CurrentPath: string;
  LegacyPath: string;
begin
  Result := '';

  CurrentPath := ExpandConstant('{localappdata}\Programs\{#AppName}');
  LegacyPath := ExpandConstant('{userappdata}\{#AppName}');

  if FileExists(CurrentPath + '\{#AppExeName}') or
     FileExists(CurrentPath + '\{#ServerExeName}') or
     FileExists(CurrentPath + '\unins000.exe') then
  begin
    Result := CurrentPath;
    exit;
  end;

  if FileExists(LegacyPath + '\lsm-windows-ui.exe') or
     FileExists(LegacyPath + '\unins000.exe') then
  begin
    Result := LegacyPath;
    exit;
  end;
end;

function InitializeSetup(): Boolean;
begin
  Result := True;
end;

{ DetectNmap: checks standard install locations and returns the first found
  nmap.exe path, or empty string if not found. Keep this in sync with
  NormalizeAppDataConf for user visibility on the wizard page. }
function DetectNmap(): string;
var
  Path32: string;
  Path64: string;
begin
  Path32 := ExpandConstant('{pf32}\Nmap\nmap.exe');
  Path64 := ExpandConstant('{pf}\Nmap\nmap.exe');

  if FileExists(Path32) then
  begin
    Result := Path32;
    exit;
  end;

  if FileExists(Path64) then
  begin
    Result := Path64;
    exit;
  end;

  Result := '';
end;

procedure UpdateNmapStatusLabel();
begin
  NmapDetectedPath := DetectNmap();

  if NmapDetectedPath <> '' then
    NmapStatusLabel.Caption := 'nmap detected: ' + NmapDetectedPath
  else
    NmapStatusLabel.Caption :=
      'nmap not found - NetScan will start in degraded mode. ' +
      'Install nmap from https://nmap.org and click Re-check.';
end;

procedure NmapDownloadButtonClick(Sender: TObject);
var
  ResultCode: Integer;
begin
  ShellExec('open', 'https://nmap.org/download.html', '', '', SW_SHOW, ewNoWait, ResultCode);
end;

procedure NmapRecheckButtonClick(Sender: TObject);
begin
  UpdateNmapStatusLabel();
end;

procedure InitializeWizard();
var
  DownloadButton: TNewButton;
  RecheckButton: TNewButton;
begin
  NmapPage := CreateCustomPage(
    wpSelectDir,
    'nmap dependency',
    'NetScan uses nmap to perform network scans.');

  NmapStatusLabel := TNewStaticText.Create(NmapPage);
  NmapStatusLabel.Parent := NmapPage.Surface;
  NmapStatusLabel.Left := 0;
  NmapStatusLabel.Top := 0;
  NmapStatusLabel.Width := NmapPage.SurfaceWidth;
  NmapStatusLabel.Height := 60;
  NmapStatusLabel.AutoSize := False;
  NmapStatusLabel.WordWrap := True;

  DownloadButton := TNewButton.Create(NmapPage);
  DownloadButton.Parent := NmapPage.Surface;
  DownloadButton.Caption := 'Open nmap download page';
  DownloadButton.Top := NmapStatusLabel.Top + NmapStatusLabel.Height + 16;
  DownloadButton.Left := 0;
  DownloadButton.Width := 200;
  DownloadButton.OnClick := @NmapDownloadButtonClick;

  RecheckButton := TNewButton.Create(NmapPage);
  RecheckButton.Parent := NmapPage.Surface;
  RecheckButton.Caption := 'Re-check';
  RecheckButton.Top := DownloadButton.Top;
  RecheckButton.Left := DownloadButton.Left + DownloadButton.Width + 8;
  RecheckButton.Width := 100;
  RecheckButton.OnClick := @NmapRecheckButtonClick;

  UpdateNmapStatusLabel();
end;

procedure StopNetScanServer();
var
  ResultCode: Integer;
begin
  if FileExists(ExpandConstant('{app}\stop.bat')) then
    Exec(ExpandConstant('{cmd}'),
      '/C "' + ExpandConstant('{app}\stop.bat') + '"',
      '', SW_HIDE, ewWaitUntilTerminated, ResultCode)
  else
    Exec(ExpandConstant('{sys}\taskkill.exe'),
      '/F /IM {#ServerExeName} /T',
      '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
end;

procedure UpsertConfValue(var Lines: TArrayOfString; Key: string; Value: string);
var
  I: Integer;
  Prefix: string;
  Count: Integer;
begin
  Prefix := Key + '=';
  for I := 0 to GetArrayLength(Lines) - 1 do
  begin
    if Pos(Prefix, Lines[I]) = 1 then
    begin
      Lines[I] := Prefix + Value;
      exit;
    end;
  end;

  Count := GetArrayLength(Lines);
  SetArrayLength(Lines, Count + 1);
  Lines[Count] := Prefix + Value;
end;

procedure RemoveIniSectionLines(var Lines: TArrayOfString);
var
  ReadIndex: Integer;
  WriteIndex: Integer;
begin
  WriteIndex := 0;
  for ReadIndex := 0 to GetArrayLength(Lines) - 1 do
  begin
    if (Length(Lines[ReadIndex]) >= 2) and
       (Copy(Lines[ReadIndex], 1, 1) = '[') and
       (Copy(Lines[ReadIndex], Length(Lines[ReadIndex]), 1) = ']') then
      continue;

    Lines[WriteIndex] := Lines[ReadIndex];
    WriteIndex := WriteIndex + 1;
  end;
  SetArrayLength(Lines, WriteIndex);
end;

{ NormalizeAppDataConf: writes web_dir and nmap_path into conf.ini if present.
  The nmap detection below mirrors DetectNmap for wizard-page visibility and
  remains the authoritative writer of nmap_path. }
procedure NormalizeAppDataConf();
var
  ConfPath: string;
  NmapExe: string;
  Lines: TArrayOfString;
begin
  ConfPath := ExpandConstant('{localappdata}\NetScan\conf.ini');
  ForceDirectories(ExtractFileDir(ConfPath));

  if FileExists(ConfPath) then
  begin
    if not LoadStringsFromFile(ConfPath, Lines) then
      exit;
  end
  else
  begin
    SetArrayLength(Lines, 9);
    Lines[0] := 'host=127.0.0.1';
    Lines[1] := 'port=8080';
    Lines[2] := 'db_path=./netscan.db';
    Lines[3] := 'web_dir=' + ExpandConstant('{app}\resources\web');
    Lines[4] := 'log_level=info';
    Lines[5] := '# log_file=./netscan.log';
    Lines[6] := 'ui_enabled=true';
    Lines[7] := 'scan_cooldown_seconds=0';
    Lines[8] := 'tls_enabled=false';
  end;

  RemoveIniSectionLines(Lines);
  UpsertConfValue(Lines, 'web_dir', ExpandConstant('{app}\resources\web'));

  NmapExe := ExpandConstant('{pf32}\Nmap\nmap.exe');
  if not FileExists(NmapExe) then
    NmapExe := ExpandConstant('{pf}\Nmap\nmap.exe');
  if FileExists(NmapExe) then
    UpsertConfValue(Lines, 'nmap_path', NmapExe);

  SaveStringsToFile(ConfPath, Lines, False);
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssInstall then
    StopNetScanServer()
  else if CurStep = ssPostInstall then
    NormalizeAppDataConf();
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  Answer: Integer;
begin
  if CurUninstallStep = usUninstall then
  begin
    StopNetScanServer();

    Answer := MsgBox(
      'Delete stored scan data (database and logs) as well?' + #13#10 +
      'Choose No to keep them for a later reinstall.',
      mbConfirmation, MB_YESNO or MB_DEFBUTTON2);
    if Answer = IDYES then
    begin
      DeleteFile(ExpandConstant('{app}\netscan.db'));
      DeleteFile(ExpandConstant('{app}\netscan.db-wal'));
      DeleteFile(ExpandConstant('{app}\netscan.db-shm'));
      DeleteFile(ExpandConstant('{app}\netscan.log'));
      DeleteFile(ExpandConstant('{localappdata}\NetScan\netscan.db'));
      DeleteFile(ExpandConstant('{localappdata}\NetScan\netscan.db-wal'));
      DeleteFile(ExpandConstant('{localappdata}\NetScan\netscan.db-shm'));
      DeleteFile(ExpandConstant('{localappdata}\NetScan\netscan.log'));
    end;
  end;
end;
