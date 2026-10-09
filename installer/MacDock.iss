; Installateur de MacDock (plan 52) : par utilisateur, sans droits d'administrateur, dans
; %LOCALAPPDATA%\Programs\MacDock. Fabriqué par tools\make-installer.ps1 (en local comme dans release.yml) :
;   ISCC.exe /DAppVersion=0.52.0 /DBinDir=<dossier des exécutables> installer\MacDock.iss
; Paramètres de l'installateur, en plus de ceux d'Inno Setup (/VERYSILENT, /DIR=…) :
;   /NOLAUNCH   MacDock n'est pas lancé à la fin (essais)
;   /NOSTARTUP  pas de démarrage avec Windows (essais)
; Une mise à jour installée par-dessus MacDock en marche le quitte d'abord, puis le relance ; ratée ou annulée, elle
; relance l'ancien. Les réglages (%APPDATA%\MacDock) ne sont jamais touchés, même à la désinstallation.

#ifndef AppVersion
  #error Version manquante : /DAppVersion=X.Y.Z
#endif
#ifndef BinDir
  #define BinDir "..\build\Release"
#endif
; Partie numérique de la version (préversion « -rc.1 » retirée) : seule acceptée par VersionInfoVersion.
#define AppVersionNumeric Copy(AppVersion, 1, Pos("-", AppVersion + "-") - 1)
#define AppGuid "7C2E4C1B-8A3D-4F6E-9B21-5D0A3C9E7F14"

