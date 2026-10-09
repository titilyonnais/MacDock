; Installateur de MacDock (plan 52) : par utilisateur, sans droits d'administrateur, dans
; %LOCALAPPDATA%\Programs\MacDock. Fabriqué par tools\make-installer.ps1 (en local comme dans release.yml) :
;   ISCC.exe /DAppVersion=0.52.0 /DBinDir=<dossier des exécutables> installer\MacDock.iss
; Variante d'essai (identifiant, nom et dossier à elle : une vraie installation n'est jamais touchée) :
;   /DAppGuid=<autre identifiant> /DAppTitle="MacDock (essai)" /DAppFolder=MacDock-essai
; Paramètres de l'installateur, en plus de ceux d'Inno Setup (/VERYSILENT, /DIR=…) :
;   /NOLAUNCH    MacDock n'est pas lancé à la fin (essais)
;   /NOSTARTUP   pas de démarrage avec Windows (essais)
;   /RELAUNCH    MacDock relancé même s'il ne tournait pas et que l'installation échoue (mise à jour au démarrage)
; Désinstallation d'essai : MACDOCK_KEEP_THEME=1 dans l'environnement, le thème de Windows n'est pas rétabli (un
; paramètre ne suffirait pas : le désinstalleur se relance depuis %TEMP% sans les paramètres maison).
; Une mise à jour installée par-dessus MacDock en marche le quitte d'abord (ordre d'arrêt au lanceur, fenêtres
; fermées), puis le relance ; ratée ou annulée, elle relance l'ancien. Les réglages (%APPDATA%\MacDock) ne sont jamais
; touchés, même à la désinstallation.

#ifndef AppVersion
  #error Version manquante : /DAppVersion=X.Y.Z
#endif
#ifndef BinDir
  #define BinDir "..\build\Release"
#endif
#ifndef AppGuid
  #define AppGuid "7C2E4C1B-8A3D-4F6E-9B21-5D0A3C9E7F14"
#endif
#ifndef AppTitle
  #define AppTitle "MacDock"
#endif
#ifndef AppFolder
  #define AppFolder "MacDock"
#endif
; Partie numérique de la version (préversion « -rc.1 » retirée) : seule acceptée par VersionInfoVersion.
#define AppVersionNumeric Copy(AppVersion, 1, Pos("-", AppVersion + "-") - 1)

