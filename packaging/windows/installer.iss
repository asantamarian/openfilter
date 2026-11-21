; Windows installer for OpenFilter EQ (Inno Setup 6). Built by the GitHub workflows:
;   ISCC /DVersion=0.1.2 /DRoot=<repository> /DArtefacts=<build>\OpenFilterEQ_artefacts\Release installer.iss
; Puts the VST3 and CLAP plugins in the folders hosts scan, and the standalone program
; and the manual under Program Files.

#ifndef Version
  #define Version "dev"
#endif

[Setup]
AppId={{C837DEB5-E775-4F65-907C-71991F9585DF}
AppName=OpenFilter EQ
AppVersion={#Version}
AppPublisher=OpenFilter
AppPublisherURL=https://github.com/asantamarian/openfilter
DefaultDirName={autopf}\OpenFilter EQ
DefaultGroupName=OpenFilter EQ
DisableProgramGroupPage=yes
LicenseFile={#Root}\LICENSE
OutputDir={#Root}
OutputBaseFilename=openfilter-eq-{#Version}-windows-x64-setup
Compression=lzma2
SolidCompression=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
WizardStyle=modern

[Types]
Name: "full"; Description: "Everything"
Name: "custom"; Description: "Choose what to install"; Flags: iscustom

[Components]
Name: "vst3"; Description: "VST3 plugin (Common Files\VST3)"; Types: full custom
Name: "clap"; Description: "CLAP plugin (Common Files\CLAP)"; Types: full custom
Name: "standalone"; Description: "Standalone program"; Types: full

[Files]
; The VST3 is a bundle folder; it is copied whole.
Source: "{#Artefacts}\VST3\*"; DestDir: "{commoncf64}\VST3"; Components: vst3; Flags: recursesubdirs ignoreversion
Source: "{#Artefacts}\CLAP\*.clap"; DestDir: "{commoncf64}\CLAP"; Components: clap; Flags: ignoreversion
Source: "{#Artefacts}\Standalone\*.exe"; DestDir: "{app}"; Components: standalone; Flags: ignoreversion
Source: "{#Root}\LICENSE"; DestDir: "{app}"; DestName: "LICENSE.txt"; Flags: ignoreversion
Source: "{#Root}\docs\manual.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Root}\assets\fonts\*-OFL.txt"; DestDir: "{app}\licenses"; Flags: ignoreversion

[Icons]
Name: "{group}\OpenFilter EQ"; Filename: "{app}\OpenFilter EQ.exe"; Components: standalone
Name: "{group}\Manual"; Filename: "{app}\manual.md"
