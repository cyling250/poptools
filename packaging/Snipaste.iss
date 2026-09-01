; Snipaste 安装程序脚本（Inno Setup 6）
; 打包源：build/Release（完整运行环境：exe + Qt 运行库 + 插件 + PaddleOCR 引擎）
; 编译：ISCC.exe packaging\Snipaste.iss

#define MyAppName "Snipaste"
#define MyAppVersion "0.1.3"
#define MyAppPublisher "Snipaste"
#define MyAppExeName "Snipaste.exe"
#define MyAppAssocName "Snipaste 截图"
#define SourceDir "..\build\Release"

[Setup]
AppId={{B4389B1C-0F4A-4F8B-8554-EB8229E27039}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
OutputDir=..\dist
OutputBaseFilename=Snipaste-Setup-{#MyAppVersion}
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
SetupIconFile=Snipaste.ico
UninstallDisplayIcon={app}\Snipaste.exe
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
UninstallDisplayName={#MyAppName}

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

; build/Release 即完整运行目录（含 PaddleOCR 引擎与 Qt 插件），整体递归复制
[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "Snipaste.ico"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; IconFilename: "{app}\Snipaste.ico"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; IconFilename: "{app}\Snipaste.ico"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent
