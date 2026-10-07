from PySide6.QtWidgets import QWidget, QVBoxLayout, QLabel
from PySide6.QtCore import Qt


class LoadingPage(QWidget):
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setObjectName("Page")

        v = QVBoxLayout(self)
        v.setAlignment(Qt.AlignCenter); v.setSpacing(16)

        self.icon = QLabel("⏳"); self.icon.setAlignment(Qt.AlignCenter)
        self.icon.setStyleSheet("font-size: 42px;"); v.addWidget(self.icon)

        self.label = QLabel("正在连接..."); self.label.setObjectName("SubTitle")
        self.label.setAlignment(Qt.AlignCenter); v.addWidget(self.label)

        self.hint = QLabel(""); self.hint.setObjectName("Hint")
        self.hint.setAlignment(Qt.AlignCenter); v.addWidget(self.hint)

    def set_message(self, text, hint=""):
        self.label.setText(text)
        self.hint.setText(hint)



