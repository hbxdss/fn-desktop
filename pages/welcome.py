from PySide6.QtWidgets import QWidget, QVBoxLayout, QLabel, QLineEdit, QPushButton, QCheckBox
from PySide6.QtCore import Qt, Signal


class WelcomePage(QWidget):
    connect_requested = Signal(str, str, str, bool)
    search_requested = Signal(str)

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setObjectName("Page")

        v = QVBoxLayout(self)
        v.setAlignment(Qt.AlignCenter); v.setSpacing(14); v.addStretch()

        icon = QLabel("🟢"); icon.setAlignment(Qt.AlignCenter)
        icon.setStyleSheet("font-size: 56px;"); v.addWidget(icon)

        title = QLabel("飞牛终端"); title.setObjectName("BigTitle"); title.setAlignment(Qt.AlignCenter)
        v.addWidget(title)

        sub = QLabel("输入 FN ID 或 NAS 地址"); sub.setObjectName("SubTitle")
        sub.setAlignment(Qt.AlignCenter); v.addWidget(sub)
        v.addSpacing(28)

        self.input = QLineEdit(); self.input.setObjectName("BigInput")
        self.input.setPlaceholderText("FN ID 或 192.168.1.100:5666")
        self.input.setAlignment(Qt.AlignCenter); self.input.setFixedSize(440, 46)
        self.input.returnPressed.connect(self._go)
        v.addWidget(self.input, 0, Qt.AlignCenter)
        v.addSpacing(10)

        btn = QPushButton("连接"); btn.setObjectName("BigBtn"); btn.setFixedSize(220, 42)
        btn.setCursor(Qt.PointingHandCursor); btn.clicked.connect(self._go)
        v.addWidget(btn, 0, Qt.AlignCenter)
        v.addSpacing(28)

        lab = QLabel("账号设置（可选）"); lab.setObjectName("CardTitle"); lab.setAlignment(Qt.AlignCenter)
        v.addWidget(lab)

        form = QWidget(); form.setFixedWidth(440)
        fv = QVBoxLayout(form); fv.setContentsMargins(0, 0, 0, 0); fv.setSpacing(10)

        self.user_input = QLineEdit(); self.user_input.setPlaceholderText("用户名"); fv.addWidget(self.user_input)
        self.pass_input = QLineEdit(); self.pass_input.setPlaceholderText("密码")
        self.pass_input.setEchoMode(QLineEdit.密码); fv.addWidget(self.pass_input)
        self.remember_cb = QCheckBox("记住密码"); self.remember_cb.setChecked(True); fv.addWidget(self.remember_cb)

        v.addWidget(form, 0, Qt.AlignCenter)
        v.addSpacing(20)

        self.enc_label = QLabel(""); self.enc_label.setObjectName("Hint")
        self.enc_label.setAlignment(Qt.AlignCenter); v.addWidget(self.enc_label)

        h = QLabel("支持 5ddd.com / fnos.net / 局域网 IP，程序会自动识别")
        h.setObjectName("Hint"); h.setAlignment(Qt.AlignCenter); v.addWidget(h)
        v.addStretch()

    def set_encryption_label(self, text):
        self.enc_label.setText(text)

    def _go(self):
        text = self.input.text().strip()
        if not text:
            self.input.setFocus(); return
        if "." in text or ":" in text or text.startswith("http"):
            self.connect_requested.emit(text, self.user_input.text(),
                                          self.pass_input.text(), self.remember_cb.isChecked())
        else:
            self.search_requested.emit(text)

    def fill(self, url="", user="", password="", remember=True):
        if url: self.input.setText(url)
        if user: self.user_input.setText(user)
        if password: self.pass_input.setText(password)
        self.remember_cb.setChecked(remember)



