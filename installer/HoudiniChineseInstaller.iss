#define ProductVersion "21.0.440"
#define ProductName "Houdini 21.0.440 中文本地化"
#define ProductId "HoudiniChinese.21.0.440"
#define RepositoryUrl "https://github.com/Neocvsu-commits/Houdini-21.0.440-Chinese-localization-"
#define PayloadDir "..\release\HoudiniChinese-21.0.440"

[Setup]
AppId={#ProductId}
AppName={#ProductName}
AppVersion={#ProductVersion}
AppVerName={#ProductName}
AppPublisher=Neocvsu-commits
AppPublisherURL={#RepositoryUrl}
AppSupportURL={#RepositoryUrl}/issues
AppUpdatesURL={#RepositoryUrl}/releases
DefaultDirName={localappdata}\HoudiniChinese21Installer
DisableDirPage=yes
DisableProgramGroupPage=yes
UninstallFilesDir={app}\uninstall
UninstallDisplayName={#ProductName}
OutputDir=..\release
OutputBaseFilename=HoudiniChineseInstaller-21.0.440
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
PrivilegesRequired=lowest
WizardStyle=modern
WizardSizePercent=110
Compression=lzma2
SolidCompression=yes
CloseApplications=no
RestartApplications=no
RestartIfNeededByRun=no
SetupLogging=yes
VersionInfoVersion=21.0.440.0
VersionInfoProductName={#ProductName}
VersionInfoDescription=Houdini 21.0.440 简体中文本地化安装器
VersionInfoCompany=Neocvsu-commits

[Languages]
Name: "chinesesimp"; MessagesFile: "ChineseSimplified.isl"

[LangOptions]
DialogFontName=Microsoft YaHei UI
DialogFontSize=10

[Messages]
WelcomeLabel1=欢迎安装 {#ProductName}
WelcomeLabel2=安装器会把中文本地化包安装到 Houdini 21.0 的用户 packages 目录，不会修改 Houdini 程序目录或工程文件。%n%n继续前请保存工作并关闭 Houdini。
FinishedLabel=中文本地化已经安装完成。重新启动 Houdini 21.0.440 后，可按 F3 或点击顶部“中/英”按钮切换语言。%n%n以后可在 Windows“已安装的应用”中卸载并恢复安装前的同名包。

[Files]
Source: "{#PayloadDir}\HoudiniChinese\*"; DestDir: "{code:GetPackageFolder}"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#PayloadDir}\HoudiniChinese.json"; DestDir: "{code:GetPackagesDir}"; Flags: ignoreversion
Source: "{#PayloadDir}\checksums.json"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#PayloadDir}\README-21.md"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{userprograms}\Houdini 21.0.440 中文本地化\卸载中文本地化"; Filename: "{uninstallexe}"

[Code]
var
  PackagesPage: TInputDirWizardPage;
  SelectedPackagesDir: String;
  SavedBackupPath: String;
  BackupCreatedThisRun: Boolean;
  InstallCompleted: Boolean;

function StateFile: String;
begin
  Result := AddBackslash(ExpandConstant('{app}')) + 'install-state.ini';
end;

function NormalizeDirectory(Value: String): String;
begin
  Result := RemoveBackslashUnlessRoot(Trim(Value));
end;

function DefaultPackagesDir: String;
var
  Pref: String;
begin
  Pref := GetEnv('HOUDINI_USER_PREF_DIR');
  if Pref = '' then
    Pref := ExpandConstant('{userdocs}\houdini21.0')
  else begin
    StringChangeEx(Pref, '__HVER__', '21.0', True);
    StringChangeEx(Pref, '$HOME', ExpandConstant('{userprofile}'), True);
  end;
  Result := AddBackslash(NormalizeDirectory(Pref)) + 'packages';
end;

function GetPackagesDir(Param: String): String;
begin
  if SelectedPackagesDir <> '' then
    Result := SelectedPackagesDir
  else if Assigned(PackagesPage) then
    Result := NormalizeDirectory(PackagesPage.Values[0])
  else
    Result := NormalizeDirectory(GetIniString('Install', 'PackagesDir', DefaultPackagesDir, StateFile));
end;

function GetPackageFolder(Param: String): String;
begin
  Result := AddBackslash(GetPackagesDir('')) + 'HoudiniChinese';
end;

function PackageJson: String;
begin
  Result := AddBackslash(GetPackagesDir('')) + 'HoudiniChinese.json';
end;

function ValidatePackagesDir(Directory: String): String;
begin
  Directory := NormalizeDirectory(Directory);
  if Directory = '' then begin
    Result := '请选择 Houdini 21.0 的 packages 目录。';
    Exit;
  end;
  if CompareText(ExtractFileName(Directory), 'packages') <> 0 then begin
    Result := '目标目录必须是 Houdini 用户配置目录中的 packages 文件夹。';
    Exit;
  end;
  if Pos(Lowercase('\houdini21.0\'), Lowercase(AddBackslash(Directory))) = 0 then begin
    Result := '当前路径看起来不属于 houdini21.0。请确认版本后再安装。';
    Exit;
  end;
  Result := '';
end;

function ProcessRunning(ImageName: String): Boolean;
var
  ResultCode: Integer;
  Arguments: String;
begin
  Arguments := '/D /Q /C tasklist.exe /FI "IMAGENAME eq ' + ImageName +
    '" /NH | find.exe /I "' + ImageName + '" >nul';
  Result := Exec(ExpandConstant('{cmd}'), Arguments, '', SW_HIDE,
    ewWaitUntilTerminated, ResultCode) and (ResultCode = 0);
end;

function AnyHoudiniRunning: Boolean;
begin
  Result := ProcessRunning('houdini.exe') or
    ProcessRunning('houdinifx.exe') or
    ProcessRunning('houdinicore.exe') or
    ProcessRunning('houdiniindie.exe');
end;

function CloseHoudiniProcesses: Boolean;
var
  ResultCode: Integer;
begin
  Exec(ExpandConstant('{cmd}'),
    '/D /Q /C taskkill.exe /F /IM houdini.exe /IM houdinifx.exe /IM houdinicore.exe /IM houdiniindie.exe >nul 2>&1',
    '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
  Sleep(600);
  Result := not AnyHoudiniRunning;
end;

function EnsureHoudiniClosed: Boolean;
begin
  Result := True;
  if not AnyHoudiniRunning then Exit;
  if SuppressibleMsgBox(
       '检测到 Houdini 正在运行。继续安装或卸载前必须关闭它。' + #13#10 + #13#10 +
       '是否由安装器强制结束 Houdini 进程？未保存的工程内容会丢失。',
       mbConfirmation, MB_YESNO, IDNO) <> IDYES then begin
    Result := False;
    Exit;
  end;
  Result := CloseHoudiniProcesses;
  if not Result then
    SuppressibleMsgBox('无法结束全部 Houdini 进程。请在任务管理器中关闭后重试。',
      mbError, MB_OK, IDOK);
end;

function NextButtonClick(CurPageID: Integer): Boolean;
var
  ErrorText: String;
begin
  Result := True;
  if CurPageID <> PackagesPage.ID then Exit;
  ErrorText := ValidatePackagesDir(PackagesPage.Values[0]);
  Result := ErrorText = '';
  if not Result then
    SuppressibleMsgBox(ErrorText, mbError, MB_OK, IDOK);
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  SelectedPackagesDir := NormalizeDirectory(PackagesPage.Values[0]);
  Result := ValidatePackagesDir(SelectedPackagesDir);
  if Result <> '' then Exit;
  if not EnsureHoudiniClosed then
    Result := 'Houdini 仍在运行，安装尚未开始。';
end;

procedure RestoreBackup(BackupPath: String);
var
  PackagesDir, CurrentFolder, CurrentJson, BackupFolder, BackupJson: String;
begin
  if BackupPath = '' then Exit;
  PackagesDir := GetPackagesDir('');
  if ValidatePackagesDir(PackagesDir) <> '' then
    RaiseException('拒绝在无效的 packages 路径中执行恢复。');
  CurrentFolder := AddBackslash(PackagesDir) + 'HoudiniChinese';
  CurrentJson := AddBackslash(PackagesDir) + 'HoudiniChinese.json';
  BackupFolder := AddBackslash(BackupPath) + 'HoudiniChinese';
  BackupJson := AddBackslash(BackupPath) + 'HoudiniChinese.json';
  if DirExists(CurrentFolder) then
    DelTree(CurrentFolder, True, True, True);
  if FileExists(CurrentJson) then
    DeleteFile(CurrentJson);
  ForceDirectories(PackagesDir);
  if DirExists(BackupFolder) and not RenameFile(BackupFolder, CurrentFolder) then
    RaiseException('无法恢复安装前的 HoudiniChinese 文件夹：' + BackupFolder);
  if FileExists(BackupJson) and not RenameFile(BackupJson, CurrentJson) then
    RaiseException('无法恢复安装前的 HoudiniChinese.json：' + BackupJson);
  RemoveDir(BackupPath);
end;

procedure BackupExistingPackage;
var
  BackupRoot, BackupPath, CurrentFolder, CurrentJson: String;
  FolderMoved, JsonMoved: Boolean;
begin
  if IniKeyExists('Install', 'BackupPath', StateFile) then begin
    SavedBackupPath := GetIniString('Install', 'BackupPath', '', StateFile);
    Exit;
  end;
  CurrentFolder := GetPackageFolder('');
  CurrentJson := PackageJson;
  if not DirExists(CurrentFolder) and not FileExists(CurrentJson) then begin
    SavedBackupPath := '';
    Exit;
  end;
  BackupRoot := AddBackslash(ExtractFileDir(GetPackagesDir(''))) + 'HoudiniChinese-backups';
  BackupPath := AddBackslash(BackupRoot) + 'installer-' +
    GetDateTimeString('yyyymmdd-hhnnss-zzz', '-', ':');
  if not ForceDirectories(BackupPath) then
    RaiseException('无法创建升级备份目录：' + BackupPath);
  FolderMoved := False;
  JsonMoved := False;
  try
    if DirExists(CurrentFolder) then begin
      if not RenameFile(CurrentFolder, AddBackslash(BackupPath) + 'HoudiniChinese') then
        RaiseException('无法备份现有 HoudiniChinese 文件夹。');
      FolderMoved := True;
    end;
    if FileExists(CurrentJson) then begin
      if not RenameFile(CurrentJson, AddBackslash(BackupPath) + 'HoudiniChinese.json') then
        RaiseException('无法备份现有 HoudiniChinese.json。');
      JsonMoved := True;
    end;
  except
    if FolderMoved then
      RenameFile(AddBackslash(BackupPath) + 'HoudiniChinese', CurrentFolder);
    if JsonMoved then
      RenameFile(AddBackslash(BackupPath) + 'HoudiniChinese.json', CurrentJson);
    RaiseException(GetExceptionMessage);
  end;
  SavedBackupPath := BackupPath;
  BackupCreatedThisRun := True;
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssInstall then begin
    ForceDirectories(ExpandConstant('{app}'));
    ForceDirectories(GetPackagesDir(''));
    BackupExistingPackage;
  end;
  if CurStep = ssPostInstall then begin
    if not FileExists(AddBackslash(GetPackageFolder('')) + 'HoudiniChineseHook.dll') or
       not FileExists(AddBackslash(GetPackageFolder('')) + 'bin\HoudiniChineseEngine.dll') or
       not FileExists(PackageJson) then
      RaiseException('安装后的关键文件不完整，正在恢复安装前状态。');
    if not SetIniString('Install', 'PackagesDir', GetPackagesDir(''), StateFile) or
       not SetIniString('Install', 'BackupPath', SavedBackupPath, StateFile) then
      RaiseException('无法保存安装记录，正在恢复安装前状态。');
    InstallCompleted := True;
  end;
end;

procedure DeinitializeSetup;
begin
  if BackupCreatedThisRun and not InstallCompleted then begin
    try
      RestoreBackup(SavedBackupPath);
    except
      SuppressibleMsgBox('安装未完成，且自动恢复旧版本失败：' + GetExceptionMessage,
        mbError, MB_OK, IDOK);
    end;
  end;
end;

function InitializeUninstall: Boolean;
begin
  SelectedPackagesDir := NormalizeDirectory(GetIniString(
    'Install', 'PackagesDir', DefaultPackagesDir, StateFile));
  SavedBackupPath := GetIniString('Install', 'BackupPath', '', StateFile);
  Result := ValidatePackagesDir(SelectedPackagesDir) = '';
  if not Result then begin
    SuppressibleMsgBox('安装记录中的 packages 路径无效，已停止卸载。', mbError, MB_OK, IDOK);
    Exit;
  end;
  Result := EnsureHoudiniClosed;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usPostUninstall then begin
    try
      if DirExists(GetPackageFolder('')) then
        DelTree(GetPackageFolder(''), True, True, True);
      if FileExists(PackageJson) then
        DeleteFile(PackageJson);
      RestoreBackup(SavedBackupPath);
    except
      SuppressibleMsgBox('卸载已完成，但恢复安装前版本时发生错误：' + GetExceptionMessage,
        mbError, MB_OK, IDOK);
    end;
  end;
end;

procedure InitializeWizard;
var
  CommandLinePackagesDir: String;
begin
  CommandLinePackagesDir := ExpandConstant('{param:PACKAGESDIR|}');
  PackagesPage := CreateInputDirPage(wpWelcome,
    '选择 Houdini 用户包目录',
    '安装器已自动选择 Houdini 21.0 的 packages 目录。',
    '通常无需修改。若设置了自定义 HOUDINI_USER_PREF_DIR，请确认下方路径正确。',
    False, '');
  PackagesPage.Add('Houdini 21.0 packages 目录：');
  if CommandLinePackagesDir <> '' then
    PackagesPage.Values[0] := NormalizeDirectory(CommandLinePackagesDir)
  else
    PackagesPage.Values[0] := DefaultPackagesDir;
  WizardForm.WelcomeLabel1.Alignment := taLeftJustify;
  WizardForm.WelcomeLabel2.Alignment := taLeftJustify;
  WizardForm.FinishedHeadingLabel.Alignment := taLeftJustify;
  WizardForm.FinishedLabel.Alignment := taLeftJustify;
end;
