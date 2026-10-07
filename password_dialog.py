from PySide6.QtWidgets import QDialog, QVBoxLayout, QHBoxLayout, QLabel, QLineEdit, QPushButton
from PySide6.QtCore import Qt


class WindowsPasswordDialog(QDialog):
    def __init__(self, username, parent=None, retry=False, title_text=None):
        super().__init__(parent)
        self.setObjectName("Dlg")
        self.setWindowTitle("解锁凭据")
        self.setModal(True)
        self.setMinimumWidth(420)
        self.password = ""

        v = QVBoxLayout(self)
        v.setContentsMargins(26, 24, 26, 20); v.setSpacing(14)

        i = QLabel("🔑"); i.setObjectName("DlgIcon"); i.setAlignment(Qt.AlignCenter); v.addWidget(i)
        t = QLabel(title_text or ("输入 Windows 密码" if not retry else "密码错误，请重试"))
        t.setObjectName("DlgTitle"); t.setAlignment(Qt.AlignCenter); v.addWidget(t)
        d = QLabel(f"账户：{username}\n密码仅用于本地解密，不会被发送或存储。")
        d.setObjectName("DlgDesc"); d.setAlignment(Qt.AlignCenter); d.setWordWrap(True); v.addWidget(d)

        v.addSpacing(8)
        self.input = QLineEdit(); self.input.setEchoMode(QLineEdit.Password)
        self.input.setPlaceholderText("Windows 登录密码")
        self.input.returnPressed.connect(self._ok); v.addWidget(self.input)

        v.addSpacing(8)
        b = QHBoxLayout(); b.addStretch()
        c = QPushButton("取消"); c.setObjectName("DlgCancel"); c.setCursor(Qt.PointingHandCursor)
        c.clicked.connect(self.reject); b.addWidget(c)
        b.addSpacing(10)
        ok = QPushButton("解锁"); ok.setObjectName("DlgPrimary"); ok.setCursor(Qt.PointingHandCursor)
        ok.clicked.connect(self._ok); b.addWidget(ok)
        v.addLayout(b)

    def _ok(self):
        if not self.input.text():
            self.input.setFocus(); return
        self.password = self.input.text(); self.accept()


class GenericPasswordDialog(QDialog):
    def __init__(self, title_text, desc_text, placeholder="密码", parent=None):
        super().__init__(parent)
        self.setObjectName("Dlg"); self.setWindowTitle(title_text)
        self.setModal(True); self.setMinimumWidth(420); self.password = ""

        v = QVBoxLayout(self)
        v.setContentsMargins(26, 24, 26, 20); v.setSpacing(14)
        i = QLabel("🔐"); i.setObjectName("DlgIcon"); i.setAlignment(Qt.AlignCenter); v.addWidget(i)
        t = QLabel(title_text); t.setObjectName("DlgTitle"); t.setAlignment(Qt.AlignCenter); v.addWidget(t)
        d = QLabel(desc_text); d.setObjectName("DlgDesc"); d.setAlignment(Qt.AlignCenter); d.setWordWrap(True)
        v.addWidget(d)
        v.addSpacing(8)
        self.input = QLineEdit(); self.input.setEchoMode(QLineEdit.Password)
        self.input.setPlaceholderText(placeholder); self.input.returnPressed.connect(self._ok)
        v.addWidget(self.input)
        v.addSpacing(8)
        b = QHBoxLayout(); b.addStretch()
        c = QPushButton("取消"); c.setObjectName("DlgCancel"); c.setCursor(Qt.PointingHandCursor)
        c.clicked.connect(self.reject); b.addWidget(c)
        b.addSpacing(10)
        ok = QPushButton("确定"); ok.setObjectName("DlgPrimary"); ok.setCursor(Qt.PointingHandCursor)
        ok.clicked.connect(self._ok); b.addWidget(ok)
        v.addLayout(b)

    def _ok(self):
        if not self.input.text():
            self.input.setFocus(); return
        self.password = self.input.text(); self.accept()


class SaveCredentialDialog(QDialog):
    def __init__(self, url="", parent=None):
        super().__init__(parent)
        self.setObjectName("Dlg"); self.setWindowTitle("保存登录信息")
        self.setModal(True); self.setMinimumWidth(440)

        v = QVBoxLayout(self)
        v.setContentsMargins(26, 24, 26, 20); v.setSpacing(12)
        i = QLabel("💾"); i.setObjectName("DlgIcon"); i.setAlignment(Qt.AlignCenter); v.addWidget(i)
        t = QLabel("保存飞牛登录信息"); t.setObjectName("DlgTitle"); t.setAlignment(Qt.AlignCenter)
        v.addWidget(t)
        d = QLabel(f"输入你刚才在飞牛登录页使用的账号密码。\n程序会加密保存，下次自动填入。\n\n当前地址：{url or '（未设置）'}")
        d.setObjectName("DlgDesc"); d.setAlignment(Qt.AlignCenter); d.setWordWrap(True); v.addWidget(d)
        v.addSpacing(8)
        l1 = QLabel("用户名"); l1.setObjectName("CertFieldKey"); v.addWidget(l1)
        self.user_input = QLineEdit(); self.user_input.setPlaceholderText("飞牛登录用户名"); v.addWidget(self.user_input)
        l2 = QLabel("密码"); l2.setObjectName("CertFieldKey"); v.addWidget(l2)
        self.pwd_input = QLineEdit(); self.pwd_input.setEchoMode(QLineEdit.Password)
        self.pwd_input.setPlaceholderText("飞牛登录密码"); v.addWidget(self.pwd_input)
        v.addSpacing(10)
        b = QHBoxLayout(); b.addStretch()
        c = QPushButton("取消"); c.setObjectName("DlgCancel"); c.setCursor(Qt.PointingHandCursor)
        c.clicked.connect(self.reject); b.addWidget(c)
        b.addSpacing(10)
        ok = QPushButton("保存"); ok.setObjectName("DlgPrimary"); ok.setCursor(Qt.PointingHandCursor)
        ok.clicked.connect(self.accept); b.addWidget(ok)
        v.addLayout(b)

    def username(self):
        return self.user_input.text().strip()

    def password(self):
        return self.pwd_input.text()



