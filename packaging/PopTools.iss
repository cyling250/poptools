; PopTools 安装程序脚本（Inno Setup 6）
; 打包源：build/Release（完整运行环境：exe + Qt 运行库 + 插件 + RapidOCR 引擎）
; 编译：ISCC.exe packaging\PopTools.iss

#define MyAppName "PopTools"
#define MyAppVersion "0.5.1"
; 构建时间戳进文件名：修复期间版本号未变，同名包易新旧混淆
#define BuildStamp GetDateTimeString("yyyymmdd_hhnn", "", "")
#define MyAppPublisher "PopTools"
#define MyAppExeName "PopTools.exe"
#define MyAppAssocName "PopTools 截图"
#define SourceDir "..\build\Release"
; T-074 M3：dsh 运行时（node + dsh node_modules，约 500MB 压缩包）不进安装包，
; 安装过程联网下载（用户 2026-11-22 拍板）。资产随每次发版上传 GitHub Release
;（scripts/pack_dsh_runtime.py 从本机 tools/ 打包）
#define DshRuntimeUrl "https://github.com/xiaopenggu/PopTools/releases/download/v{#MyAppVersion}/poptools-dsh-runtime.zip"
#define WebView2BootstrapperUrl "https://go.microsoft.com/fwlink/?linkid=2124701"

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
OutputBaseFilename=PopTools-Setup-{#MyAppVersion}-{#BuildStamp}
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

; build/Release 即完整运行目录（含 RapidOCR 引擎与 Qt 插件），整体递归复制；
; 排除误入的单元测试程序（Excludes 模式对递归子目录同样生效）
[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Excludes: "poptools_domain_tests.exe"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "PopTools.ico"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; IconFilename: "{app}\PopTools.ico"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; IconFilename: "{app}\PopTools.ico"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent


; ============================================================
; T-074 M3：安装过程联网装配 dsh 运行时与 WebView2（用户拍板「安装包不放
; dsh，安装过程中联网装好」）。实现：PowerShell 下载 + Expand-Archive（系统
; 自带，不经第三方解压工具）；失败不阻断安装——PopTools 首启的 dsh 状态页
; 会明示缺什么，可经 install_dsh.py 或重跑安装器补齐。
; ============================================================
[Code]

procedure ExecPs(const FileName: String; var ResultCode: Integer);
begin
  Exec('powershell.exe', '-NoProfile -ExecutionPolicy Bypass -File "' +
       ExpandConstant(FileName) + '"', '', SW_SHOW, ewWaitUntilTerminated, ResultCode);
end;

// WebView2 运行时检测（Win11 自带；Win10 早期版本缺失时装 Evergreen 引导器）
procedure EnsureWebView2();
var
  PsFile, Script: String;
  ResultCode: Integer;
begin
  if RegKeyExists(HKLM, 'SOFTWARE\WOW6432Node\Microsoft\EdgeUpdate\Clients\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}') or
     RegKeyExists(HKCU, 'SOFTWARE\Microsoft\EdgeUpdate\Clients\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}') then
    exit;
  WizardForm.StatusLabel.Caption := '正在安装 WebView2 运行时（约 2MB）...';
  WizardForm.ProgressGauge.Style := npbstMarquee;
  try
    PsFile := ExpandConstant('{tmp}\install_webview2.ps1');
    Script :=
      '$ErrorActionPreference = "Stop";' + #13#10 +
      'Invoke-WebRequest -Uri "' + ExpandConstant('{#WebView2BootstrapperUrl}') + '" -OutFile "' +
      ExpandConstant('{tmp}\webview2setup.exe') + '";' + #13#10 +
      'Start-Process -FilePath "' + ExpandConstant('{tmp}\webview2setup.exe') + '" -ArgumentList "/silent /install" -Wait;';
    SaveStringToFile(ExpandConstant(PsFile), Script, False);
    ExecPs(PsFile, ResultCode);
  except
    // WebView2 缺失不阻断（Win11 自带；失败机器首启 dsh 状态页会提示）
  end;
  WizardForm.ProgressGauge.Style := npbstNormal;
end;

// dsh 运行时下载解压（{app}\tools\node\current + {app}\tools\dsh\node_modules）
procedure InstallDshRuntime();
var
  PsFile, Script, ZipPath: String;
  ResultCode: Integer;
begin
  WizardForm.StatusLabel.Caption :=
    '正在下载 dsh 运行时（约 500MB，仅安装时一次）...';
  WizardForm.ProgressGauge.Style := npbstMarquee;
  try
    ZipPath := ExpandConstant('{tmp}\poptools-dsh-runtime.zip');
    PsFile := ExpandConstant('{tmp}\install_dsh_runtime.ps1');
    Script :=
      '$ErrorActionPreference = "Stop";' + #13#10 +
      '$ProgressPreference = "SilentlyContinue";' + #13#10 +
      'Invoke-WebRequest -Uri "' + ExpandConstant('{#DshRuntimeUrl}') + '" -OutFile "' +
      ZipPath + '";' + #13#10 +
      'if ((Get-Item "' + ZipPath + '").Length -lt 100000000) { throw "runtime zip too small" };' + #13#10 +
      'Expand-Archive -Path "' + ZipPath + '" -DestinationPath "' +
      ExpandConstant('{app}\tools') + '" -Force;' + #13#10 +
      'Set-Content -Path "' + ExpandConstant('{app}\tools\node\.installed-version') + '" -Value "runtime";' + #13#10 +
      'Set-Content -Path "' + ExpandConstant('{app}\tools\dsh\.installed-version') + '" -Value "{#MyAppVersion}";';
    SaveStringToFile(ExpandConstant(PsFile), Script, False);
    ExecPs(PsFile, ResultCode);
    if ResultCode <> 0 then
      MsgBox('dsh 运行时下载/解压失败（联网装 dsh 未完成）。' #13#10
             'PopTools 仍可安装使用；管家界面会在补齐运行时后可用。', mbError, MB_OK);
  except
    MsgBox('dsh 运行时下载异常（联网装 dsh 未完成）。' #13#10
           'PopTools 仍可安装使用；管家界面会在补齐运行时后可用。', mbError, MB_OK);
  end;
  WizardForm.ProgressGauge.Style := npbstNormal;
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssInstall then
  begin
    EnsureWebView2();
    InstallDshRuntime();
  end;
end;
