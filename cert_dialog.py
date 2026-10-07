import hashlib
from PySide6.QtWidgets import QDialog, QVBoxLayout, QHBoxLayout, QLabel, QPushButton, QWidget
from PySide6.QtCore import Qt
from PySide6.QtNetwork import QSslCertificate


class CertTrustDialog(QDialog):
    def __init__(self, host, error, parent=None, mismatch=False):
        super().__init__(parent)
        self.setObjectName("Dlg")
        self.setWindowTitle("证书安全提示")
        self.setModal(True)
        self.setMinimumWidth(560)

        cert = error.certificate()
        der = bytes(cert.toDer().data())
        fp = hashlib.sha256(der).hexdigest().upper()
        self.fingerprint = ":".join(fp[i:i+2] for i in range(0, len(fp), 2))

        cn = cert.subjectInfo(QSslCertificate.SubjectInfo.CommonName) or "(无)"
        org = cert.subjectInfo(QSslCertificate.SubjectInfo.Organization) or "(无)"
        issuer = cert.issuerInfo(QSslCertificate.SubjectInfo.CommonName) or "(无)"
        nb = cert.effectiveDate().toString("yyyy-MM-dd")
        na = cert.expiryDate().toString("yyyy-MM-dd")

        v = QVBoxLayout(self)
        v.setContentsMargins(26, 24, 26, 20)
        v.setSpacing(12)

        icon = QLabel("⚠️" if mismatch else "🔒")
        icon.setObjectName("DlgIcon")
        icon.setAlignment(Qt.AlignCenter)
        v.addWidget(icon)

        if mismatch:
            tt = "此证书与上次不同"
            dt = f"你之前信任过 {host} 的证书，但现在指纹变了。\n这可能意味着有人正在拦截你的连接。"
        else:
            tt = "此站点使用了自签名证书"
            dt = f"你首次连接 {host}。\n如果确认这是你自己的 NAS，可以信任并继续。"

        t = QLabel(tt); t.setObjectName("DlgTitle"); t.setAlignment(Qt.AlignCenter); t.setWordWrap(True)
        v.addWidget(t)
        d = QLabel(dt); d.setObjectName("DlgDesc"); d.setAlignment(Qt.AlignCenter); d.setWordWrap(True)
        v.addWidget(d)

        v.addSpacing(6)
        s = QLabel("证书信息"); s.setObjectName("CertSection"); v.addWidget(s)

        info = QWidget(); gl = QVBoxLayout(info); gl.setContentsMargins(0,0,0,0); gl.setSpacing(6)
        for k, val in [("主机", host), ("颁发给", cn), ("组织", org), ("颁发者", issuer), ("有效期", f"{nb} 至 {na}")]:
            row = QHBoxLayout(); row.setSpacing(12)
            kl = QLabel(f"{k}："); kl.setObjectName("CertFieldKey"); kl.setFixedWidth(64)
            vl = QLabel(val); vl.setObjectName("CertFieldVal"); vl.setTextInteractionFlags(Qt.TextSelectableByMouse)
            row.addWidget(kl); row.addWidget(vl, 1); gl.addLayout(row)
        v.addWidget(info)

        v.addSpacing(6)
        f = QLabel("SHA-256 指纹"); f.setObjectName("CertSection"); v.addWidget(f)
        fl = QLabel(self.fingerprint); fl.setObjectName("CertFp"); fl.setWordWrap(True)
        fl.setTextInteractionFlags(Qt.TextSelectableByMouse)
        v.addWidget(fl)

        v.addSpacing(10)
        btns = QHBoxLayout(); btns.addStretch()
        c = QPushButton("取消连接"); c.setObjectName("DlgCancel"); c.setFixedSize(110, 38)
        c.setCursor(Qt.PointingHandCursor); c.clicked.connect(self.reject); btns.addWidget(c)
        btns.addSpacing(10)
        ok = QPushButton("仍然继续"); ok.setObjectName("DlgDanger" if mismatch else "DlgPrimary")
        ok.setFixedSize(130, 38); ok.setCursor(Qt.PointingHandCursor); ok.clicked.connect(self.accept)
        btns.addWidget(ok)
        v.addLayout(btns)