[Setup]
AppId={{{#AppGuid}}
AppName=MacDock
AppVersion={#AppVersion}
AppVerName=MacDock {#AppVersion}
AppPublisher=titilyonnais
AppPublisherURL=https://github.com/titilyonnais/MacDock
AppSupportURL=https://github.com/titilyonnais/MacDock/issues
AppUpdatesURL=https://github.com/titilyonnais/MacDock/releases
VersionInfoVersion={#AppVersionNumeric}
VersionInfoProductName=MacDock
VersionInfoDescription=Installateur de MacDock
DefaultDirName={localappdata}\Programs\MacDock
DisableDirPage=yes
DisableProgramGroupPage=yes
DisableReadyPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.22000
OutputDir=..\build\installer
OutputBaseFilename=MacDock-Setup-{#AppVersion}
SetupIconFile=..\res\dock.ico
UninstallDisplayIcon={app}\MacDock.exe
UninstallDisplayName=MacDock
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
ShowLanguageDialog=no
CloseApplications=no
RestartApplications=no

[Languages]
Name: "french"; MessagesFile: "compiler:Languages\French.isl"

[Tasks]
Name: "startup"; Description: "Démarrer MacDock à l'ouverture de session"; Check: ShowStartupTask

[Files]
Source: "{#BinDir}\MacDock.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#BinDir}\MacMenuBar.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#BinDir}\MacDockLauncher.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#BinDir}\MacDockSettings.exe"; DestDir: "{app}"; Flags: ignoreversion
; Mods Windhawk : l'app Réglages les trouve à côté des exécutables (windhawk\<id>.wh.cpp).
Source: "..\windhawk\*"; DestDir: "{app}\windhawk"; Flags: ignoreversion recursesubdirs
Source: "..\LICENSE"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\README.md"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\MacDock"; Filename: "{app}\MacDockLauncher.exe"; IconFilename: "{app}\MacDock.exe"; Comment: "Le Dock et la barre des menus"
Name: "{autoprograms}\Réglages MacDock"; Filename: "{app}\MacDockSettings.exe"

[Registry]
; Même valeur que MacDockLauncher.exe --install et que l'interrupteur de l'app Réglages.
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "MacDock"; ValueData: """{app}\MacDockLauncher.exe"""; Check: StartupWanted

[Run]
Filename: "{app}\MacDockLauncher.exe"; Description: "Lancer MacDock"; Flags: nowait postinstall; Check: LaunchWanted

[Code]
const
  WM_CLOSE = $0010;
  RunKey = 'Software\Microsoft\Windows\CurrentVersion\Run';
  UninstallKey = 'Software\Microsoft\Windows\CurrentVersion\Uninstall\{{#AppGuid}}_is1';
  Mutexes = 'Local\MacDock,Local\MacMenuBar,Local\MacDockLauncher,MacDockSettings.Instance';

var
  Upgrade: Boolean;     // MacDock déjà installé : une mise à jour
  WasRunning: Boolean;  // MacDock tournait avant l'installation
  Installed: Boolean;   // fichiers copiés
  AppDir: String;

// Paramètre de la ligne de commande (« /NOLAUNCH »…), sans tenir compte de la casse.
function ParamGiven(const Name: String): Boolean;
var
  I: Integer;
begin
  Result := False;
  for I := 1 to ParamCount do
    if CompareText(ParamStr(I), Name) = 0 then
    begin
      Result := True;
      Exit;
    end;
end;

function InitializeSetup(): Boolean;
begin
  Upgrade := RegKeyExists(HKCU, UninstallKey);
  Result := True;
end;

function ShowStartupTask(): Boolean;
begin
  Result := not Upgrade and not ParamGiven('/NOSTARTUP');
end;

// Démarrage avec Windows : à la première installation, la case (cochée) ; à une mise à jour, l'état actuel est gardé
// (l'app Réglages a pu le couper) ; jamais avec /NOSTARTUP.
function StartupWanted(): Boolean;
begin
  if ParamGiven('/NOSTARTUP') then
    Result := False
  else if Upgrade then
    Result := RegValueExists(HKCU, RunKey, 'MacDock')
  else
    Result := WizardIsTaskSelected('startup');
end;

function LaunchWanted(): Boolean;
begin
  Result := not ParamGiven('/NOLAUNCH');
end;

function MacDockRunning(): Boolean;
begin
  Result := CheckForMutexes(Mutexes);
end;

procedure CloseWindowOfClass(const ClassName: String);
var
  Wnd: HWND;
begin
  Wnd := FindWindowByClassName(ClassName);
  if Wnd <> 0 then
    PostMessage(Wnd, WM_CLOSE, 0, 0);
end;

// Quitte MacDock proprement : le Dock et la barre ferment leurs fenêtres (la barre des tâches revient), le lanceur
// s'arrête avec eux ; l'app Réglages se ferme aussi. Attend au plus 10 s.
procedure StopMacDock();
var
  I: Integer;
begin
  CloseWindowOfClass('MacDockWindow');
  CloseWindowOfClass('MacMenuBarWindow');
  CloseWindowOfClass('MacDockSettingsWindow');
  for I := 1 to 100 do
  begin
    if not MacDockRunning() then
      Exit;
    Sleep(100);
  end;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  AppDir := ExpandConstant('{app}');
  WasRunning := MacDockRunning();
  Result := '';
  if WasRunning then
  begin
    StopMacDock();
    if MacDockRunning() then
      Result := 'MacDock ne s''est pas arrêté. Quitte-le (menu  > Quitter MacDock), puis relance l''installation.';
  end;
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then
    Installed := True;
end;

// Installation ratée ou annulée : MacDock revient tel qu'il était.
procedure DeinitializeSetup();
var
  Code: Integer;
begin
  if WasRunning and not Installed and LaunchWanted() and (AppDir <> '') and FileExists(AppDir + '\MacDockLauncher.exe') then
    Exec(AppDir + '\MacDockLauncher.exe', '', AppDir, SW_SHOWNORMAL, ewNoWait, Code);
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  Value: String;
begin
  if CurUninstallStep = usUninstall then
    StopMacDock();
  // Démarrage avec Windows retiré s'il lançait cette copie (pas une autre, compilée ailleurs).
  if (CurUninstallStep = usPostUninstall) and RegQueryStringValue(HKCU, RunKey, 'MacDock', Value) and
     (Pos(Lowercase(ExpandConstant('{app}')), Lowercase(Value)) > 0) then
    RegDeleteValue(HKCU, RunKey, 'MacDock');
end;
