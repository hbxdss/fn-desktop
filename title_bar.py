from PySide6.QtWidgets import QWidget, QHBoxLayout, QLabel, QPushButton
from PySide6.QtCore import Qt, QSize
from PySide6.QtGui import QPainter, QPixmap, QIcon, QPen, QColor
from config import asset_path


def _win_icon(kind, color):
    """用 QPainter 绘制窗口按钮图标。
    min=最小化 / max=最大化 / restore=还原 / close=关闭
    颜色跟随主题，无外部资源依赖。
    """
    pm = QPixmap(16, 16)
    pm.fill(Qt.transparent)
    p = QPainter(pm)
    p.setRenderHint(QPainter.RenderHint.Antialiasing)
    pen = QPen(QColor(color))
    pen.setWidthF(1.8)
    p.setPen(pen)
    if kind == "min":
        p.drawLine(3, 12, 13, 12)
    elif kind == "max":
        p.drawRect(3, 3, 10, 10)
    elif kind == "restore":
        p.drawRect(5, 5, 8, 8)
        p.drawRect(2, 2, 8, 8)
    elif kind == "close":
        p.drawLine(4, 4, 12, 12)
        p.drawLine(12, 4, 4, 12)
    p.end()
    return QIcon(pm)


class TitleBar(QWidget):
    def __init__(self, parent):
        super().__init__(parent)
        self.setObjectName("TitleBar")
        self.setFixedHeight(36)
        self._drag_pos = None

        h = QHBoxLayout(self)
        h.setContentsMargins(12, 0, 0, 0); h.setSpacing(8)

        logo = QLabel()
        _pm = QPixmap(asset_path("app.png"))
        if not _pm.isNull():
            logo.setPixmap(_pm.scaled(16, 16, Qt.KeepAspectRatio, Qt.SmoothTransformation))
        h.addWidget(logo)
        self.title_label = QLabel("飞牛桌面版"); self.title_label.setObjectName("TitleText"); h.addWidget(self.title_label)
        h.addStretch()

        self.min_btn = self._btn("最小化", False); self.min_btn.clicked.connect(parent.showMinimized); h.addWidget(self.min_btn)
        self.max_btn = self._btn("最大化", False); self.max_btn.clicked.connect(parent._toggle_max); h.addWidget(self.max_btn)
        self.close_btn = self._btn("关闭", True); self.close_btn.clicked.connect(parent.close); h.addWidget(self.close_btn)
        self.refresh_icons()

    def _btn(self, tip, is_close):
        b = QPushButton()
        b.setObjectName("CloseBtn" if is_close else "WinBtn")
        b.setToolTip(tip); b.setCursor(Qt.PointingHandCursor)
        b.setFixedSize(46, 36); b.setFocusPolicy(Qt.NoFocus)
        return b

    def add_nav_widget(self, w):
        """把导航控件（前进/后退/刷新/主页/渲染/菜单）插入到窗口按钮之前，
        与最小化/最大化/关闭放在同一行（用户要求顶部一行收纳所有控件）。"""
        idx = self.layout().indexOf(self.min_btn)
        self.layout().insertWidget(idx, w)

    def _icon_color(self):
        from config import PALETTES
        from data_store import get_store
        theme = get_store().get("theme", "mono")
        return PALETTES.get(theme, PALETTES["mono"])["text_soft"]

    def refresh_icons(self):
        """主题切换后重绘全部窗口按钮图标（颜色跟随主题）"""
        c = self._icon_color()
        self.min_btn.setIcon(_win_icon("min", c))
        self.max_btn.setIcon(_win_icon("restore" if self.window().isMaximized() else "max", c))
        self.close_btn.setIcon(_win_icon("close", c))
        for b in (self.min_btn, self.max_btn, self.close_btn):
            b.setIconSize(QSize(16, 16))

    def set_maximized(self, maximized):
        """窗口最大化状态变化时切换 最大化/还原 图标"""
        c = self._icon_color()
        self.max_btn.setIcon(_win_icon("restore" if maximized else "max", c))

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



