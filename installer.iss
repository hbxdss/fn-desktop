[Setup]
AppName=飞牛终端
AppVersion=1.0.0
AppPublisher=fnOS Desktop
DefaultDirName={autopf}\飞牛终端
DefaultGroupName=飞牛终端
OutputDir=dist
OutputBaseFilename=飞牛终端-Setup-1.0.0
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
ArchitecturesInstallIn64BitMode=x64
DisableProgramGroupPage=yes

[Tasks]
Name: "desktopicon"; Description: "创建桌面快捷方式"; GroupDescription: "附加快捷方式:"

[Files]
Source: "dist\飞牛终端\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\飞牛终端"; Filename: "{app}\飞牛终端.exe"
Name: "{group}\卸载飞牛终端"; Filename: "{uninstallexe}"
Name: "{autodesktop}\飞牛终端"; Filename: "{app}\飞牛终端.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\飞牛终端.exe"; Description: "立即启动飞牛终端"; Flags: nowait postinstall skipifsilent
