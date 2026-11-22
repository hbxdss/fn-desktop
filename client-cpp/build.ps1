# Build script for FnDesktopPlus (Qt 6 + MSVC 2022 BuildTools + Ninja)
# Usage:  powershell -ExecutionPolicy Bypass -File build.ps1 [-Clean] [-Deploy]

param(
    [switch]$Clean,
    [switch]$Deploy
)

$ErrorActionPreference = 'Stop'

$Root      = Split-Path -Parent $MyInvocation.MyCommand.Definition
$BuildDir  = Join-Path $Root 'build'
$BinDir    = Join-Path $BuildDir 'bin'
$QtDir     = 'C:\Qt\6.7.2\msvc2019_64'
$VcVars    = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
$CMake     = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$Ninja     = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'

if (-not (Test-Path $CMake)) { $CMake = 'cmake.exe' }
if (-not (Test-Path $Ninja)) { $Ninja = 'ninja.exe' }

if (-not (Test-Path $VcVars)) {
    throw "vcvars64.bat not found: $VcVars"
}

if ($Clean -and (Test-Path $BuildDir)) {
    Write-Host '[*] Removing build directory...'
    Remove-Item -Recurse -Force $BuildDir
}

if (-not (Test-Path $BuildDir)) {
    New-Item -ItemType Directory -Path $BuildDir | Out-Null
}

$configure = @(
    "`"$CMake`"",
    '-S', "`"$Root`"",
    '-B', "`"$BuildDir`"",
    '-G', 'Ninja',
    '-DCMAKE_BUILD_TYPE=Release',
    "-DCMAKE_PREFIX_PATH=`"$QtDir`"",
    "-DCMAKE_MAKE_PROGRAM=`"$Ninja`""
) -join ' '

$build = "`"$CMake`" --build `"$BuildDir`" --parallel"

$script = "call `"$VcVars`" >nul && $configure && $build"
Write-Host '[*] Configuring / building...'
cmd.exe /c $script
if ($LASTEXITCODE -ne 0) {
    throw "Build failed with exit code $LASTEXITCODE"
}

# --- runtime dependencies -------------------------------------------------
$sslSrc = 'C:\Program Files\Git\mingw64\bin'
foreach ($dll in @('libssl-3-x64.dll', 'libcrypto-3-x64.dll')) {
    $src = Join-Path $sslSrc $dll
    if (Test-Path $src) {
        Copy-Item $src (Join-Path $BinDir $dll) -Force
    }
}

if ($Deploy) {
    $windeployqt = Join-Path $QtDir 'bin\windeployqt.exe'
    if (Test-Path $windeployqt) {
        Write-Host '[*] Deploying Qt runtime...'
        & $windeployqt --release --no-translations --no-opengl-sw --webenginewidgets (Join-Path $BinDir 'FnDesktopPlus.exe')
    }
}

# Qt 控件中文翻译（对话框按钮等），程序启动时从 translations 目录加载
$qmSrc = Join-Path $QtDir 'translations\qtbase_zh_CN.qm'
if (Test-Path $qmSrc) {
    $qmDst = Join-Path $BinDir 'translations'
    New-Item -ItemType Directory -Path $qmDst -Force | Out-Null
    Copy-Item $qmSrc (Join-Path $qmDst 'qtbase_zh_CN.qm') -Force
}

Write-Host ''
Write-Host "[OK] Executable: $(Join-Path $BinDir 'FnDesktopPlus.exe')"
