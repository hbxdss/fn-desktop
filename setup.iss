; 飞牛桌面版 (FN Desktop) 安装脚本 — Inno Setup 6
#define MyAppName "飞牛桌面版"
#define MyAppVersion "1.0.0"
#define MyAppPublisher "hbxdss"
#define MyAppExeName "FNDesktop.exe"

[Setup]
; 唯一应用 ID（固定，用于卸载/升级识别）
AppId={{addab8fe-bc3d-4362-b2a1-0d2726de4106}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
; 安装到当前用户目录（无需管理员权限；data 配置目录可写）
DefaultDirName={userpf}\FNDesktop
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
OutputDir=C:\Users\hbxdss\Desktop\FN-Desktop-发布\安装版
OutputBaseFilename=FNDesktop-安装版-v1.0.0
SetupIconFile=C:\Users\hbxdss\Desktop\fn-desktop\assets\app.ico
UninstallDisplayIcon={app}\{#MyAppExeName}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible

[Languages]
Name: "chinesesimp"; MessagesFile: "C:\Users\hbxdss\Desktop\fn-desktop\ChineseSimplified.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "创建桌面快捷方式"; GroupDescription: "附加任务:"

[Files]
Source: "C:\Users\hbxdss\Desktop\fn-desktop\dist\FNDesktop\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "立即运行 {#MyAppName}"; Flags: nowait postinstall skipifsilent
