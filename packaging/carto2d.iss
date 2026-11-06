; carto2D's installer, for Inno Setup 6 (notions §79, step 7e).
;
; It installs the folder made by packaging/package-windows.sh
; (dist\carto2D-<version>-windows-x64\) for the user alone, without
; administrator rights, in %LOCALAPPDATA%\Programs\carto2D: the folder is
; the user's to write in (symbols\user\, render2d notes §76).
;
; Built by package-windows.sh when Inno Setup is installed, or by hand:
;     ISCC /DAppVersion=0.7.1 packaging\carto2d.iss
; The result: dist\carto2D-<version>-setup.exe

#ifndef AppVersion
  #error Give the version: ISCC /DAppVersion=0.7.1 packaging\carto2d.iss (package-windows.sh does it)
#endif
#define SourceDir "..\dist\carto2D-" + AppVersion + "-windows-x64"

[Setup]
; One identity for every version: a new one replaces the old one in place.
; Never change it.
AppId={{98891BE4-C066-4986-B3F9-739CB32D065B}
AppName=carto2D
AppVersion={#AppVersion}
AppVerName=carto2D {#AppVersion}
AppPublisher=Noel Frankinet
AppPublisherURL=https://github.com/nfr25/carto2d
AppSupportURL=https://github.com/nfr25/carto2d/issues
AppUpdatesURL=https://github.com/nfr25/carto2d/releases
; For the user alone: no administrator, %LOCALAPPDATA%\Programs\carto2D.
PrivilegesRequired=lowest
DefaultDirName={autopf}\carto2D
DisableProgramGroupPage=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=..\dist
OutputBaseFilename=carto2D-{#AppVersion}-setup
SetupIconFile=..\src\window\editor2d.ico
UninstallDisplayIcon={app}\carto2d.exe
UninstallDisplayName=carto2D {#AppVersion}
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern

[Languages]
Name: "en"; MessagesFile: "compiler:Default.isl"
Name: "fr"; MessagesFile: "compiler:Languages\French.isl"
Name: "nl"; MessagesFile: "compiler:Languages\Dutch.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[InstallDelete]
; An update first removes the libraries and reference files of the version
; before: GDAL's DLLs change names from one version to the next, and an old
; one left beside a new one is trouble. The user's files are not touched.
Type: files; Name: "{app}\*.dll"
Type: filesandordirs; Name: "{app}\share"

[Dirs]
; The user's symbols (render2d notes §76): kept when carto2D is removed.
Name: "{app}\symbols\user"; Flags: uninsneveruninstall

[Files]
; Everything the package holds, but the two things that are the user's.
Source: "{#SourceDir}\*"; DestDir: "{app}"; Excludes: "\carto2d.yaml,\symbols\user\*"; Flags: ignoreversion recursesubdirs createallsubdirs
; The demonstration project: installed once, never replaced (the user may
; have changed it and saved), never removed.
Source: "{#SourceDir}\carto2d.yaml"; DestDir: "{app}"; Flags: onlyifdoesntexist uninsneveruninstall
Source: "{#SourceDir}\symbols\user\README.txt"; DestDir: "{app}\symbols\user"; Flags: ignoreversion

[Icons]
; Started in its folder: imgui.ini (the panels' layout) and the recent
; connections are written there.
Name: "{autoprograms}\carto2D"; Filename: "{app}\carto2d.exe"; WorkingDir: "{app}"
Name: "{autodesktop}\carto2D"; Filename: "{app}\carto2d.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\carto2d.exe"; WorkingDir: "{app}"; Description: "{cm:LaunchProgram,carto2D}"; Flags: nowait postinstall skipifsilent
