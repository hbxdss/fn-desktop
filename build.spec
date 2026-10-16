# -*- coding: utf-8 -*-
# PyInstaller 打包配置：飞牛桌面版（Windows）
# 用法：python -m PyInstaller build.spec --clean --noconfirm
from PyInstaller.utils.hooks import collect_data_files, collect_submodules
import PySide6

# assets 以完整目录形式收集（PyInstaller 6 onedir 放入 _internal/assets，
# asset_path 已兼容 exe 旁与 _internal 两种布局）
datas = [("assets", "assets")]
hiddenimports = [
    "win32crypt", "wmi", "cryptography", "requests",
    "win32com", "win32con", "win32gui",
]
# PySide6 QtWebEngine 完整收集（Qt 资源/翻译/WebEngineProcess）
for m in ("PySide6.QtWebEngineCore", "PySide6.QtWebEngineWidgets"):
    datas += collect_data_files(m)
    hiddenimports += collect_submodules(m)

a = Analysis(
    ["main.py"],
    pathex=[],
    binaries=[],
    datas=datas,
    hiddenimports=hiddenimports,
    hookspath=[],
    hooksconfig={},
    runtime_hooks=[],
    excludes=[],
    noarchive=False,
    optimize=0,
)
pyz = PYZ(a.pure)

exe = EXE(
    pyz,
    a.scripts,
    [],
    exclude_binaries=True,
    name="FNDesktop",
    debug=False,
    bootloader_ignore_signals=False,
    strip=False,
    upx=False,
    console=False,
    disable_windowed_traceback=False,
    icon="assets/app.ico",
)

coll = COLLECT(
    exe,
    a.binaries,
    a.datas,
    strip=False,
    upx=False,
    upx_exclude=[],
    name="FNDesktop",
)
