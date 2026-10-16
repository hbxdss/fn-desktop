import os
import sys
import winreg
from pathlib import Path

from PySide6.QtCore import Qt
from PySide6.QtWidgets import QApplication
from PySide6.QtGui import QIcon

from data_store import get_store
from config import apply_theme, effective_theme, normalize_theme_mode
from browser import Browser


def is_vm():
    """检测是否运行在虚拟机中（VMware/VirtualBox/QEMU/KVM 等）"""
    try:
        k = winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, r"HARDWARE\DESCRIPTION\System\BIOS")
        m, _ = winreg.QueryValueEx(k, "SystemManufacturer")
        p, _ = winreg.QueryValueEx(k, "SystemProductName")
        winreg.CloseKey(k)
        s = (str(m) + " " + str(p)).lower()
        return any(x in s for x in ["vmware", "virtualbox", "qemu", "kvm", "xen", "parallels", "innotek", "microsoft corporation"])
    except Exception:
        return False


def app_icon_path():
    """应用图标路径：打包后取 exe 旁 assets/，源码运行取项目 assets/"""
    if getattr(sys, "frozen", False):
        base = Path(sys.executable).resolve().parent
    else:
        base = Path(__file__).resolve().parent
    for name in ("app.ico", "app.png"):
        p = base / "assets" / name
        if p.exists():
            return str(p)
    return ""


mode = get_store().get("render_mode", "auto")
if mode == "auto":
    final = "software" if is_vm() else "hardware"
else:
    final = mode

# 证书策略说明（实测依据）：
# PySide6 6.12 + Qt WebEngine 下，QWebEnginePage.certificateError 重写不会被
# Chromium 回调（本地自签名 HTTPS 实测 cert_error_called=0，SSL 直接失败
# net_error -202），导致“指纹信任弹窗”方案在真实连接时永不触发。
# 因此保留全局 --ignore-certificate-errors 兜底，保证用户可连接使用自签名
# 证书的飞牛 NAS；CustomPage.certificateError + TrustStore 指纹逻辑仍保留，
# 在证书回调可用的环境下继续提供首次信任与指纹变更提示。
if final == "software":
    os.environ["QTWEBENGINE_CHROMIUM_FLAGS"] = "--disable-gpu --disable-gpu-compositing --disable-software-rasterizer --allow-running-insecure-content --ignore-certificate-errors"
    os.environ["QT_OPENGL"] = "software"
else:
    os.environ["QTWEBENGINE_CHROMIUM_FLAGS"] = "--allow-running-insecure-content --ignore-certificate-errors"

os.environ["QT_ENABLE_HIGHDPI_SCALING"] = "1"


def main():
    if final == "software":
        QApplication.setAttribute(Qt.AA_UseSoftwareOpenGL)
    QApplication.setAttribute(Qt.AA_ShareOpenGLContexts)
    app = QApplication(sys.argv)
    app.setApplicationName("飞牛桌面版")
    icon = app_icon_path()
    if icon:
        app.setWindowIcon(QIcon(icon))
    store = get_store()
    theme_key = normalize_theme_mode(store.get("theme"), store.get("auto_theme", False))
    apply_theme(app, effective_theme(app, theme_key),
                store.get("custom_accent"),
                store.get("custom_bg_dark") or store.get("custom_bg"),
                store.get("custom_bg_light"))
    win = Browser()
    win.show()
    sys.exit(app.exec())


if __name__ == "__main__":
    main()
