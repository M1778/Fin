; Inno Setup script for the finc Windows installer.
; Compiled on the release runner with:
;   iscc /DFinVersion=<semver> /DFinTarget=<triple> /DFinArch=<x64compatible|arm64> /DRepoRoot=<abs repo root> /O<abs out dir> tools/packaging/finc.iss
; so the version is never written down here -- the release tag owns it and the
; portable zip built in the same job owns the bytes. The output directory is
; passed with /O on the command line rather than fixed here, for the same
; reason: where the asset lands is the workflow's decision. There is no custom
; installer code: the installer lays down the same bin/finc.exe + lib/std tree
; the zip carries and optionally adds it to PATH.
#ifndef FinVersion
#define FinVersion "0.0.0"
#endif
#ifndef FinTarget
#define FinTarget "x86_64-pc-windows-msvc"
#endif
#ifndef FinArch
#define FinArch "x64compatible"
#endif
#ifndef RepoRoot
#define RepoRoot "..\.."
#endif
#ifndef StageDir
#define StageDir RepoRoot + "\\stage"
#endif

[Setup]
AppName=Fin Compiler
AppVersion={#FinVersion}
AppVerName=Fin Compiler {#FinVersion} ({#FinTarget})
AppId={{99AAB439-A2DA-4DA0-AC52-89CCF90DB64E}
AppPublisher=M1778/Fin contributors
AppPublisherURL=https://github.com/M1778/Fin
DefaultDirName={autopf}\Fin\{#FinVersion}\{#FinTarget}
DefaultGroupName=Fin Compiler
PrivilegesRequired=lowest
ArchitecturesAllowed={#FinArch}
OutputBaseFilename=finc-{#FinVersion}-{#FinTarget}-setup
LicenseFile={#RepoRoot}\LICENSE
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName=Fin Compiler {#FinVersion} ({#FinTarget})

[Tasks]
Name: envPath; Description: "Add finc to PATH"; GroupDescription: "Additional:"; Flags: checkedonce

[Files]
Source: "{#StageDir}\bin\finc.exe"; DestDir: "{app}\bin"; Flags: ignoreversion
Source: "{#StageDir}\lib\std\*.fin"; DestDir: "{app}\lib\std"; Flags: ignoreversion

[Code]
{ The HKCU PATH entry is edited, never replaced: the old value is kept and the
  install directory appended once. This is the stock Inno pattern, not custom
  logic -- a hand-rolled registry edit is what would need reviewing. }
const EnvironmentKey = 'Environment';

procedure AddPath();
var
  OrigPath: string;
begin
  if not RegQueryStringValue(HKCU, EnvironmentKey, 'Path', OrigPath) then
    OrigPath := '';
  if Pos(';' + ExpandConstant('{app}\bin') + ';', ';' + OrigPath + ';') = 0 then
  begin
    if (OrigPath <> '') and (OrigPath[Length(OrigPath)] <> ';') then
      OrigPath := OrigPath + ';';
    OrigPath := OrigPath + ExpandConstant('{app}\bin') + ';';
    RegWriteExpandszValue(HKCU, EnvironmentKey, 'Path', OrigPath);
  end;
end;

procedure RemovePath();
var
  OrigPath, Removed: string;
  P, Len: integer;
begin
  if not RegQueryStringValue(HKCU, EnvironmentKey, 'Path', OrigPath) then
    exit;
  Removed := ExpandConstant('{app}\bin') + ';';
  P := Pos(Removed, OrigPath);
  while P > 0 do
  begin
    Len := Length(OrigPath);
    Delete(OrigPath, P, Length(Removed));
    if Length(OrigPath) = Len then
      break;
    P := Pos(Removed, OrigPath);
  end;
  RegWriteExpandszValue(HKCU, EnvironmentKey, 'Path', OrigPath);
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if (CurStep = ssPostInstall) and WizardIsTaskSelected('envPath') then
    AddPath();
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usPostUninstall then
    RemovePath();
end;
