; Inno Setup script for Vila GLT — Live Translate (obs-vila-glt).
; Compiled in CI with:  ISCC /DMyAppVersion=<version> installer\obs-vila-glt.iss
;
; Installs into the plugin location OBS Studio 33+ supports for third-party
; plugins (https://obsproject.com/kb/legacy-plugin-developer-info):
;   C:\ProgramData\obs-studio\plugins\obs-vila-glt\obs-vila-glt.dll
;   C:\ProgramData\obs-studio\plugins\obs-vila-glt\data\*
; and removes copies from the legacy locations (Program Files, bin\64bit),
; which OBS 34 will no longer load. Requires OBS Studio 33 or newer.

#ifndef MyAppVersion
  #define MyAppVersion "0.2.1"
#endif
#define MyAppName "Vila GLT — Live Translate"
#define MyAppPublisher "nklisic"
#define MyAppURL "https://github.com/nklisic/obs-vila-glt"
#define MinObsMajor 33

[Setup]
; Keep AppId stable across versions so upgrades/uninstall work.
AppId={{D4B92E7A-83C1-4F6B-9E2A-7C5D8F1B3E40}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}/issues
DefaultDirName={commonappdata}\obs-studio\plugins\obs-vila-glt
DisableDirPage=yes
DisableProgramGroupPage=yes
PrivilegesRequired=admin
; x64os, not x64compatible: an ARM64 build of OBS cannot load this x64 DLL.
ArchitecturesAllowed=x64os
ArchitecturesInstallIn64BitMode=x64os
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
SetupIconFile=..\media\icon.ico
UninstallDisplayName={#MyAppName}
; Icon shown in Windows "Apps"; installed with the plugin's data files below.
UninstallDisplayIcon={app}\data\icon.ico
OutputBaseFilename=obs-vila-glt-{#MyAppVersion}-windows-x64-installer
; Use Restart Manager to detect/close OBS if it has the DLL open.
CloseApplications=yes
RestartApplications=no

[InstallDelete]
; Earlier versions installed into OBS's own folder (legacy location).
Type: files; Name: "{code:GetOBSDir}\obs-plugins\64bit\obs-vila-glt.dll"
Type: files; Name: "{code:GetOBSDir}\obs-plugins\64bit\obs-vila-glt.pdb"
Type: filesandordirs; Name: "{code:GetOBSDir}\data\obs-plugins\obs-vila-glt"
; Legacy ProgramData layout (DLL under bin\64bit) and stale data files.
Type: filesandordirs; Name: "{app}\bin"
Type: filesandordirs; Name: "{app}\data"

[Files]
Source: "..\build_x64\RelWithDebInfo\obs-vila-glt.dll"; DestDir: "{app}"; \
  Flags: ignoreversion
Source: "..\data\*"; DestDir: "{app}\data"; Flags: recursesubdirs ignoreversion
Source: "..\media\icon.ico"; DestDir: "{app}\data"; Flags: ignoreversion

[UninstallDelete]
Type: dirifempty; Name: "{app}"

[Code]
{ The OBS Studio install directory, from the registry written by OBS's own
  installer; empty if OBS is not installed that way. }
function FindOBSDir(): String;
var
  Path: String;
begin
  Result := '';
  if RegQueryStringValue(HKLM, 'SOFTWARE\OBS Studio', '', Path) and (Path <> '') then
    Result := Path
  else if FileExists(ExpandConstant('{commonpf64}\obs-studio\bin\64bit\obs64.exe')) then
    Result := ExpandConstant('{commonpf64}\obs-studio');
end;

function GetOBSDir(Param: String): String;
begin
  Result := FindOBSDir();
  if Result = '' then
    Result := ExpandConstant('{commonpf64}\obs-studio');
end;

{ OBS Studio 33 introduced the plugin location used here; older versions do
  not look there, so refuse to install for them. }
function InitializeSetup(): Boolean;
var
  ObsDir, ObsExe: String;
  VersionMS, VersionLS: Cardinal;
  Major: Integer;
begin
  Result := True;
  ObsDir := FindOBSDir();
  ObsExe := ObsDir + '\bin\64bit\obs64.exe';
  if (ObsDir = '') or not FileExists(ObsExe) then
  begin
    Result := MsgBox('OBS Studio was not found on this computer.' + #13#10 +
      'This plugin needs OBS Studio {#MinObsMajor} or newer.' + #13#10#13#10 +
      'Install anyway?', mbConfirmation, MB_YESNO) = IDYES;
    exit;
  end;
  if GetVersionNumbers(ObsExe, VersionMS, VersionLS) then
  begin
    Major := VersionMS shr 16;
    if Major < {#MinObsMajor} then
    begin
      MsgBox('This plugin needs OBS Studio {#MinObsMajor} or newer.' + #13#10 +
        'Found OBS Studio ' + IntToStr(Major) + '.x. Please update OBS first.',
        mbError, MB_OK);
      Result := False;
    end;
  end;
end;
