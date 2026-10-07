from PySide6.QtWidgets import QWidget, QVBoxLayout, QHBoxLayout, QLabel, QPushButton
from PySide6.QtCore import Qt, Signal


class ErrorPage(QWidget):
    retry_requested = Signal()
    change_address_requested = Signal()

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setObjectName("Page")

        v = QVBoxLayout(self); v.setAlignment(Qt.AlignCenter); v.setSpacing(12); v.addStretch()

        self.icon = QLabel("⚠️"); self.icon.setObjectName("ErrIcon")
        self.icon.setAlignment(Qt.AlignCenter); v.addWidget(self.icon)

        self.title_label = QLabel("无法连接"); self.title_label.setObjectName("ErrTitle")
        self.title_label.setAlignment(Qt.AlignCenter); v.addWidget(self.title_label)

        self.msg_label = QLabel(""); self.msg_label.setObjectName("ErrMsg")
        self.msg_label.setAlignment(Qt.AlignCenter); self.msg_label.setWordWrap(True)
        self.msg_label.setMaximumWidth(520)
        v.addWidget(self.msg_label, 0, Qt.AlignCenter)
        v.addSpacing(8)

        self.detail_label = QLabel(""); self.detail_label.setObjectName("ErrDetail")
        self.detail_label.setAlignment(Qt.AlignCenter); self.detail_label.setWordWrap(True)
        self.detail_label.setMaximumWidth(520); self.detail_label.hide()
        v.addWidget(self.detail_label, 0, Qt.AlignCenter)
        v.addSpacing(16)

        b = QHBoxLayout(); b.addStretch()
        r = QPushButton("重试"); r.setObjectName("BigBtn"); r.setFixedSize(120, 42)
        r.setCursor(Qt.PointingHandCursor); r.clicked.connect(self.retry_requested); b.addWidget(r)
        b.addSpacing(10)
        c = QPushButton("更换地址"); c.setObjectName("GhostBtn"); c.setFixedSize(120, 42)
        c.setCursor(Qt.PointingHandCursor); c.clicked.connect(self.change_address_requested); b.addWidget(c)
        b.addStretch()
        v.addLayout(b); v.addStretch()

    def set_error(self, title, msg, detail=""):
        self.title_label.setText(title)
        self.msg_label.setText(msg)
        if detail:
            self.detail_label.setText(detail); self.detail_label.show()
        else:
            self.detail_label.hide()



