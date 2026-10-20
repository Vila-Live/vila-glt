; Inno Setup script for Vila GLT — Live Translate (obs-vila-glt).
; Compiled in CI with:  ISCC /DMyAppVersion=<version> installer\obs-vila-glt.iss
; Installs the plugin DLL and data files into the detected OBS Studio install directory.

#ifndef MyAppVersion
  #define MyAppVersion "0.2.0"
#endif
#define MyAppName "Vila GLT — Live Translate"
#define MyAppPublisher "nklisic"
#define MyAppURL "https://github.com/nklisic/obs-vila-glt"

[Setup]
; Keep AppId stable across versions so upgrades/uninstall work.
AppId={{D4B92E7A-83C1-4F6B-9E2A-7C5D8F1B3E40}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}/issues
DefaultDirName={code:GetOBSDir}
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
UninstallDisplayIcon={app}\data\obs-plugins\obs-vila-glt\icon.ico
OutputBaseFilename=obs-vila-glt-{#MyAppVersion}-windows-x64-installer
; Use Restart Manager to detect/close OBS if it has the DLL open.
CloseApplications=yes
RestartApplications=no

[Files]
Source: "..\build_x64\RelWithDebInfo\obs-vila-glt.dll"; \
  DestDir: "{app}\obs-plugins\64bit"; Flags: ignoreversion
Source: "..\data\*"; DestDir: "{app}\data\obs-plugins\obs-vila-glt"; \
  Flags: recursesubdirs ignoreversion
Source: "..\media\icon.ico"; DestDir: "{app}\data\obs-plugins\obs-vila-glt"; \
  Flags: ignoreversion

[Code]
{ Resolve the OBS Studio install directory from the registry written by OBS's
  own installer; fall back to the default Program Files location. }
function GetOBSDir(Param: String): String;
var
  Path: String;
begin
  if RegQueryStringValue(HKLM, 'SOFTWARE\OBS Studio', '', Path) and (Path <> '') then
    Result := Path
  else
    Result := ExpandConstant('{commonpf64}\obs-studio');
end;
