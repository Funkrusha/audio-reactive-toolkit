#ifndef ProductVersion
  #error ProductVersion must be provided with /DProductVersion=...
#endif
#ifndef SourceRoot
  #error SourceRoot must be provided with /DSourceRoot=...
#endif
#ifndef OutputDirectory
  #error OutputDirectory must be provided with /DOutputDirectory=...
#endif

#define ProductName "Audio Reactive Toolkit"
#define ProductId "audio-reactive-toolkit"
#define ProductUrl "https://github.com/Funkrusha/audio-reactive-toolkit"

[Setup]
AppId={{1AE32369-2EF7-45EF-8C22-72B1254B9635}
AppName={#ProductName}
AppVersion={#ProductVersion}
AppPublisher=Funkrusha
AppPublisherURL={#ProductUrl}
AppSupportURL={#ProductUrl}/issues
AppUpdatesURL={#ProductUrl}/releases
LicenseFile={#SourceRoot}\{#ProductId}\data\LICENSE
DefaultDirName={reg:HKLM\Software\OBS Studio,|{commonpf64}\obs-studio}
AppendDefaultDirName=no
DirExistsWarning=no
DisableProgramGroupPage=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
OutputDir={#OutputDirectory}
OutputBaseFilename={#ProductId}-{#ProductVersion}-windows-x64-setup
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes
UninstallDisplayIcon={app}\obs-plugins\64bit\audio-reactive-toolkit.dll

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "german"; MessagesFile: "compiler:Languages\German.isl"

[Files]
Source: "{#SourceRoot}\{#ProductId}\bin\64bit\{#ProductId}.dll"; DestDir: "{app}\obs-plugins\64bit"; Flags: ignoreversion
Source: "{#SourceRoot}\{#ProductId}\data\*"; DestDir: "{app}\data\obs-plugins\{#ProductId}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Code]
function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;
  if CurPageID = wpSelectDir then
  begin
    Result := DirExists(ExpandConstant('{app}\obs-plugins\64bit')) and
      DirExists(ExpandConstant('{app}\data\obs-plugins'));
    if not Result then
      MsgBox('No valid OBS Studio installation was found in this directory.', mbError, MB_OK);
  end;
end;
