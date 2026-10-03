; Pulse installer — single-file exe setup via Inno Setup 6.
; Builds dist\PulseSetup-<version>.exe from the Release build in build\.
;
; Install-time behaviour:
;   - Installs the executables into Program Files\Pulse (admin required).
;   - "Index service" task (default on): runs `Pulse.Index.exe --install` and
;     starts the PulseIndex service, so full-disk MFT/USN indexing works out of
;     the box without a runtime UAC prompt.
;   - Uninstall stops and removes the service before deleting files.
; Uninstall offers a default-on cleanup option for Pulse settings, caches,
; logs, and local/network index data. A custom index directory is handled
; conservatively: only Pulse-owned index artifacts are removed.

#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif
#ifndef BuildDir
  #define BuildDir "build"
#endif

[Setup]
AppId={{A3F47C2E-9D1B-4E58-8C6A-2B5D0F9E1734}
AppName=Pulse
AppVersion={#AppVersion}
#ifdef Win81Candidate
AppVerName=Pulse {#AppVersion} (Windows 8.1 x64)
#else
AppVerName=Pulse {#AppVersion}
#endif
DefaultDirName={code:DefaultPulseDirectory}
DefaultGroupName=Pulse
; Reuse the existing installation directory and task selections when this is
; an upgrade. AppId is intentionally stable so the previous install can be
; located and removed before the new files are copied.
UsePreviousAppDir=yes
UsePreviousTasks=yes
UsePreviousGroup=yes
PrivilegesRequired=admin
#ifdef Win81Candidate
MinVersion=6.3
#else
MinVersion=10.0
#endif
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
; Sources are referenced relative to the project root (this script's parent).
SourceDir=..
OutputDir=dist
#ifdef Win81Candidate
OutputBaseFilename=PulseSetup-{#AppVersion}-win81
#else
OutputBaseFilename=PulseSetup-{#AppVersion}
#endif
Compression=lzma2/ultra
SolidCompression=yes
WizardStyle=modern
SetupIconFile=src\app\pulse.ico
; PrepareToInstall requests an idle, orderly UI exit before stopping hosts.
; Never let Restart Manager bypass that handshake and close busy windows.
CloseApplications=no
RestartApplications=no
SetupLogging=yes
UninstallDisplayIcon={app}\pulse.exe

[Languages]
; ChineseSimplified.isl is bundled in installer/Languages (community translation,
; https://github.com/kira-96/Inno-Setup-Chinese-Simplified-Translation).
Name: "chinesesimp"; MessagesFile: "installer\Languages\ChineseSimplified.isl"
; ChineseTraditional.isl: official Inno Setup 6.5+ translation (jrsoftware/issrc,
; Files/Languages), bundled because older compiler installs lack it.
Name: "chinesetrad"; MessagesFile: "installer\Languages\ChineseTraditional.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"


[CustomMessages]
PulseUpdateBusy=Pulse 仍在处理文件或无法安全退出。请等待任务完成并退出所有 Pulse 窗口后重试。 / Pulse is busy or could not exit safely. Finish pending work and close all Pulse windows, then retry.
chinesetrad.PulseUpdateBusy=Pulse 仍在處理檔案或無法安全結束。請等待工作完成並結束所有 Pulse 視窗後重試。 / Pulse is busy or could not exit safely. Finish pending work and close all Pulse windows, then retry.
IntegrationRestoreFailed=部分打开命令仍指向 Pulse，卸载后这些入口可能无法打开。请重新安装 Pulse 后在系统集成设置中恢复，或修复 Windows 文件关联。 / Some open commands still point to Pulse. After uninstall they may stop working. Reinstall Pulse to restore integration, or repair Windows file associations.
IntegrationRestoreIncomplete=Pulse 打开命令已移除，但旧版本没有保留完整的原始关联，无法确认全部恢复。现有第三方设置和备份已保留。 / Pulse open commands were removed, but old versions did not retain complete original associations. Other settings and backups were preserved.
chinesetrad.IntegrationRestoreFailed=部分開啟命令仍指向 Pulse，解除安裝後這些入口可能無法開啟。請重新安裝 Pulse 後在系統整合設定中還原，或修復 Windows 檔案關聯。 / Some open commands still point to Pulse. After uninstall they may stop working. Reinstall Pulse to restore integration, or repair Windows file associations.
chinesetrad.IntegrationRestoreIncomplete=Pulse 開啟命令已移除，但舊版沒有保留完整的原始關聯，無法確認全部還原。現有第三方設定與備份已保留。 / Pulse open commands were removed, but old versions did not retain complete original associations. Other settings and backups were preserved.
; Unprefixed values are the shared (Simplified + English) texts used by the
; chinesesimp and english setups; chinesetrad overrides them.
TaskIndex=启用全盘文件索引（安装 PulseIndex 后台服务，推荐） / Enable full-disk file index (installs the PulseIndex background service, recommended)
GroupSearch=搜索与索引 / Search && indexing:
TaskStartup=开机自动启动 Pulse / Launch Pulse at sign-in
GroupOther=其他 / Other:
StatusSeedVerbs=正在缓存右键菜单项… / Caching context-menu verbs…
UninstHeading=卸载 Pulse
UninstDetail=Pulse 将停止并移除索引服务。你也可以删除 Pulse 创建的设置、缓存、日志和索引数据。
UninstCleanup=同时删除 Pulse 设置、缓存和索引数据（推荐）
UninstContinue=继续
UninstCancel=取消
IndexPageCaption=搜索索引位置 / Search index location
IndexPageDescription=选择 Pulse 索引数据库的存储目录 / Choose where Pulse stores its index database
IndexPageSubCaption=默认覆盖全部本地 NTFS 固定盘和移动盘。服务器文件夹可稍后在 Pulse 设置中按当前 Windows 用户凭据添加。
IndexPageDir=索引目录 / Index directory:
PrevUninstParse=无法识别旧版本卸载程序，安装已停止。 / Could not parse the previous uninstaller.
PrevUninstMissing=找不到旧版本卸载程序，安装已停止。 / The previous uninstaller could not be found.
PrevUninstStart=无法启动旧版本卸载程序，安装已停止。 / Could not start the previous uninstaller.
PrevUninstFailed=旧版本卸载失败（错误码 %1），安装已停止。 / The previous uninstall failed (exit code %1).
StatusIndexPath=正在设置索引位置… / Configuring index location…
IndexPathFailed=索引位置设置未完全完成，错误码：%1。请在 Pulse 设置中查看实际位置后重试。 / Index relocation needs attention. Check Settings.
StatusIndexService=正在准备索引服务… / Preparing index service…
IndexServiceFailed=索引服务启动失败，错误码：%1。软件已安装，可稍后在设置中重试。 / Index service setup failed. Retry in Settings.
chinesetrad.TaskIndex=啟用全磁碟檔案索引（安裝 PulseIndex 背景服務，建議） / Enable full-disk file index (installs the PulseIndex background service, recommended)
chinesetrad.GroupSearch=搜尋與索引 / Search && indexing:
chinesetrad.TaskStartup=開機自動啟動 Pulse / Launch Pulse at sign-in
chinesetrad.GroupOther=其他 / Other:
chinesetrad.StatusSeedVerbs=正在快取右鍵選單項目… / Caching context-menu verbs…
chinesetrad.UninstHeading=解除安裝 Pulse
chinesetrad.UninstDetail=Pulse 將停止並移除索引服務。你也可以刪除 Pulse 建立的設定、快取、記錄檔和索引資料。
chinesetrad.UninstCleanup=同時刪除 Pulse 設定、快取和索引資料（建議）
chinesetrad.UninstContinue=繼續
chinesetrad.UninstCancel=取消
chinesetrad.IndexPageCaption=搜尋索引位置 / Search index location
chinesetrad.IndexPageDescription=選擇 Pulse 索引資料庫的儲存目錄 / Choose where Pulse stores its index database
chinesetrad.IndexPageSubCaption=預設涵蓋所有本機 NTFS 固定磁碟和卸除式磁碟。伺服器資料夾可稍後在 Pulse 設定中以目前 Windows 使用者認證新增。
chinesetrad.IndexPageDir=索引目錄 / Index directory:
chinesetrad.PrevUninstParse=無法識別舊版解除安裝程式，安裝已停止。 / Could not parse the previous uninstaller.
chinesetrad.PrevUninstMissing=找不到舊版解除安裝程式，安裝已停止。 / The previous uninstaller could not be found.
chinesetrad.PrevUninstStart=無法啟動舊版解除安裝程式，安裝已停止。 / Could not start the previous uninstaller.
chinesetrad.PrevUninstFailed=舊版解除安裝失敗（錯誤碼 %1），安裝已停止。 / The previous uninstall failed (exit code %1).
chinesetrad.StatusIndexPath=正在設定索引位置… / Configuring index location…
chinesetrad.IndexPathFailed=索引位置設定未完全完成，錯誤碼：%1。請在 Pulse 設定中查看實際位置後重試。 / Index relocation needs attention. Check Settings.
chinesetrad.StatusIndexService=正在準備索引服務… / Preparing index service…
chinesetrad.IndexServiceFailed=索引服務啟動失敗，錯誤碼：%1。軟體已安裝，可稍後在設定中重試。 / Index service setup failed. Retry in Settings.

[Tasks]
Name: "indexservice"; Description: "{cm:TaskIndex}"; GroupDescription: "{cm:GroupSearch}"
Name: "startup"; Description: "{cm:TaskStartup}"; GroupDescription: "{cm:GroupOther}"; Flags: unchecked
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:GroupOther}"; Flags: unchecked

[Files]
#ifdef AppLocalRuntime
; Local MD builds bundle the matching CRT; the static Win81 CI path is unchanged.
Source: "{#BuildDir}\msvc-runtime\*.dll"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
#endif
Source: "{#BuildDir}\pulse.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#BuildDir}\lumatext.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#BuildDir}\pdfium.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#BuildDir}\licenses\PDFium\*"; DestDir: "{app}\licenses\PDFium"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "third_party\ib-pinyin-cpp\LICENSE.txt"; DestDir: "{app}\licenses\ib-pinyin"; Flags: ignoreversion
Source: "third_party\md4c\LICENSE.md"; DestDir: "{app}\licenses\md4c"; Flags: ignoreversion
Source: "{#BuildDir}\licenses\LumaText\*"; DestDir: "{app}\licenses\LumaText"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#BuildDir}\Pulse.Index.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#BuildDir}\Pulse.Document.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#BuildDir}\Pulse.Preview.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#BuildDir}\pulse_shell.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#BuildDir}\pulse_integration.exe"; DestDir: "{app}"; Flags: ignoreversion

#ifndef AppLocalRuntime
[InstallDelete]
; Exact app-local files from older MD releases; never touch Windows runtimes.
Type: files; Name: "{app}\msvcp140.dll"
Type: files; Name: "{app}\msvcp140_atomic_wait.dll"
Type: files; Name: "{app}\vcruntime140.dll"
Type: files; Name: "{app}\vcruntime140_1.dll"
#endif

[Icons]
Name: "{group}\Pulse"; Filename: "{app}\pulse.exe"; WorkingDir: "{app}"
Name: "{group}\{cm:UninstallProgram,Pulse}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\Pulse"; Filename: "{app}\pulse.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Registry]
; Same key the in-app preference manages (src/app/app_prefs.cpp).
; Note: with an elevated install this lands in the installing user's hive.
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "Pulse"; ValueData: """{app}\pulse.exe"""; Tasks: startup; Flags: uninsdeletevalue

[Run]
; Index configuration runs in CurStepChanged so helper failures are not ignored.
Filename: "{app}\pulse.exe"; Parameters: "--seed-shell-verbs"; StatusMsg: "{cm:StatusSeedVerbs}"; Flags: runhidden waituntilterminated
Filename: "{app}\pulse.exe"; Description: "{cm:LaunchProgram,Pulse}"; Flags: nowait postinstall skipifsilent runasoriginaluser

[UninstallRun]

; Runs before files are deleted.
Filename: "{cmd}"; Parameters: "/c net stop PulseIndex >nul 2>&1 & exit /b 0"; Flags: runhidden waituntilterminated; RunOnceId: "StopPulseIndex"
Filename: "{app}\Pulse.Index.exe"; Parameters: "--uninstall"; Flags: runhidden waituntilterminated; RunOnceId: "RemovePulseIndex"

[Code]
var
  IndexDirPage: TInputDirWizardPage;
  CleanupUserData: Boolean;
  UninstallIndexPath: String;
  UpgradePrefsCaptured: Boolean;
  UpgradeStartupPresent: Boolean;
  UpgradeStartupCommand, UpgradePreviousExe: String;
  UpgradeFolderCommands: array[0..1] of String;
  UpgradeWinECommand: String;
  UpgradeThisPcCommand: String;



const
  { Win+E / taskbar File Explorer launch verb (AppPrefs::ApplyWinE). }
  WinEClsidKey = 'Software\Classes\CLSID\{52205fd8-5dfb-447d-801a-d0b52f2e83e1}';
  WinEVerbKey = 'Software\Classes\CLSID\{52205fd8-5dfb-447d-801a-d0b52f2e83e1}\shell\opennewwindow';
  { This PC open verb (default_file_manager.cpp ApplyThisPcOpen). }
  ThisPcClsidKey = 'Software\Classes\CLSID\{20D04FE0-3AEA-1069-A2D8-08002B30309D}';
  ThisPcShellKey = 'Software\Classes\CLSID\{20D04FE0-3AEA-1069-A2D8-08002B30309D}\shell';
  ThisPcCommandKey = 'Software\Classes\CLSID\{20D04FE0-3AEA-1069-A2D8-08002B30309D}\shell\open\command';

function FolderClass(Index: Integer): String;
begin
  if Index = 0 then Result := 'Directory' else Result := 'Drive';
end;

function CommandTargetsExe(Command, Exe: String): Boolean;
var
  I: Integer;
  Token: String;
begin
  Command := Trim(Command);
  Token := '';
  if Length(Command) = 0 then begin Result := False; Exit; end;
  if Command[1] = '"' then
  begin
    Delete(Command, 1, 1);
    I := Pos('"', Command);
    if I = 0 then begin Result := False; Exit; end;
    Token := Copy(Command, 1, I - 1);
  end else begin
    I := 1;
    while I <= Length(Command) do begin
      if (Command[I] = ' ') or (Command[I] = #9) then Break;
      I := I + 1;
    end;
    Token := Copy(Command, 1, I - 1);
  end;
  Result := CompareText(Token, Exe) = 0;
end;

procedure CaptureUpgradePrefs(const PreviousExe: String);
var
  I: Integer;
  Command: String;
begin
  if UpgradePrefsCaptured then Exit;
  UpgradePreviousExe := PreviousExe;
  UpgradeStartupPresent := RegQueryStringValue(HKCU,
    'Software\Microsoft\Windows\CurrentVersion\Run', 'Pulse', UpgradeStartupCommand);
  for I := 0 to 1 do
    if RegQueryStringValue(HKCU, 'Software\Classes\' + FolderClass(I) +
      '\shell\open\command', '', Command) and CommandTargetsExe(Command, PreviousExe) then
      UpgradeFolderCommands[I] := Command;
  if RegQueryStringValue(HKCU, WinEVerbKey + '\command', '', Command) and
    CommandTargetsExe(Command, PreviousExe) then UpgradeWinECommand := Command;
  if RegQueryStringValue(HKCU, ThisPcCommandKey, '', Command) and
    CommandTargetsExe(Command, PreviousExe) then UpgradeThisPcCommand := Command;
  UpgradePrefsCaptured := True;
end;
function PrepareIntegrationUpgrade: Boolean;
var
  Params: String;
  ResultCode: Integer;
begin
  Result := True;
  Params := '';
  if UpgradeFolderCommands[0] <> '' then Params := Params + ' --directory';
  if UpgradeFolderCommands[1] <> '' then Params := Params + ' --drive';
  if UpgradeWinECommand <> '' then Params := Params + ' --win-e';
  if UpgradeThisPcCommand <> '' then Params := Params + ' --this-pc';
  if Params = '' then Exit;
  ExtractTemporaryFile('pulse_integration.exe');
  Result := Exec(ExpandConstant('{tmp}\pulse_integration.exe'),
    '--prepare-upgrade --exe "' + UpgradePreviousExe + '"' + Params, '', SW_HIDE,
    ewWaitUntilTerminated, ResultCode);
  if Result then Result := ResultCode = 0;
end;
function UpgradeCommand(Command: String): String;
var
  P: Integer;
begin
  P := Pos(Lowercase(UpgradePreviousExe), Lowercase(Command));
  if P > 0 then
  begin
    Delete(Command, P, Length(UpgradePreviousExe));
    Insert(ExpandConstant('{app}\pulse.exe'), Command, P);
  end;
  Result := Command;
end;

function ShouldRestoreIntegration: Boolean;
begin
  Result := ExpandConstant('{param:PULSEUPGRADE|0}') <> '1';
end;

procedure RestoreUpgradePrefs;
var
  Params: String;
  ResultCode: Integer;
begin
  if not UpgradePrefsCaptured then Exit;
  if UpgradeStartupPresent then
    RegWriteStringValue(HKCU, 'Software\Microsoft\Windows\CurrentVersion\Run',
      'Pulse', UpgradeCommand(UpgradeStartupCommand))
  else
    RegDeleteValue(HKCU, 'Software\Microsoft\Windows\CurrentVersion\Run', 'Pulse');
  Params := '';
  if UpgradeFolderCommands[0] <> '' then Params := Params + ' --directory';
  if UpgradeFolderCommands[1] <> '' then Params := Params + ' --drive';
  if UpgradeWinECommand <> '' then Params := Params + ' --win-e';
  if UpgradeThisPcCommand <> '' then Params := Params + ' --this-pc';
  if Params = '' then Exit;
  Params := Params + ' --upgrade-from "' + UpgradePreviousExe + '" --exe "' +
    ExpandConstant('{app}\pulse.exe') + '"';
  if not Exec(ExpandConstant('{app}\pulse_integration.exe'), Params, '', SW_HIDE,
    ewWaitUntilTerminated, ResultCode) then
    Log('Could not start integration migration')
  else if ResultCode <> 0 then
    Log('Integration migration incomplete; historical backup retained');
end;
function IsChinese: Boolean;
begin
  Result := (ActiveLanguage = 'chinesesimp') or (ActiveLanguage = 'chinesetrad');
end;

function ReadJsonString(const Json, Key: String): String;
var
  I, N: Integer;
  Marker: String;
  Ch: Char;
begin
  Result := '';
  Marker := '"' + Key + '"';
  I := Pos(Marker, Json);
  if I = 0 then
    Exit;
  I := I + Length(Marker);
  N := Length(Json);
  while (I <= N) and (Json[I] <> ':') do
    I := I + 1;
  if I > N then
    Exit;
  I := I + 1;
  while (I <= N) and ((Json[I] = ' ') or (Json[I] = #9) or
    (Json[I] = #10) or (Json[I] = #13)) do
    I := I + 1;
  if (I > N) or (Json[I] <> '"') then
    Exit;
  I := I + 1;
  while I <= N do
  begin
    Ch := Json[I];
    if Ch = '"' then
      Exit;
    if Ch = '\' then
    begin
      I := I + 1;
      if I > N then
      begin
        Result := '';
        Exit;
      end;
      Ch := Json[I];
      if Ch = 'n' then
        Result := Result + #10
      else if Ch = 'r' then
        Result := Result + #13
      else if Ch = 't' then
        Result := Result + #9
      else
        Result := Result + Ch;
    end
    else
      Result := Result + Ch;
    I := I + 1;
  end;
  Result := '';
end;

function ConfiguredIndexPath: String;
var
  Json: AnsiString;
  ConfigPath: String;
begin
  Result := ExpandConstant('{commonappdata}\Pulse\Index');
  ConfigPath := ExpandConstant('{commonappdata}\Pulse\index-config.json');
  if not FileExists(ConfigPath) then
    ConfigPath := ExpandConstant('{commonappdata}\Pulse\Index\config.json');
  if LoadStringFromFile(ConfigPath, Json) then
  begin
    Result := ReadJsonString(UTF8Decode(Json), 'index_path');
    if Result = '' then
      Result := ExpandConstant('{commonappdata}\Pulse\Index');
  end;
end;

function EndsWith(const Text, Suffix: String): Boolean;
begin
  Result := (Length(Text) >= Length(Suffix)) and
    (CompareText(Copy(Text, Length(Text) - Length(Suffix) + 1,
      Length(Suffix)), Suffix) = 0);
end;

function IsPulseIndexArtifact(const Name: String): Boolean;
var
  Lower: String;
begin
  Lower := Lowercase(Name);
  Result :=
    (Lower = 'pulse-index.bin') or
    (Lower = 'pulse-index.bin.tmp') or
    (Lower = 'pulse-index.dlt') or
    ((Pos('pulse-index-', Lower) = 1) and EndsWith(Lower, '.dlt'));
end;

procedure DeletePulseIndexArtifacts(const Directory: String);
var
  FindRec: TFindRec;
  Path: String;
begin
  if (Directory = '') or not DirExists(Directory) then
    Exit;
  if FindFirst(AddBackslash(Directory) + '*', FindRec) then
  begin
    try
      repeat
        if ((FindRec.Attributes and FILE_ATTRIBUTE_DIRECTORY) = 0) and
          IsPulseIndexArtifact(FindRec.Name) then
        begin
          Path := AddBackslash(Directory) + FindRec.Name;
          if not DeleteFile(Path) then
            Log('Could not delete Pulse index artifact: ' + Path);
        end;
      until not FindNext(FindRec);
    finally
      FindClose(FindRec);
    end;
  end;
  { This succeeds only when the custom directory is empty. }
  RemoveDir(Directory);
end;

procedure CleanupPulseData;
var
  DefaultIndexPath: String;
begin
  DefaultIndexPath := ExpandConstant('{commonappdata}\Pulse\Index');
  if (UninstallIndexPath <> '') and
    (CompareText(RemoveBackslashUnlessRoot(UninstallIndexPath),
      RemoveBackslashUnlessRoot(DefaultIndexPath)) <> 0) then
    DeletePulseIndexArtifacts(UninstallIndexPath);

  DelTree(ExpandConstant('{localappdata}\Pulse'), True, True, True);
  DelTree(ExpandConstant('{commonappdata}\Pulse'), True, True, True);
end;

function InitializeUninstall: Boolean;
var
  Form: TSetupForm;
  HeadingLabel, DetailLabel: TNewStaticText;
  CleanupCheck: TNewCheckBox;
  ContinueButton, CancelButton: TNewButton;
begin
  { Silent removal is used by the upgrade path, so keep user data there. An
    interactive uninstall still defaults to removing Pulse-owned data. }
  CleanupUserData := not UninstallSilent;
  UninstallIndexPath := ConfiguredIndexPath;

  if UninstallSilent then
  begin
    Result := True;
    Exit;
  end;

  Form := CreateCustomForm(ScaleX(400), ScaleY(220), True, True);
  try
    Form.Caption := 'Pulse';
    Form.CenterOnShow := True;

    HeadingLabel := TNewStaticText.Create(Form);
    HeadingLabel.Parent := Form;
    HeadingLabel.Left := ScaleX(20);
    HeadingLabel.Top := ScaleY(16);
    HeadingLabel.Width := ScaleX(360);
    HeadingLabel.AutoSize := False;
    HeadingLabel.Font.Style := [fsBold];
    if IsChinese then
      HeadingLabel.Caption := CustomMessage('UninstHeading')
    else
      HeadingLabel.Caption := 'Uninstall Pulse';

    DetailLabel := TNewStaticText.Create(Form);
    DetailLabel.Parent := Form;
    DetailLabel.Left := ScaleX(20);
    DetailLabel.Top := ScaleY(44);
    DetailLabel.Width := ScaleX(360);
    DetailLabel.Height := ScaleY(54);
    DetailLabel.AutoSize := False;
    DetailLabel.WordWrap := True;
    if IsChinese then
      DetailLabel.Caption := CustomMessage('UninstDetail')
    else
      DetailLabel.Caption := 'Pulse will stop and remove its index service. You can also delete settings, caches, logs, and index data created by Pulse.';

    CleanupCheck := TNewCheckBox.Create(Form);
    CleanupCheck.Parent := Form;
    CleanupCheck.Left := ScaleX(20);
    CleanupCheck.Top := ScaleY(108);
    CleanupCheck.Width := ScaleX(360);
    CleanupCheck.Checked := True;
    if IsChinese then
      CleanupCheck.Caption := CustomMessage('UninstCleanup')
    else
      CleanupCheck.Caption := 'Also delete Pulse settings, caches, and index data (recommended)';

    ContinueButton := TNewButton.Create(Form);
    ContinueButton.Parent := Form;
    ContinueButton.Width := ScaleX(88);
    ContinueButton.Height := ScaleY(28);
    ContinueButton.Left := Form.ClientWidth - ScaleX(188);
    ContinueButton.Top := Form.ClientHeight - ScaleY(44);
    ContinueButton.Default := True;
    ContinueButton.ModalResult := mrOk;
    if IsChinese then
      ContinueButton.Caption := CustomMessage('UninstContinue')
    else
      ContinueButton.Caption := 'Continue';

    CancelButton := TNewButton.Create(Form);
    CancelButton.Parent := Form;
    CancelButton.Width := ScaleX(88);
    CancelButton.Height := ScaleY(28);
    CancelButton.Left := Form.ClientWidth - ScaleX(92);
    CancelButton.Top := Form.ClientHeight - ScaleY(44);
    CancelButton.Cancel := True;
    CancelButton.ModalResult := mrCancel;
    if IsChinese then
      CancelButton.Caption := CustomMessage('UninstCancel')
    else
      CancelButton.Caption := 'Cancel';

    Form.ActiveControl := ContinueButton;
    Result := Form.ShowModal = mrOk;
    if Result then
      CleanupUserData := CleanupCheck.Checked;
  finally
    Form.Free;
  end;
end;

{ File Explorer "Pulse tags" submenu (shell_tag_menu.cpp) and its dot icons.
  Pulse reinstalls it on the next launch after an upgrade (app.json pref). }
procedure DeleteShellTagMenu;
begin
  RegDeleteKeyIncludingSubkeys(HKCU, 'Software\Classes\*\shell\PulseTags');
  RegDeleteKeyIncludingSubkeys(HKCU, 'Software\Classes\Directory\shell\PulseTags');
  DelTree(ExpandConstant('{localappdata}\Pulse\tagicons'), True, True, True);
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  RestoreResult: Integer;
  RestoreMessage: String;
begin
  if (CurUninstallStep = usUninstall) and ShouldRestoreIntegration then
  begin
    RestoreResult := 3;
    if not Exec(ExpandConstant('{app}\pulse_integration.exe'),
      '--restore --exe "' + ExpandConstant('{app}\pulse.exe') + '"', '', SW_HIDE,
      ewWaitUntilTerminated, RestoreResult) then RestoreResult := 3;
    if RestoreResult <> 0 then
    begin
      if RestoreResult = 1 then RestoreMessage := CustomMessage('IntegrationRestoreIncomplete')
      else RestoreMessage := CustomMessage('IntegrationRestoreFailed');
      Log(RestoreMessage + ' (result=' + IntToStr(RestoreResult) + ')');
      SuppressibleMsgBox(RestoreMessage, mbError, MB_OK, IDOK);
    end;
  end;
  if CurUninstallStep = usPostUninstall then
  begin
    { Silent removal must clean up too. The upgrading installer saves and
      restores preferences only after the replacement files are installed. }
    RegDeleteValue(HKCU, 'Software\Microsoft\Windows\CurrentVersion\Run', 'Pulse');




    DeleteShellTagMenu;
    if CleanupUserData then
      CleanupPulseData;
  end;
end;

const
  PulseUninstallKey =
    'Software\Microsoft\Windows\CurrentVersion\Uninstall\{A3F47C2E-9D1B-4E58-8C6A-2B5D0F9E1734}_is1';

function PreviousPulseRoot(var Root: Integer): Boolean;
var
  Command: String;
begin
  Root := HKLM64;
  Result := RegQueryStringValue(Root, PulseUninstallKey, 'UninstallString', Command);
  if Result then Exit;
  Root := HKLM32;
  Result := RegQueryStringValue(Root, PulseUninstallKey, 'UninstallString', Command);
  if Result then Exit;
  Root := HKCU64;
  Result := RegQueryStringValue(Root, PulseUninstallKey, 'UninstallString', Command);
  if Result then Exit;
  Root := HKCU32;
  Result := RegQueryStringValue(Root, PulseUninstallKey, 'UninstallString', Command);
end;

function DefaultPulseDirectory(Param: String): String;
var
  Root: Integer;
  Previous: String;
begin
  Result := ExpandConstant('{autopf}\Pulse');
  if not PreviousPulseRoot(Root) then Exit;
  if not RegQueryStringValue(Root, PulseUninstallKey, 'Inno Setup: App Path', Previous) then
    RegQueryStringValue(Root, PulseUninstallKey, 'InstallLocation', Previous);
  if (Previous <> '') and FileExists(AddBackslash(Previous) + 'pulse.exe') then
  begin
    Result := RemoveBackslashUnlessRoot(Previous);
    Log('Reusing registered Pulse directory: ' + Result);
  end;
end;

function ReadPreviousUninstallCommand(var CommandLine: String): Boolean;
var
  Root: Integer;
begin
  CommandLine := '';
  Result := PreviousPulseRoot(Root);
  if Result then
    Result := RegQueryStringValue(Root, PulseUninstallKey, 'UninstallString', CommandLine);
end;

procedure InitializeWizard;
begin
  IndexDirPage := CreateInputDirPage(wpSelectTasks,
    CustomMessage('IndexPageCaption'),
    CustomMessage('IndexPageDescription'),
    CustomMessage('IndexPageSubCaption'),
    False, '');
  IndexDirPage.Add(CustomMessage('IndexPageDir'));
  IndexDirPage.Values[0] := ConfiguredIndexPath;
  if IndexDirPage.Values[0] = '' then
    IndexDirPage.Values[0] := ExpandConstant('{commonappdata}\Pulse\Index');
end;

function GetIndexPath(Param: String): String;
begin
  Result := IndexDirPage.Values[0];
end;

function SplitCommandLine(const CommandLine: String; var FileName,
  Params: String): Boolean;
var
  S: String;
  I: Integer;
begin
  FileName := '';
  Params := '';
  S := Trim(CommandLine);
  if S = '' then
  begin
    Result := False;
    Exit;
  end;

  if S[1] = '"' then
  begin
    I := 2;
    while (I <= Length(S)) and (S[I] <> '"') do
      I := I + 1;
    if I > Length(S) then
    begin
      Result := False;
      Exit;
    end;
    FileName := Copy(S, 2, I - 2);
    Params := Trim(Copy(S, I + 1, Length(S)));
  end
  else
  begin
    I := Pos(' ', S);
    if I = 0 then
      FileName := S
    else
    begin
      FileName := Copy(S, 1, I - 1);
      Params := Trim(Copy(S, I + 1, Length(S)));
    end;
  end;
  Result := FileName <> '';
end;

function UninstallPreviousVersion: String;
var
  CommandLine, FileName, Params: String;
  ResultCode: Integer;
begin
  Result := '';
  if not ReadPreviousUninstallCommand(CommandLine) then
    Exit;
  if not SplitCommandLine(CommandLine, FileName, Params) then
  begin
    Result := CustomMessage('PrevUninstParse');
    Exit;
  end;
  if not FileExists(FileName) then
  begin
    Result := CustomMessage('PrevUninstMissing');
    Exit;
  end;

  { Silent upgrades keep user data; an interactive uninstall still lets the
    user choose cleanup through the checkbox above. }
  CaptureUpgradePrefs(AddBackslash(ExtractFileDir(FileName)) + 'pulse.exe');
  if not PrepareIntegrationUpgrade then
  begin
    Result := CustomMessage('PrevUninstStart');
    Log('Cannot preserve shell integration backup; previous version was not removed');
    Exit;
  end;
  if not Exec(FileName,
    Trim(Params + ' /VERYSILENT /SUPPRESSMSGBOXES /NORESTART /PULSEUPGRADE=1'),
    '', SW_HIDE, ewWaitUntilTerminated, ResultCode) then
    Result := CustomMessage('PrevUninstStart')
  else if ResultCode <> 0 then
    Result := FmtMessage(CustomMessage('PrevUninstFailed'), [IntToStr(ResultCode)]);
end;

function RegisterPulseShutdownMessage(const Name: String): LongWord;
  external 'RegisterWindowMessageW@user32.dll stdcall';
{ Inno Setup 6 executes Pascal Script in its 32-bit Setup.e32 engine even
  when the bootstrap loader and installed application are 64-bit. }
function SendPulseShutdownMessage(Window: HWND; Msg: LongWord; WParam, LParam: Longint;
  Flags, Timeout: LongWord; var Reply: LongWord): LongWord;
  external 'SendMessageTimeoutW@user32.dll stdcall';

function PulseImageRunning(const ImageName: String): Boolean;
var
  ResultCode: Integer;
  OutputPath: String;
  Output: AnsiString;
begin
  { A failed query is not evidence that the process exited. Keep installation
    blocked unless tasklist completed successfully and its output was read. }
  Result := True;
  OutputPath := ExpandConstant('{tmp}\pulse-process-check.txt');
  if not Exec(ExpandConstant('{cmd}'),
    '/D /C ""' + ExpandConstant('{sys}\tasklist.exe') + '" /FI "IMAGENAME eq ' +
    ImageName + '" /FO CSV /NH > "' + OutputPath + '" 2>&1"',
    '', SW_HIDE, ewWaitUntilTerminated, ResultCode) then Exit;
  if ResultCode <> 0 then Exit;
  if not LoadStringFromFile(OutputPath, Output) then Exit;
  DeleteFile(OutputPath);
  if Trim(String(Output)) = '' then Exit;
  Result := Pos(Lowercase(ImageName), Lowercase(String(Output))) <> 0;
end;

function ClosePulseForUpdate: Boolean;
var
  Window: HWND;
  Msg: LongWord;
  Reply: LongWord;
  I: Integer;
begin
  Result := False;
  Msg := RegisterPulseShutdownMessage('Pulse.PrepareUpdateShutdown.v1');
  if Msg = 0 then Exit;
  { Each accepted request destroys that window. A busy/older/unresponsive
    process blocks installation; never force-terminate the UI or its work. }
  for I := 1 to 40 do
  begin
    Window := FindWindowByClassName('PulseMainWindow');
    if Window = 0 then Break;
    Reply := 0;
    if SendPulseShutdownMessage(Window, Msg, 0, 0, 2, 30000, Reply) = 0 then Exit;
    if Reply <> 1 then Exit;
  end;
  { Covers older versions without this window, headless instances and the
    brief interval between window destruction and process exit. }
  for I := 1 to 40 do
  begin
    if not PulseImageRunning('pulse.exe') then
    begin
      Result := True;
      Exit;
    end;
    Sleep(250);
  end;
end;

procedure StopPulseHosts;
var
  ResultCode: Integer;
begin
  { ClosePulseForUpdate has already confirmed all UI processes have exited. }
  Exec('net.exe', 'stop PulseIndex', '', SW_HIDE, ewWaitUntilTerminated,
    ResultCode);
  Exec('taskkill.exe', '/F /IM Pulse.Index.exe /T', '', SW_HIDE,
    ewWaitUntilTerminated, ResultCode);
  Exec('taskkill.exe', '/F /IM Pulse.Preview.exe /T', '', SW_HIDE,
    ewWaitUntilTerminated, ResultCode);
  Exec('taskkill.exe', '/F /IM pulse_shell.exe /T', '', SW_HIDE,
    ewWaitUntilTerminated, ResultCode);
end;

function WaitUntilPulseIndexGone: Boolean;
var
  I: Integer;
begin
  Result := True;
  for I := 1 to 40 do
  begin
    if not PulseImageRunning('Pulse.Index.exe') then
      Exit;
    Sleep(250);
  end;
  Result := False;
end;

function PulseIndexServiceExists: Boolean;
var
  ResultCode: Integer;
begin
  Result := Exec('cmd.exe',
    '/C sc.exe query PulseIndex | findstr /I "SERVICE_NAME" >nul',
    '', SW_HIDE, ewWaitUntilTerminated, ResultCode) and (ResultCode = 0);
end;

function WaitUntilPulseIndexServiceGone: Boolean;
var
  I: Integer;
begin
  Result := True;
  for I := 1 to 40 do
  begin
    if not PulseIndexServiceExists then
      Exit;
    Sleep(250);
  end;
  Result := False;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  PreviousUninstallError: String;
begin
  { Ask every UI process to finish safely before touching hosts or installed
    files. Stopping only the service leaves the network agent running. }
  Result := CustomMessage('PulseUpdateBusy');
  if not ClosePulseForUpdate then Exit;
  StopPulseHosts;
  WaitUntilPulseIndexGone;
  PreviousUninstallError := UninstallPreviousVersion;
  if not ClosePulseForUpdate then Exit;
  StopPulseHosts;
  WaitUntilPulseIndexGone;
  WaitUntilPulseIndexServiceGone;
  Result := PreviousUninstallError;
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  Code: Integer;
  IndexExe, Path: String;
begin
  if CurStep <> ssPostInstall then Exit;
  RestoreUpgradePrefs;
  if not WizardIsTaskSelected('indexservice') then Exit;
  IndexExe := ExpandConstant('{app}\Pulse.Index.exe');
  Path := RemoveBackslashUnlessRoot(GetIndexPath(''));
  WizardForm.StatusLabel.Caption := CustomMessage('StatusIndexPath');
  Code := -1;
  if not Exec(IndexExe, '--set-index-path "' + Path + '"', '', SW_HIDE, ewWaitUntilTerminated, Code) or (Code <> 0) then
  begin
    Log('Index location configuration failed: ' + IntToStr(Code));
    SuppressibleMsgBox(FmtMessage(CustomMessage('IndexPathFailed'), [IntToStr(Code)]),
      mbError, MB_OK, IDOK);
    Exit;
  end;
  { Configure the location before starting the service: an upgrade must not
    start a full scan only to immediately stop it for the same index path. }
  WizardForm.StatusLabel.Caption := CustomMessage('StatusIndexService');
  Code := -1;
  if not Exec(IndexExe, '--install', '', SW_HIDE, ewWaitUntilTerminated, Code) or (Code <> 0) then
  begin
    SuppressibleMsgBox(FmtMessage(CustomMessage('IndexServiceFailed'), [IntToStr(Code)]),
      mbError, MB_OK, IDOK);
    Exit;
  end;
end;
