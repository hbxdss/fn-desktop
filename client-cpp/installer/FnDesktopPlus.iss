; 飞牛终端 (FnDesktopPlus) Windows 安装包脚本
; 编译： "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" client-cpp\installer\FnDesktopPlus.iss
; 产物： dist\release\FnDesktopPlus-v1.2.0-Setup.exe

#define MyAppName "飞牛终端"
#define MyAppVersion "1.2.0"
#define MyAppPublisher "hbxdss"
#define MyAppExeName "FnDesktopPlus.exe"
#define MyAppUrl "https://github.com/hbxdss/fn-desktop"

[Setup]
AppId={{6F2B8C1A-9D3E-4E7B-A5C2-8F1D0B3E7A64}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppUrl}
AppSupportURL={#MyAppUrl}/issues
AppUpdatesURL={#MyAppUrl}/releases
VersionInfoVersion={#MyAppVersion}.0
VersionInfoDescription={#MyAppName} 安装程序
DefaultDirName={autopf}\FnDesktopPlus
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
OutputDir=..\dist\release
OutputBaseFilename=FnDesktopPlus-v{#MyAppVersion}-Setup
SetupIconFile=resources\app.ico
UninstallDisplayIcon={app}\{#MyAppExeName}
UninstallDisplayName={#MyAppName}
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
SourceDir=..

[Languages]
Name: "chs"; MessagesFile: "installer\ChineseSimplified.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "创建桌面快捷方式"; GroupDescription: "附加任务:"; Flags: unchecked

[Files]
Source: "build\bin\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "启动 {#MyAppName}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
Type: filesandordirs; Name: "{app}"
