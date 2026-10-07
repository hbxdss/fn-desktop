from PySide6.QtWidgets import QWidget, QHBoxLayout, QLabel, QPushButton
from PySide6.QtCore import Qt


class TitleBar(QWidget):
    def __init__(self, parent):
        super().__init__(parent)
        self.setObjectName("TitleBar")
        self.setFixedHeight(36)
        self._drag_pos = None

        h = QHBoxLayout(self)
        h.setContentsMargins(12, 0, 0, 0); h.setSpacing(8)

        logo = QLabel("🟢"); logo.setStyleSheet("font-size: 12px;"); h.addWidget(logo)
        self.title_label = QLabel("飞牛终端"); self.title_label.setObjectName("TitleText"); h.addWidget(self.title_label)
        h.addStretch()

        self.min_btn = self._btn("", "最小化", False); self.min_btn.clicked.connect(parent.showMinimized); h.addWidget(self.min_btn)
        self.max_btn = self._btn("", "最大化", False); self.max_btn.clicked.connect(parent._toggle_max); h.addWidget(self.max_btn)
        self.close_btn = self._btn("✕", "关闭", True); self.close_btn.clicked.connect(parent.close); h.addWidget(self.close_btn)

    def _btn(self, text, tip, is_close):
        b = QPushButton(text)
        b.setObjectName("CloseBtn" if is_close else "WinBtn")
        b.setToolTip(tip); b.setCursor(Qt.PointingHandCursor)
        b.setFixedSize(46, 36); b.setFocusPolicy(Qt.NoFocus)
        return b

    def set_title(self, text):
        self.title_label.setText(text)

    def mousePressEvent(self, e):
        if e.button() == Qt.LeftButton:
            self._drag_pos = e.globalPosition().toPoint() - self.window().frameGeometry().topLeft()
            e.accept()

    def mouseMoveEvent(self, e):
        if self._drag_pos and e.buttons() & Qt.LeftButton:
            win = self.window()
            if win.isMaximized():
                ratio = e.globalPosition().x() / max(win.width(), 1)
                win.showNormal()
                win.move(int(e.globalPosition().x() - win.width() * ratio), e.globalPosition().y() - 18)
                self._drag_pos = e.globalPosition().toPoint() - win.frameGeometry().topLeft()
            else:
                win.move(e.globalPosition().toPoint() - self._drag_pos)
            e.accept()

    def mouseReleaseEvent(self, e):
        self._drag_pos = None

    def mouseDoubleClickEvent(self, e):
        if e.button() == Qt.LeftButton:
            self.window()._toggle_max()



