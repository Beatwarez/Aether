; Inno Setup script for compiling the Aether VST3 installer on Windows

[Setup]
AppName=Aether
AppVersion=1.0.0
AppPublisher=Algebra Within
DefaultDirName={commoncf}\VST3\Algebra Within\Aether.vst3
DisableDirPage=yes
UsePreviousAppDir=no
OutputBaseFilename=Aether_Windows_Installer
Compression=lzma
SolidCompression=yes
ArchitecturesInstallIn64BitMode=x64
DisableProgramGroupPage=yes
DisableWelcomePage=no

[Files]
Source: "Builds\VisualStudio2022\x64\Release\VST3\Aether.vst3\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion
