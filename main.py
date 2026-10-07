import os
import sys
import winreg


def is_vm():
    try:
        k = winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, r"HARDWARE\DESCRIPTION\System\BIOS")
        m, _ = winreg.QueryValueEx(k, "SystemManufacturer")
        p, _ = winreg.QueryValueEx(k, "SystemProductName")
        winreg.CloseKey(k)
        s = (str(m) + " " + str(p)).lower()
        return any(x in s for x in ["vmware", "virtualbox", "qemu", "kvm", "xen", "parallels", "innotek", "microsoft corporation"])
    except Exception:
        return False


from data_store import get_store
mode = get_store().get("render_mode", "auto")
if mode == "auto":
    final = "software" if is_vm() else "hardware"
else:
    final = mode

if final == "software":
    os.environ["QTWEBENGINE_CHROMIUM_FLAGS"] = "--disable-gpu --disable-gpu-compositing --disable-software-rasterizer --ignore-certificate-errors --allow-running-insecure-content"
    os.environ["QT_OPENGL"] = "software"
else:
    os.environ["QTWEBENGINE_CHROMIUM_FLAGS"] = "--ignore-certificate-errors --allow-running-insecure-content"

os.environ["QT_ENABLE_HIGHDPI_SCALING"] = "1"

from PySide6.QtCore import Qt
from PySide6.QtWidgets import QApplication
from browser import Browser


def main():
    if final == "software":
        QApplication.setAttribute(Qt.AA_UseSoftwareOpenGL)
    QApplication.setAttribute(Qt.AA_ShareOpenGLContexts)
    app = QApplication(sys.argv)
    app.setApplicationName("飞牛终端")
    win = Browser()
    win.show()
    sys.exit(app.exec())


if __name__ == "__main__":
    main()