[Setup]
AppId={{{#AppGuid}}
AppName={#AppTitle}
AppVersion={#AppVersion}
AppVerName={#AppTitle} {#AppVersion}
AppPublisher=titilyonnais
AppPublisherURL=https://github.com/titilyonnais/MacDock
AppSupportURL=https://github.com/titilyonnais/MacDock/issues
AppUpdatesURL=https://github.com/titilyonnais/MacDock/releases
VersionInfoVersion={#AppVersionNumeric}
VersionInfoProductName=MacDock
VersionInfoDescription=Installateur de MacDock
DefaultDirName={localappdata}\Programs\{#AppFolder}
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
UninstallDisplayName={#AppTitle}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
ShowLanguageDialog=no
CloseApplications=no
RestartApplications=no
; Deux installateurs à la fois (mise à jour automatique et installation à la main) : le second attend son tour.
SetupMutex=MacDockSetup

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
Source: "..\windhawk\macdock-hide-taskbar.wh.cpp"; DestDir: "{app}\windhawk"; Flags: ignoreversion
Source: "..\windhawk\macdock-look.wh.cpp"; DestDir: "{app}\windhawk"; Flags: ignoreversion
Source: "..\windhawk\install-macdock-look.ps1"; DestDir: "{app}\windhawk"; Flags: ignoreversion
Source: "..\windhawk\installer-macdock-look.cmd"; DestDir: "{app}\windhawk"; Flags: ignoreversion
Source: "..\windhawk\retirer-macdock-look.cmd"; DestDir: "{app}\windhawk"; Flags: ignoreversion
Source: "..\LICENSE"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\README.md"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\{#AppTitle}"; Filename: "{app}\MacDockLauncher.exe"; IconFilename: "{app}\MacDock.exe"; Comment: "Le Dock et la barre des menus"
Name: "{autoprograms}\Réglages {#AppTitle}"; Filename: "{app}\MacDockSettings.exe"

[Registry]
; Même valeur que MacDockLauncher.exe --install et que l'interrupteur de l'app Réglages.
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "MacDock"; ValueData: """{app}\MacDockLauncher.exe"""; Check: StartupWanted

[Run]
Filename: "{app}\MacDockLauncher.exe"; Description: "Lancer MacDock"; Flags: nowait postinstall; Check: LaunchWanted


[Code]
const
  WM_CLOSE = $0010;
  EVENT_MODIFY_STATE = $0002;
  RunKey = 'Software\Microsoft\Windows\CurrentVersion\Run';
  UninstallKey = 'Software\Microsoft\Windows\CurrentVersion\Uninstall\{{#AppGuid}}_is1';
  // Tout ce qui garde un fichier de MacDock ouvert : Dock, barre, lanceur, Réglages, et une recherche de mise à jour
  // (MacDockLauncher.exe --check-update, verrou des mises à jour), qui s'arrête à l'ordre d'arrêt.
  Mutexes = 'Local\MacDock,Local\MacMenuBar,Local\MacDockLauncher,MacDockSettings.Instance,Local\MacDockUpdate';
  // Relance : seuls le Dock, la barre et le lanceur comptent (Réglages qui se ferme ne la retarde pas).
  LauncherMutexes = 'Local\MacDock,Local\MacMenuBar,Local\MacDockLauncher';
  QuitEvent = 'Local\MacDockQuit';
  StopFailed = 'MacDock ne s''est pas arrêté. Quitte-le (clic droit sur le Dock > Quitter MacDock), ferme Réglages MacDock, puis recommence.';

var
  Upgrade: Boolean;     // MacDock déjà installé : une mise à jour
  WasRunning: Boolean;  // MacDock tournait avant l'installation (ou /RELAUNCH)
  Installed: Boolean;   // fichiers copiés
  AppDir: String;

function OpenEvent(dwDesiredAccess: Cardinal; bInheritHandle: Integer; lpName: String): THandle;
  external 'OpenEventW@kernel32.dll stdcall';
function SetEvent(hEvent: THandle): Integer;
  external 'SetEvent@kernel32.dll stdcall';
function CloseHandle(hObject: THandle): Integer;
  external 'CloseHandle@kernel32.dll stdcall';

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

// Essais : la désinstallation ne rétablit pas le thème de Windows.
function KeepTheme(): Boolean;
begin
  Result := GetEnv('MACDOCK_KEEP_THEME') = '1';
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

// Quitte MacDock proprement, au plus 30 s :
// - ordre d'arrêt au lanceur (0.52.0 et après) : il ferme le Dock puis la barre (la barre des tâches revient) et ne
//   relance plus rien, même un enfant qui plante ;
// - fenêtres du Dock, de la barre et de l'app Réglages fermées, à chaque seconde (lanceurs plus anciens, enfant
//   relancé après un plantage).
procedure StopMacDock();
var
  I: Integer;
  Quit: THandle;
begin
  Quit := OpenEvent(EVENT_MODIFY_STATE, 0, QuitEvent);
  if Quit <> 0 then
  begin
    SetEvent(Quit);
    CloseHandle(Quit);
  end;
  for I := 0 to 300 do
  begin
    if not MacDockRunning() then
      Exit;
    if I mod 10 = 0 then
    begin
      CloseWindowOfClass('MacDockWindow');
      CloseWindowOfClass('MacMenuBarWindow');
      CloseWindowOfClass('MacDockSettingsWindow');
    end;
    Sleep(100);
  end;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  AppDir := ExpandConstant('{app}');
  WasRunning := WasRunning or MacDockRunning() or ParamGiven('/RELAUNCH');
  Result := '';
  if MacDockRunning() then
  begin
    StopMacDock();
    if MacDockRunning() then
      Result := StopFailed;
  end;
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then
    Installed := True;
end;

// Installation ratée ou annulée : MacDock revient tel qu'il était, une fois l'ancien tout à fait arrêté (sinon le
// lanceur relancé trouverait encore le sien et sortirait aussitôt).
procedure DeinitializeSetup();
var
  Code, I: Integer;
begin
  if WasRunning and not Installed and LaunchWanted() and (AppDir <> '') and FileExists(AppDir + '\MacDockLauncher.exe') then
  begin
    for I := 1 to 150 do
    begin
      if not CheckForMutexes(LauncherMutexes) then
        Break;
      Sleep(100);
    end;
    if not CheckForMutexes(LauncherMutexes) then
      Exec(AppDir + '\MacDockLauncher.exe', '', AppDir, SW_SHOWNORMAL, ewNoWait, Code);
  end;
end;

// Désinstallation : MacDock d'abord arrêté, sinon rien n'est retiré (fichiers verrouillés).
function InitializeUninstall(): Boolean;
begin
  StopMacDock();
  Result := not MacDockRunning();
  if not Result then
    MsgBox(StopFailed, mbError, MB_OK);
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  Value: String;
  Code: Integer;
begin
  // Thème macOS appliqué (curseurs, fond d'écran) : celui de Windows est rétabli, avant que les fichiers partent ; sans
  // sauvegarde, rien ne change. Décidé ici, à la désinstallation : la condition d'une entrée [UninstallRun] serait
  // évaluée à l'installation.
  if (CurUninstallStep = usUninstall) and not KeepTheme() then
    Exec(ExpandConstant('{app}\MacDock.exe'), '--theme restore', '', SW_HIDE, ewWaitUntilTerminated, Code);
  // Démarrage avec Windows retiré s'il lançait exactement cette copie (pas une autre, compilée ailleurs).
  if (CurUninstallStep = usPostUninstall) and RegQueryStringValue(HKCU, RunKey, 'MacDock', Value) and
     (CompareText(Value, '"' + ExpandConstant('{app}') + '\MacDockLauncher.exe"') = 0) then
    RegDeleteValue(HKCU, RunKey, 'MacDock');
end;
