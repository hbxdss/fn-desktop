from PySide6.QtWidgets import (QDialog, QVBoxLayout, QHBoxLayout, QLabel, QLineEdit,
                               QPushButton, QComboBox, QMessageBox, QFileDialog)
from PySide6.QtCore import Qt
from secure_store import SecureStore


class MigrateDialog(QDialog):
    def __init__(self, store, parent=None):
        super().__init__(parent)
        self.setObjectName("Dlg"); self.setWindowTitle("密码迁移")
        self.setModal(True); self.setMinimumWidth(520)
        self.store = store; self.success = False

        v = QVBoxLayout(self)
        v.setContentsMargins(26, 24, 26, 20); v.setSpacing(12)
        i = QLabel("🔐"); i.setObjectName("DlgIcon"); i.setAlignment(Qt.AlignCenter); v.addWidget(i)
        t = QLabel("迁移加密方式"); t.setObjectName("DlgTitle"); t.setAlignment(Qt.AlignCenter); v.addWidget(t)
        cur = QLabel(f"当前方式：{store.encryption_label()}")
        cur.setObjectName("DlgDesc"); cur.setAlignment(Qt.AlignCenter); v.addWidget(cur)
        v.addSpacing(8)

        l1 = QLabel("当前密码（密码类加密才需要填）"); l1.setObjectName("CertFieldKey"); v.addWidget(l1)
        self.old_input = QLineEdit(); self.old_input.setEchoMode(QLineEdit.Password)
        self.old_input.setPlaceholderText("TPM / 指纹模式可留空"); v.addWidget(self.old_input)
        v.addSpacing(6)

        l2 = QLabel("新的加密方式"); l2.setObjectName("CertFieldKey"); v.addWidget(l2)
        self.mode_combo = QComboBox()
        if store.has_tpm:
            self.mode_combo.addItem("🔒 TPM 硬件加密（推荐）", "tpm")
        self.mode_combo.addItem("🔑 Windows 登录密码", "password")
        self.mode_combo.addItem("🔐 自定义密码", "custom")
        self.mode_combo.addItem("📌 PIN 码（⚠️ 弱）", "pin")
        self.mode_combo.addItem("🧬 机器指纹（无感，最弱）", "fingerprint")
        self.mode_combo.currentIndexChanged.connect(self._on_change)
        v.addWidget(self.mode_combo)

        self.nl1 = QLabel("新密码"); self.nl1.setObjectName("CertFieldKey"); v.addWidget(self.nl1)
        self.new_input = QLineEdit(); self.new_input.setEchoMode(QLineEdit.Password); v.addWidget(self.new_input)
        self.nl2 = QLabel("确认新密码"); self.nl2.setObjectName("CertFieldKey"); v.addWidget(self.nl2)
        self.new_input2 = QLineEdit(); self.new_input2.setEchoMode(QLineEdit.Password); v.addWidget(self.new_input2)

        self.hint = QLabel(""); self.hint.setObjectName("Hint"); self.hint.setWordWrap(True); v.addWidget(self.hint)
        v.addSpacing(10)
        b = QHBoxLayout(); b.addStretch()
        c = QPushButton("取消"); c.setObjectName("DlgCancel"); c.setCursor(Qt.PointingHandCursor)
        c.clicked.connect(self.reject); b.addWidget(c)
        b.addSpacing(10)
        ok = QPushButton("确认迁移"); ok.setObjectName("DlgPrimary"); ok.setCursor(Qt.PointingHandCursor)
        ok.clicked.connect(self._do); b.addWidget(ok)
        v.addLayout(b)
        self._on_change()

    def _on_change(self):
        m = self.mode_combo.currentData()
        need = m in ("password", "custom", "pin")
        self.nl1.setVisible(need); self.new_input.setVisible(need)
        self.nl2.setVisible(need); self.new_input2.setVisible(need)
        if m == "pin":
            self.hint.setText("⚠️ PIN 码无法被 Windows 验证，仅用作派生密钥。")
        elif m == "password":
            self.hint.setText("将调用 Windows LogonUser 验证密码。")
        elif m == "custom":
            self.hint.setText("自定义密码不会被 Windows 验证，请牢记。")
        elif m == "fingerprint":
            self.hint.setText("⚠️ 不设密码，无感解密。安全性最弱。")
        else:
            self.hint.setText("TPM 加密无需密码，安全性最高。")

    def _do(self):
        old = self.old_input.text()
        nm = self.mode_combo.currentData()
        np = ""
        if nm in ("password", "custom", "pin"):
            np = self.new_input.text()
            if not np:
                QMessageBox.warning(self, "提示", "请填写新密码"); return
            if np != self.new_input2.text():
                QMessageBox.warning(self, "提示", "两次密码不一致"); return
            if nm == "password" and not SecureStore.verify_windows_password(SecureStore.current_username(), np):
                QMessageBox.warning(self, "提示", "Windows 密码错误"); return
        ok, msg = self.store.migrate(old, nm, np)
        if ok:
            self.success = True
            QMessageBox.information(self, "成功", "迁移完成！")
            self.accept()
        else:
            QMessageBox.warning(self, "失败", msg)


