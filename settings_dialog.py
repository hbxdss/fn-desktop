from PySide6.QtWidgets import QDialog, QVBoxLayout, QHBoxLayout, QLabel, QComboBox, QPushButton, QMessageBox
from PySide6.QtCore import Qt
from data_store import get_store


class SettingsDialog(QDialog):
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setObjectName("Dlg"); self.setWindowTitle("设置")
        self.setModal(True); self.setMinimumWidth(520)
        self.data = get_store()

        v = QVBoxLayout(self)
        v.setContentsMargins(26, 24, 26, 20); v.setSpacing(14)
        t = QLabel("⚙️  渲染模式设置"); t.setObjectName("DlgTitle"); t.setAlignment(Qt.AlignCenter)
        v.addWidget(t)
        d = QLabel("修改渲染模式后需要重启应用才能生效。")
        d.setObjectName("DlgDesc"); d.setAlignment(Qt.AlignCenter); v.addWidget(d)
        v.addSpacing(6)
        s = QLabel("选择渲染方式"); s.setObjectName("CertSection"); v.addWidget(s)

        self.combo = QComboBox(); self.combo.setMinimumHeight(36)
        self.combo.addItem("自动检测（推荐）", "auto")
        self.combo.addItem("硬件加速 (GPU) - 物理机首选", "hardware")
        self.combo.addItem("软件渲染 (CPU) - 虚拟机首选", "software")
        cur = self.data.get("render_mode", "auto")
        idx = self.combo.findData(cur)
        if idx >= 0:
            self.combo.setCurrentIndex(idx)
        v.addWidget(self.combo)

        h = QLabel(" 硬件加速：网页滑动丝滑，但虚拟机或老显卡容易黑屏。\n 软件渲染：绝对稳定，但 CPU 占用高。")
        h.setObjectName("Hint"); h.setWordWrap(True); v.addWidget(h)
        v.addSpacing(10)
        b = QHBoxLayout(); b.addStretch()
        c = QPushButton("取消"); c.setObjectName("DlgCancel"); c.setFixedSize(100, 38)
        c.setCursor(Qt.PointingHandCursor); c.clicked.connect(self.reject); b.addWidget(c)
        b.addSpacing(10)
        s2 = QPushButton("保存"); s2.setObjectName("DlgPrimary"); s2.setFixedSize(100, 38)
        s2.setCursor(Qt.PointingHandCursor); s2.clicked.connect(self._save); b.addWidget(s2)
        v.addLayout(b)

    def _save(self):
        self.data.set("render_mode", self.combo.currentData())
        QMessageBox.information(self, "提示", "设置已保存！\n请关闭并重新打开程序。")
        self.accept()



