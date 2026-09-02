; PopTools 安装程序脚本（Inno Setup 6）
; 打包源：build/Release（完整运行环境：exe + Qt 运行库 + 插件 + PaddleOCR 引擎）
; 编译：ISCC.exe packaging\PopTools.iss

#define MyAppName "PopTools"
#define MyAppVersion "0.2.0"
#define MyAppPublisher "PopTools"
#define MyAppExeName "PopTools.exe"
#define MyAppAssocName "PopTools 截图"
#define SourceDir "..\build\Release"

[Setup]
AppId={{01FA8E05-78BD-4319-9057-8EFB7C998BD7}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
OutputDir=..\dist
OutputBaseFilename=PopTools-Setup-{#MyAppVersion}
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
SetupIconFile=PopTools.ico
UninstallDisplayIcon={app}\PopTools.exe
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
Source: "PopTools.ico"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; IconFilename: "{app}\PopTools.ico"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; IconFilename: "{app}\PopTools.ico"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent
