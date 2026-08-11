; MySnipaste 安装程序脚本（Inno Setup 6）
; 打包源：build/Release（完整运行环境：exe + Qt 运行库 + 插件 + tesseract OCR 引擎）
; 编译：ISCC.exe packaging\MySnipaste.iss

#define MyAppName "MySnipaste"
#define MyAppVersion "0.1.0"
#define MyAppPublisher "MySnipaste"
#define MyAppExeName "MySnipaste.exe"
#define MyAppAssocName "MySnipaste 截图"
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
OutputBaseFilename=MySnipaste-Setup-{#MyAppVersion}
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
SetupIconFile=MySnipaste.ico
UninstallDisplayIcon={app}\MySnipaste.exe
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
UninstallDisplayName={#MyAppName}

[Languages]
Name: "chinesesimplified"; MessagesFile: "compiler:Languages\ChineseSimplified.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

; build/Release 即完整运行目录（含 tesseract 引擎与 Qt 插件），整体递归复制
[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "MySnipaste.ico"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; IconFilename: "{app}\MySnipaste.ico"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; IconFilename: "{app}\MySnipaste.ico"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent

; OCR 运行时临时目录 {app}\ocr_tmp（OcrService 生成），卸载时一并清理
[UninstallDelete]
Type: filesandordirs; Name: "{app}\ocr_tmp"
