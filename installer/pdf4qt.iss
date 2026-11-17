; MIT License
;
; Copyright (c) 2018-2026 Jakub Melka and Contributors
;
; Permission is hereby granted, free of charge, to any person obtaining a copy
; of this software and associated documentation files (the "Software"), to deal
; in the Software without restriction, including without limitation the rights
; to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
; copies of the Software, and to permit persons to whom the Software is
; furnished to do so, subject to the following conditions:
;
; The above copyright notice and this permission notice shall be included in all
; copies or substantial portions of the Software.
;
; THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
; IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
; FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
; AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
; LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
; OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
; SOFTWARE.

; Per-user Windows installer (Inno Setup 6) of the fork. It installs the files of the portable build
; (build/install/usr/bin) into %LOCALAPPDATA%\Programs\<AppName> without administrator rights, creates the
; Start menu shortcuts, registers the editor as a PDF application (Open with, Default apps) and removes all
; of it on uninstall. The default PDF application is never changed by the installer itself: since Windows 8
; only the user can choose it, so the optional task opens the Default apps page of the Windows Settings.
;
; Build:
;   ISCC.exe /DAppVersion=1.7.0.0 /DSourceDir=<path to build\install\usr\bin> [/DAppName="..."] installer\pdf4qt.iss
; Silent install (used by the CI smoke test):
;   PDF4QT-Setup-<version>.exe /VERYSILENT /SUPPRESSMSGBOXES /NORESTART [/TASKS="desktopicon"]

#ifndef AppVersion
  #error AppVersion must be defined, pass /DAppVersion=<version> to ISCC
#endif
#ifndef SourceDir
  #error SourceDir must be defined, pass /DSourceDir=<path to build\install\usr\bin> to ISCC
#endif

; The name shown in the Start menu, in Apps & features and in the Default apps page of the Windows Settings
#ifndef AppName
  #define AppName "PDF4QT Editor"
#endif
#define AppPublisher "PDF4QT"
#define AppExeName "Pdf4QtEditor.exe"
#define AppProgId "PDF4QT.Editor.pdf"
#define AppCapabilitiesKey "Software\PDF4QT\Editor\Capabilities"
#ifndef OutputBaseName
  #define OutputBaseName "PDF4QT-Setup-" + AppVersion
#endif

