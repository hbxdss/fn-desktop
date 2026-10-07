from PySide6.QtWidgets import QDialog, QVBoxLayout, QHBoxLayout, QLabel, QPushButton, QListWidget, QListWidgetItem
from PySide6.QtCore import Qt


class SearchResultDialog(QDialog):
    def __init__(self, results, parent=None):
        super().__init__(parent)
        self.setObjectName("Dlg")
        self.setWindowTitle("选择连接地址")
        self.setModal(True)
        self.setMinimumWidth(520)
        self._picked = ""

        v = QVBoxLayout(self)
        v.setContentsMargins(26, 24, 26, 20)
        v.setSpacing(14)

        i = QLabel("🔍")
        i.setObjectName("DlgIcon")
        i.setAlignment(Qt.AlignCenter)
        v.addWidget(i)

        t = QLabel("发现多个可用地址")
        t.setObjectName("DlgTitle")
        t.setAlignment(Qt.AlignCenter)
        v.addWidget(t)

        d = QLabel("该 FN ID 在 5ddd.com 和 fnos.net 上都能访问。\n推荐选择 5ddd.com。")
        d.setObjectName("DlgDesc")
        d.setAlignment(Qt.AlignCenter)
        d.setWordWrap(True)
        v.addWidget(d)
        v.addSpacing(6)

        self.list_widget = QListWidget()
        self.list_widget.setMinimumHeight(140)
        self.list_widget.itemDoubleClicked.connect(lambda _: self._pick())

        ordered = sorted(results, key=lambda x: 0 if x["domain"] == "5ddd.com" else 1)
        for r in ordered:
            label = "推荐" if r["domain"] == "5ddd.com" else "备选"
            item = QListWidgetItem(f"{r['domain']}   [{label}]\n{r['url']}")
            item.setData(Qt.UserRole, r["url"])
            self.list_widget.addItem(item)
        self.list_widget.setCurrentRow(0)
        v.addWidget(self.list_widget)

        v.addSpacing(6)
        b = QHBoxLayout()
        b.addStretch()

        c = QPushButton("取消")
        c.setObjectName("DlgCancel")
        c.setCursor(Qt.PointingHandCursor)
        c.clicked.connect(self.reject)
        b.addWidget(c)
        b.addSpacing(10)

        ok = QPushButton("连接")
        ok.setObjectName("DlgPrimary")
        ok.setCursor(Qt.PointingHandCursor)
        ok.clicked.connect(self._pick)
        b.addWidget(ok)
        v.addLayout(b)

    def _pick(self):
        item = self.list_widget.currentItem()
        if item:
            self._picked = item.data(Qt.UserRole)
            self.accept()

    def picked_url(self):
        return self._picked


