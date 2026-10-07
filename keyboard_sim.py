from PySide6.QtGui import QKeyEvent
from PySide6.QtCore import Qt, QCoreApplication
import time


def _target(widget):
    fp = widget.focusProxy()
    return fp if fp else widget


def send_text(widget, text, delay_ms=35):
    target = _target(widget)
    for ch in text:
        p = QKeyEvent(QKeyEvent.KeyPress, 0, Qt.NoModifier, ch)
        QCoreApplication.sendEvent(target, p)
        r = QKeyEvent(QKeyEvent.KeyRelease, 0, Qt.NoModifier, ch)
        QCoreApplication.sendEvent(target, r)
        QCoreApplication.processEvents()
        time.sleep(delay_ms / 1000.0)


def send_tab(widget):
    target = _target(widget)
    p = QKeyEvent(QKeyEvent.KeyPress, Qt.Key_Tab, Qt.NoModifier)
    QCoreApplication.sendEvent(target, p)
    r = QKeyEvent(QKeyEvent.KeyRelease, Qt.Key_Tab, Qt.NoModifier)
    QCoreApplication.sendEvent(target, r)
    QCoreApplication.processEvents()
