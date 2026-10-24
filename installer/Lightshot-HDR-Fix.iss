#ifndef PayloadDir
  #define PayloadDir "..\build\installer-payload"
#endif
#ifndef InstallerOutput
  #define InstallerOutput "..\dist"
#endif
#define FixVersion "0.3.0"

[Setup]
AppId=LightshotHDRFixUser
AppName=Lightshot HDR Fix
AppVersion={#FixVersion}
AppPublisher=Joaovitorsw
AppPublisherURL=https://github.com/Joaovitorsw/lightshot-hdr-fix
AppSupportURL=https://github.com/Joaovitorsw/lightshot-hdr-fix/issues
DefaultDirName={localappdata}\Programs\LightshotHDRFix
DefaultGroupName=Lightshot HDR Fix
PrivilegesRequired=lowest
MinVersion=10.0.17763
WizardStyle=modern
DisableDirPage=yes
DisableProgramGroupPage=yes
DisableReadyPage=yes
DisableWelcomePage=no
CloseApplications=no
RestartApplications=no
OutputDir={#InstallerOutput}
OutputBaseFilename=Lightshot-HDR-Fix-Setup
Compression=lzma2
SolidCompression=yes
SetupLogging=yes
UninstallDisplayIcon={app}\Lightshot-v{#FixVersion}\Lightshot.exe
VersionInfoDescription=Lightshot HDR Fix Installer
VersionInfoVersion=0.3.0.0
VersionInfoCopyright=Joaovitorsw

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "brazilianportuguese"; MessagesFile: "compiler:Languages\BrazilianPortuguese.isl"
Name: "spanish"; MessagesFile: "compiler:Languages\Spanish.isl"
Name: "french"; MessagesFile: "compiler:Languages\French.isl"
Name: "german"; MessagesFile: "compiler:Languages\German.isl"
Name: "japanese"; MessagesFile: "compiler:Languages\Japanese.isl"

[CustomMessages]
SourceTitle=Find your Lightshot installation
SourceSubtitle=Select the folder containing the original Lightshot.exe and Lightshot.dll.
SourceDescription=A compatible installed copy is required. Your original files will remain unchanged.
SourceFolder=Original Lightshot folder:
SourceError=The supported Lightshot installation was not found. Select its internal version folder (5.5.0.7).
InstallError=Could not create the corrected copy. Original installation remains unchanged.
LaunchDescription=Open Lightshot HDR Fix now
CloseQuestion=Lightshot is running. Close that instance and open the corrected copy? An active screenshot selection will be cancelled.
UnknownProcess=Another Lightshot version is running. Close it manually before opening the corrected copy.
UninstallQuestion=Close this corrected Lightshot copy before uninstalling it?
DesktopShortcut=Create a desktop shortcut
brazilianportuguese.SourceTitle=Localizar a instalação do Lightshot
brazilianportuguese.SourceSubtitle=Selecione a pasta com Lightshot.exe e Lightshot.dll originais.
brazilianportuguese.SourceDescription=É necessária uma instalação compatível. Seus arquivos originais serão preservados.
brazilianportuguese.SourceFolder=Pasta do Lightshot original:
brazilianportuguese.SourceError=Instalação compatível não encontrada. Selecione a pasta interna da versão 5.5.0.7.
brazilianportuguese.InstallError=Não foi possível criar a cópia corrigida. A instalação original foi preservada.
brazilianportuguese.LaunchDescription=Abrir o Lightshot HDR Fix agora
brazilianportuguese.CloseQuestion=O Lightshot está aberto. Fechar essa instância e abrir a cópia corrigida? Uma seleção de print ativa será cancelada.
brazilianportuguese.UnknownProcess=Outra versão do Lightshot está aberta. Feche-a manualmente antes de abrir a cópia corrigida.
brazilianportuguese.UninstallQuestion=Fechar esta cópia corrigida antes de desinstalar?
brazilianportuguese.DesktopShortcut=Criar atalho na área de trabalho

[Tasks]
Name: "desktopicon"; Description: "{cm:DesktopShortcut}"; Flags: unchecked

[Files]
Source: "{#PayloadDir}\DXGIODScreenshot.dll"; DestDir: "{app}\tools"; Flags: ignoreversion
Source: "{#PayloadDir}\patch-spec.json"; DestDir: "{app}\tools"; Flags: ignoreversion
Source: "Install-Core.ps1"; DestDir: "{app}\tools"; Flags: ignoreversion
Source: "Launch-Fix.ps1"; DestDir: "{app}\tools"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\Lightshot HDR Fix"; Filename: "{win}\System32\WindowsPowerShell\v1.0\powershell.exe"; Parameters: "{code:LaunchParameters}"; WorkingDir: "{app}"; IconFilename: "{app}\Lightshot-v{#FixVersion}\Lightshot.exe"
Name: "{autodesktop}\Lightshot HDR Fix"; Filename: "{win}\System32\WindowsPowerShell\v1.0\powershell.exe"; Parameters: "{code:LaunchParameters}"; WorkingDir: "{app}"; IconFilename: "{app}\Lightshot-v{#FixVersion}\Lightshot.exe"; Tasks: desktopicon

[Run]
Filename: "{code:PowerShellPath}"; Parameters: "{code:LaunchParameters}"; Description: "{cm:LaunchDescription}"; Flags: postinstall skipifsilent runhidden waituntilterminated

[Code]
var
  SourcePage: TInputDirWizardPage;
  SourceDetected, Extracted: Boolean;

function Quote(const Value: String): String;
begin
  Result := '"' + Value + '"';
end;

function PowerShellPath(Param: String): String;
begin
  if IsWin64 then
    Result := ExpandConstant('{sysnative}\WindowsPowerShell\v1.0\powershell.exe')
  else
    Result := ExpandConstant('{sys}\WindowsPowerShell\v1.0\powershell.exe');
end;

function ReadUtf8(const FileName: String): String;
var
  Data: AnsiString;
begin
  Result := '';
  if LoadStringFromFile(FileName, Data) then Result := Trim(UTF8Decode(Data));
end;

procedure EnsureTemporaryFiles;
begin
  if Extracted then Exit;
  ExtractTemporaryFile('DXGIODScreenshot.dll');
  ExtractTemporaryFile('patch-spec.json');
  ExtractTemporaryFile('Install-Core.ps1');
  Extracted := True;
end;

function RunEngine(const Action, Source, Destination: String; var ErrorText: String): Boolean;
var
  Params, ReportFile: String;
  ExitCode: Integer;
begin
  EnsureTemporaryFiles;
  ReportFile := ExpandConstant('{tmp}\hdr-install-report.json');
  DeleteFile(ReportFile + '.error.txt');
  DeleteFile(ReportFile + '.source.txt');
  Params := '-NoProfile -NonInteractive -ExecutionPolicy Bypass -File ' +
    Quote(ExpandConstant('{tmp}\Install-Core.ps1')) + ' -Action ' + Action +
    ' -Spec ' + Quote(ExpandConstant('{tmp}\patch-spec.json')) +
    ' -NewDll ' + Quote(ExpandConstant('{tmp}\DXGIODScreenshot.dll')) +
    ' -Report ' + Quote(ReportFile);
  if Source <> '' then Params := Params + ' -Source ' + Quote(Source);
  if Destination <> '' then Params := Params + ' -Destination ' + Quote(Destination);
  Result := Exec(PowerShellPath(''), Params, '', SW_HIDE, ewWaitUntilTerminated, ExitCode);
  Result := Result and (ExitCode = 0);
  ErrorText := ReadUtf8(ReportFile + '.error.txt');
  Log('HDR engine ' + Action + ' exit=' + IntToStr(ExitCode) + ' error=' + ErrorText);
  if (not Result) and (ErrorText = '') then ErrorText := CustomMessage('SourceError');
end;

procedure InitializeWizard;
var
  ErrorText, Source: String;
begin
  SourcePage := CreateInputDirPage(wpWelcome, CustomMessage('SourceTitle'),
    CustomMessage('SourceSubtitle'), CustomMessage('SourceDescription'), False, '');
  SourcePage.Add(CustomMessage('SourceFolder'));
  Source := ExpandConstant('{param:LIGHTSHOTSOURCE|}');
  SourceDetected := RunEngine('Detect', Source, '', ErrorText);
  if SourceDetected then Source := ReadUtf8(ExpandConstant('{tmp}\hdr-install-report.json.source.txt'));
  if SourceDetected then Log('HDR source detected, length=' + IntToStr(Length(Source)))
  else Log('HDR detection failed: ' + ErrorText);
  SourcePage.Values[0] := Source;
end;

function ShouldSkipPage(PageID: Integer): Boolean;
begin
  Result := (PageID = SourcePage.ID) and SourceDetected;
end;

function NextButtonClick(CurPageID: Integer): Boolean;
var
  ErrorText: String;
begin
  Result := True;
  if CurPageID = SourcePage.ID then begin
    Result := RunEngine('Validate', SourcePage.Values[0], '', ErrorText);
    if not Result then MsgBox(ErrorText, mbError, MB_OK);
  end;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  ErrorText: String;
begin
  Result := '';
  if not RunEngine('Validate', SourcePage.Values[0],
    ExpandConstant('{app}\Lightshot-v{#FixVersion}'), ErrorText) then begin
    Result := ErrorText;
    Exit;
  end;
  if not ForceDirectories(ExpandConstant('{app}')) then begin
    Result := CustomMessage('InstallError');
    Exit;
  end;
  if not RunEngine('Install', SourcePage.Values[0],
    ExpandConstant('{app}\Lightshot-v{#FixVersion}'), ErrorText) then
    Result := CustomMessage('InstallError') + #13#10 + ErrorText;
end;

function LaunchParameters(Param: String): String;
begin
  Result := '-WindowStyle Hidden -NoProfile -ExecutionPolicy Bypass -File ' +
    Quote(ExpandConstant('{app}\tools\Launch-Fix.ps1')) + ' -Mode Interactive' +
    ' -AppExe ' + Quote(ExpandConstant('{app}\Lightshot-v{#FixVersion}\Lightshot.exe')) +
    ' -Spec ' + Quote(ExpandConstant('{app}\tools\patch-spec.json'));
end;

function InitializeUninstall: Boolean;
var
  Params, Target: String;
  ExitCode: Integer;
begin
  Result := True;
  Target := ExpandConstant('{app}\Lightshot-v{#FixVersion}\Lightshot.exe');
  if not FileExists(Target) then Exit;
  if not UninstallSilent then
    if MsgBox(CustomMessage('UninstallQuestion'), mbConfirmation, MB_YESNO) <> IDYES then begin
      Result := False; Exit;
    end;
  Params := '-NoProfile -NonInteractive -ExecutionPolicy Bypass -File ' +
    Quote(ExpandConstant('{app}\tools\Launch-Fix.ps1')) + ' -Mode StopCopy -CloseCompatible -AppExe ' +
    Quote(Target) + ' -Spec ' + Quote(ExpandConstant('{app}\tools\patch-spec.json'));
  Result := Exec(PowerShellPath(''), Params, '', SW_HIDE, ewWaitUntilTerminated, ExitCode);
  Result := Result and (ExitCode = 0);
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  Params: String;
  ExitCode: Integer;
begin
  if CurUninstallStep <> usUninstall then Exit;
  Params := '-NoProfile -NonInteractive -ExecutionPolicy Bypass -File ' +
    Quote(ExpandConstant('{app}\tools\Install-Core.ps1')) + ' -Action Uninstall -Destination ' +
    Quote(ExpandConstant('{app}\Lightshot-v{#FixVersion}')) + ' -Spec ' +
    Quote(ExpandConstant('{app}\tools\patch-spec.json'));
  if not Exec(PowerShellPath(''), Params, '', SW_HIDE, ewWaitUntilTerminated, ExitCode) or
    (ExitCode <> 0) then RaiseException('Could not clean the managed copy. Close Lightshot and try again.');
end;
