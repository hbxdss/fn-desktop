import hashlib
from PySide6.QtWebEngineCore import QWebEnginePage
from PySide6.QtNetwork import QSslCertificate
from PySide6.QtWidgets import QDialog
from PySide6.QtCore import Signal

from trust_store import TrustStore
from cert_dialog import CertTrustDialog


class CustomPage(QWebEnginePage):
    focus_ready = Signal()

    def __init__(self, trust_store, parent=None):
        super().__init__(parent)
        self.trust_store = trust_store

    def javaScriptConsoleMessage(self, level, message, lineNumber, sourceID):
        if "__FNOS_FOCUS_READY__" in (message or ""):
            self.focus_ready.emit()

    def createWindow(self, window_type):
        return self

    def certificateError(self, error):
        try:
            url = error.url()
            host = url.host()
            hk = f"{host}:{url.port()}" if (url.port() and url.port() != 443) else host

            if not error.isOverridable():
                error.rejectCertificate()
                return False

            cert = error.certificate()
            der = bytes(cert.toDer().data())
            fp = hashlib.sha256(der).hexdigest().upper()
            fp_c = ":".join(fp[i:i+2] for i in range(0, len(fp), 2))

            status = self.trust_store.is_trusted(hk, fp_c)
            if status is True:
                error.acceptCertificate()
                return True

            parent = self.view().window() if self.view() else None
            dlg = CertTrustDialog(hk, error, parent=parent, mismatch=(status is False))
            if dlg.exec() == QDialog.Accepted:
                cn = cert.subjectInfo(QSslCertificate.SubjectInfo.CommonName) or ""
                self.trust_store.trust(hk, fp_c, cn)
                error.acceptCertificate()
                return True
            error.rejectCertificate()
            return False
        except Exception:
            try:
                error.rejectCertificate()
            except Exception:
                pass
            return False