class ExportDialog(QDialog):
    def __init__(self, store, data, parent=None):
        super().__init__(parent)
        self.setObjectName("Dlg"); self.setWindowTitle("导出凭据")
        self.setModal(True); self.setMinimumWidth(460)
        self.store = store; self.data = data; self.success = False

        v = QVBoxLayout(self)
        v.setContentsMargins(26, 24, 26, 20); v.setSpacing(12)
        i = QLabel("📤"); i.setObjectName("DlgIcon"); i.setAlignment(Qt.AlignCenter); v.addWidget(i)
        t = QLabel("导出凭据备份"); t.setObjectName("DlgTitle"); t.setAlignment(Qt.AlignCenter); v.addWidget(t)
        d = QLabel("导出的文件可在其他电脑上导入。"); d.setObjectName("DlgDesc")
        d.setAlignment(Qt.AlignCenter); d.setWordWrap(True); v.addWidget(d)
        v.addSpacing(6)
        l1 = QLabel("迁移密码（至少 8 位）"); l1.setObjectName("CertFieldKey"); v.addWidget(l1)
        self.p1 = QLineEdit(); self.p1.setEchoMode(QLineEdit.Password); v.addWidget(self.p1)
        l2 = QLabel("确认密码"); l2.setObjectName("CertFieldKey"); v.addWidget(l2)
        self.p2 = QLineEdit(); self.p2.setEchoMode(QLineEdit.Password); v.addWidget(self.p2)
        v.addSpacing(10)
        b = QHBoxLayout(); b.addStretch()
        c = QPushButton("取消"); c.setObjectName("DlgCancel"); c.setCursor(Qt.PointingHandCursor)
        c.clicked.connect(self.reject); b.addWidget(c)
        b.addSpacing(10)
        ok = QPushButton("导出"); ok.setObjectName("DlgPrimary"); ok.setCursor(Qt.PointingHandCursor)
        ok.clicked.connect(self._do); b.addWidget(ok)
        v.addLayout(b)

    def _do(self):
        p1, p2 = self.p1.text(), self.p2.text()
        if len(p1) < 8:
            QMessageBox.warning(self, "提示", "至少 8 位"); return
        if p1 != p2:
            QMessageBox.warning(self, "提示", "两次密码不一致"); return
        path, _ = QFileDialog.getSaveFileName(self, "保存备份", "fnos-credentials.fnosbak", "fnOS 凭据备份 (*.fnosbak)")
        if not path: return
        try:
            self.store.export_backup(self.data, p1, path)
            self.success = True
            QMessageBox.information(self, "成功", f"已导出：\n{path}")
            self.accept()
        except Exception as e:
            QMessageBox.warning(self, "失败", str(e))


class ImportDialog(QDialog):
    def __init__(self, store, parent=None):
        super().__init__(parent)
        self.setObjectName("Dlg"); self.setWindowTitle("导入凭据")
        self.setModal(True); self.setMinimumWidth(460)
        self.store = store; self.imported = None

        v = QVBoxLayout(self)
        v.setContentsMargins(26, 24, 26, 20); v.setSpacing(12)
        i = QLabel("📥"); i.setObjectName("DlgIcon"); i.setAlignment(Qt.AlignCenter); v.addWidget(i)
        t = QLabel("导入凭据备份"); t.setObjectName("DlgTitle"); t.setAlignment(Qt.AlignCenter); v.addWidget(t)
        d = QLabel("选择 .fnosbak 文件并输入迁移密码。"); d.setObjectName("DlgDesc")
        d.setAlignment(Qt.AlignCenter); d.setWordWrap(True); v.addWidget(d)
        v.addSpacing(6)
        self.path_input = QLineEdit(); self.path_input.setPlaceholderText("备份文件路径")
        self.path_input.setReadOnly(True); v.addWidget(self.path_input)
        pick = QPushButton("选择文件"); pick.setObjectName("GhostBtn")
        pick.setCursor(Qt.PointingHandCursor); pick.clicked.connect(self._pick); v.addWidget(pick)
        l = QLabel("迁移密码"); l.setObjectName("CertFieldKey"); v.addWidget(l)
        self.pwd_input = QLineEdit(); self.pwd_input.setEchoMode(QLineEdit.Password); v.addWidget(self.pwd_input)
        v.addSpacing(10)
        b = QHBoxLayout(); b.addStretch()
        c = QPushButton("取消"); c.setObjectName("DlgCancel"); c.setCursor(Qt.PointingHandCursor)
        c.clicked.connect(self.reject); b.addWidget(c)
        b.addSpacing(10)
        ok = QPushButton("导入"); ok.setObjectName("DlgPrimary"); ok.setCursor(Qt.PointingHandCursor)
        ok.clicked.connect(self._do); b.addWidget(ok)
        v.addLayout(b)

    def _pick(self):
        path, _ = QFileDialog.getOpenFileName(self, "选择备份文件", "", "fnOS 凭据备份 (*.fnosbak)")
        if path: self.path_input.setText(path)

    def _do(self):
        path = self.path_input.text().strip()
        pwd = self.pwd_input.text()
        if not path:
            QMessageBox.warning(self, "提示", "请先选择文件"); return
        if not pwd:
            QMessageBox.warning(self, "提示", "请输入密码"); return
        data = self.store.import_backup(path, pwd)
        if data is None:
            QMessageBox.warning(self, "失败", "密码错误或文件损坏"); return
        self.imported = data
        QMessageBox.information(self, "成功", "导入成功！")
        self.accept()