[Setup]
; The identifier must never change, it connects an update with the installed version
AppId={{6E0F9D3C-2B7A-4C59-9E3B-8F51A4D2C7E1}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL=https://github.com/Al-Zap/PDF4QT
AppSupportURL=https://github.com/Al-Zap/PDF4QT/issues
VersionInfoVersion={#AppVersion}
VersionInfoProductName={#AppName}
; Per-user installation: no administrator rights, {autopf} is %LOCALAPPDATA%\Programs and registry entries
; of HKA go to HKEY_CURRENT_USER
PrivilegesRequired=lowest
DefaultDirName={autopf}\{#AppName}
DisableProgramGroupPage=yes
DisableDirPage=auto
DisableReadyPage=no
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
LicenseFile=..\LICENSE
SetupIconFile=..\Pdf4QtEditor\app-icon.ico
UninstallDisplayIcon={app}\{#AppExeName}
UninstallDisplayName={#AppName}
WizardStyle=modern
Compression=lzma2/max
SolidCompression=yes
OutputBaseFilename={#OutputBaseName}
; Running copies of the applications are closed before the files are replaced
CloseApplications=yes
RestartApplications=no
; Explorer is notified about the registered PDF association
ChangesAssociations=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[CustomMessages]
english::DefaultAppTask=Make {#AppName} my default PDF app (opens Windows Settings to confirm)
english::ToolsGroup={#AppName} tools
english::RemoveUserData=Also remove your settings and downloaded OCR language models of {#AppName}?%n%nChoose No to keep them for a later installation.

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "defaultapp"; Description: "{cm:DefaultAppTask}"; GroupDescription: "PDF files:"; Flags: unchecked

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\{#AppExeName}"; WorkingDir: "{app}"; Comment: "Open, mark up and measure PDF documents"
Name: "{autoprograms}\{cm:ToolsGroup}\PDF4QT Viewer"; Filename: "{app}\Pdf4QtViewer.exe"; WorkingDir: "{app}"
Name: "{autoprograms}\{cm:ToolsGroup}\PDF4QT PageMaster"; Filename: "{app}\Pdf4QtPageMaster.exe"; WorkingDir: "{app}"
Name: "{autoprograms}\{cm:ToolsGroup}\PDF4QT Diff"; Filename: "{app}\Pdf4QtDiff.exe"; WorkingDir: "{app}"
Name: "{autoprograms}\{cm:ToolsGroup}\Uninstall {#AppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExeName}"; WorkingDir: "{app}"; Tasks: desktopicon

[Registry]
; Program identifier of PDF documents opened by the editor
Root: HKA; Subkey: "Software\Classes\{#AppProgId}"; ValueType: string; ValueName: ""; ValueData: "PDF Document"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\{#AppProgId}"; ValueType: string; ValueName: "FriendlyTypeName"; ValueData: "PDF Document"
Root: HKA; Subkey: "Software\Classes\{#AppProgId}\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: """{app}\{#AppExeName}"",0"
Root: HKA; Subkey: "Software\Classes\{#AppProgId}\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#AppExeName}"" ""%1"""
; Offered in "Open with" of .pdf files. The default value of .pdf (the current default application) is never
; touched.
Root: HKA; Subkey: "Software\Classes\.pdf\OpenWithProgids"; ValueType: string; ValueName: "{#AppProgId}"; ValueData: ""; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Classes\Applications\{#AppExeName}"; ValueType: string; ValueName: "FriendlyAppName"; ValueData: "{#AppName}"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\Applications\{#AppExeName}\SupportedTypes"; ValueType: string; ValueName: ".pdf"; ValueData: ""
Root: HKA; Subkey: "Software\Classes\Applications\{#AppExeName}\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#AppExeName}"" ""%1"""
; Registered application, listed in the Default apps page of the Windows Settings
Root: HKA; Subkey: "Software\PDF4QT"; Flags: uninsdeletekeyifempty
Root: HKA; Subkey: "Software\PDF4QT\Editor"; Flags: uninsdeletekey
Root: HKA; Subkey: "{#AppCapabilitiesKey}"; ValueType: string; ValueName: "ApplicationName"; ValueData: "{#AppName}"
Root: HKA; Subkey: "{#AppCapabilitiesKey}"; ValueType: string; ValueName: "ApplicationDescription"; ValueData: "Open, mark up and measure PDF documents"
Root: HKA; Subkey: "{#AppCapabilitiesKey}"; ValueType: string; ValueName: "ApplicationIcon"; ValueData: """{app}\{#AppExeName}"",0"
Root: HKA; Subkey: "{#AppCapabilitiesKey}\FileAssociations"; ValueType: string; ValueName: ".pdf"; ValueData: "{#AppProgId}"
Root: HKA; Subkey: "Software\RegisteredApplications"; ValueType: string; ValueName: "{#AppName}"; ValueData: "{#AppCapabilitiesKey}"; Flags: uninsdeletevalue

[Run]
Filename: "{app}\{#AppExeName}"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
Type: dirifempty; Name: "{app}"

[Code]
const
  SHCNE_ASSOCCHANGED = $08000000;
  SHCNF_IDLIST = $0000;

procedure SHChangeNotify(wEventId: Integer; uFlags: Cardinal; dwItem1, dwItem2: Integer);
  external 'SHChangeNotify@shell32.dll stdcall';

// Opens the Default apps page of the Windows Settings. Windows 11 opens the page of the registered application
// directly, Windows 10 ignores the parameter and shows the list of the default applications.
procedure OpenDefaultAppsSettings();
var
  Uri: String;
  ErrorCode: Integer;
begin
  Uri := '{#AppName}';
  StringChangeEx(Uri, ' ', '%20', True);
  Uri := 'ms-settings:defaultapps?registeredAppUser=' + Uri;
  if not ShellExec('open', Uri, '', '', SW_SHOWNORMAL, ewNoWait, ErrorCode) then
    Log('Windows Settings could not be opened: ' + SysErrorMessage(ErrorCode));
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then
  begin
    if WizardIsTaskSelected('defaultapp') and not WizardSilent() then
      OpenDefaultAppsSettings();
  end;
end;

// Settings of the applications (QSettings of organization MelkaJ) and their data directories, which contain
// the downloaded OCR models. They are removed only when the user confirms it.
procedure RemoveUserData();
var
  Names: TArrayOfString;
  I: Integer;
begin
  SetArrayLength(Names, 5);
  Names[0] := 'PDF4QT Editor';
  Names[1] := 'PDF4QT Viewer';
  Names[2] := 'PDF4QT PageMaster';
  Names[3] := 'PDF4QT Diff';
  Names[4] := 'PDF4QT LaunchPad';
  for I := 0 to GetArrayLength(Names) - 1 do
  begin
    RegDeleteKeyIncludingSubkeys(HKEY_CURRENT_USER, 'Software\MelkaJ\' + Names[I]);
    DelTree(ExpandConstant('{userappdata}\MelkaJ\') + Names[I], True, True, True);
    DelTree(ExpandConstant('{localappdata}\MelkaJ\') + Names[I], True, True, True);
  end;
  RegDeleteKeyIfEmpty(HKEY_CURRENT_USER, 'Software\MelkaJ');
  RemoveDir(ExpandConstant('{userappdata}\MelkaJ'));
  RemoveDir(ExpandConstant('{localappdata}\MelkaJ'));
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usPostUninstall then
  begin
    if SuppressibleMsgBox(CustomMessage('RemoveUserData'), mbConfirmation, MB_YESNO or MB_DEFBUTTON2, IDNO) = IDYES then
      RemoveUserData();
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, 0, 0);
  end;
end;
