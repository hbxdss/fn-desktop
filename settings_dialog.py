import re
from PySide6.QtWidgets import (QDialog, QVBoxLayout, QHBoxLayout, QLabel, QComboBox,
                               QPushButton, QCheckBox, QLineEdit, QMessageBox,
                               QApplication, QColorDialog, QRadioButton, QButtonGroup)
from PySide6.QtCore import Qt
from PySide6.QtGui import QColor
from data_store import get_store
from config import (apply_theme, effective_theme, normalize_theme_mode,
                    MODE_OPTIONS, ACCENT_PRESETS)


def _parse_hex(text):
    """解析色号：#RGB / #RRGGBB / RGB / RRGGBB，非法返回 None。"""
    s = (text or "").strip().lstrip("#")
    if len(s) == 3:
        s = "".join(c * 2 for c in s)
    return "#" + s.lower() if re.fullmatch(r"[0-9a-fA-F]{6}", s) else None


class SettingsDialog(QDialog):
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setObjectName("Dlg"); self.setWindowTitle("设置")
        self.setModal(True); self.setMinimumWidth(560)
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
        v.addSpacing(6)

        # ---- 调节器一：颜色模式（浅色 / 深色 / 跟随系统）----
        s2 = QLabel("颜色模式"); s2.setObjectName("CertSection"); v.addWidget(s2)
        mode = normalize_theme_mode(self.data.get("theme"), self.data.get("auto_theme", False))
        self.mode_group = QButtonGroup(self)
        self.rb_light = QRadioButton("浅色模式")
        self.rb_dark = QRadioButton("深色模式")
        self.rb_auto = QRadioButton("跟随系统")
        self.rb_light.setChecked(mode == "light")
        self.rb_dark.setChecked(mode == "dark")
        self.rb_auto.setChecked(mode == "auto")
        self.mode_group.addButton(self.rb_light, 0)
        self.mode_group.addButton(self.rb_dark, 1)
        self.mode_group.addButton(self.rb_auto, 2)
        mrow = QHBoxLayout(); mrow.setSpacing(18)
        mrow.addWidget(self.rb_light); mrow.addWidget(self.rb_dark); mrow.addWidget(self.rb_auto)
        mrow.addStretch()
        v.addLayout(mrow)
        mh = QLabel(" 深色模式 / 浅色模式决定背景深浅；跟随系统则随 Windows 深浅色自动切换。")
        mh.setObjectName("Hint"); mh.setWordWrap(True); v.addWidget(mh)

        # ---- 调节器二：主题色（连接按钮等强调元素）----
        s3 = QLabel("主题色（连接按钮、勾选等强调元素）"); s3.setObjectName("CertSection"); v.addWidget(s3)
        row = QHBoxLayout(); row.setSpacing(8)
        sw = QHBoxLayout(); sw.setSpacing(6)
        self._swatches = []
        for p_label, hexv in ACCENT_PRESETS:
            b = QPushButton(); b.setFixedSize(22, 22)
            b.setCursor(Qt.PointingHandCursor); b.setToolTip(p_label)
            b.setStyleSheet(f"background: {hexv}; border-radius: 11px; border: 1px solid rgba(0,0,0,0);")
            b.clicked.connect(lambda _=False, h=hexv: self._pick_swatch(h))
            sw.addWidget(b)
            self._swatches.append((b, hexv))
        row.addLayout(sw)
        self.custom_accent_edit = self._hex_edit("custom_accent", 118)
        row.addWidget(self.custom_accent_edit)
        row.addWidget(self._pick_btn("custom_accent", "主题色"))
        row.addStretch()
        v.addLayout(row)

        # ---- 自定义背景（可选）：深色模式 / 浅色模式 ----
        s4 = QLabel("自定义背景（可选，留空使用默认）"); s4.setObjectName("CertSection"); v.addWidget(s4)
        d1 = QLabel("深色模式背景"); d1.setObjectName("Hint"); d1.setFixedWidth(96)
        r1 = QHBoxLayout(); r1.setSpacing(8); r1.addWidget(d1)
        self.custom_bg_dark_edit = self._hex_edit("custom_bg_dark", 118)
        r1.addWidget(self.custom_bg_dark_edit)
        r1.addWidget(self._pick_btn("custom_bg_dark", "深色模式背景"))
        r1.addStretch(); v.addLayout(r1)

        l1 = QLabel("浅色模式背景"); l1.setObjectName("Hint"); l1.setFixedWidth(96)
        r2 = QHBoxLayout(); r2.setSpacing(8); r2.addWidget(l1)
        self.custom_bg_light_edit = self._hex_edit("custom_bg_light", 118)
        r2.addWidget(self.custom_bg_light_edit)
        r2.addWidget(self._pick_btn("custom_bg_light", "浅色模式背景"))
        r2.addStretch(); v.addLayout(r2)

        r = QPushButton("恢复默认颜色"); r.setObjectName("DlgCancel"); r.setFixedSize(130, 32)
        r.setCursor(Qt.PointingHandCursor); r.clicked.connect(self._reset_colors)
        rl = QHBoxLayout(); rl.addWidget(r); rl.addStretch()
        v.addLayout(rl)
        v.addSpacing(10)

        b = QHBoxLayout(); b.addStretch()
        c = QPushButton("取消"); c.setObjectName("DlgCancel"); c.setFixedSize(100, 38)
        c.setCursor(Qt.PointingHandCursor); c.clicked.connect(self.reject); b.addWidget(c)
        b.addSpacing(10)
        s_btn = QPushButton("保存"); s_btn.setObjectName("DlgPrimary"); s_btn.setFixedSize(100, 38)
        s_btn.setCursor(Qt.PointingHandCursor); s_btn.clicked.connect(self._save); b.addWidget(s_btn)
        v.addLayout(b)

    # ---- 控件工厂 ----
    def _hex_edit(self, key, width):
        edit = QLineEdit(); edit.setObjectName("BigInput")
        edit.setFixedSize(width, 34); edit.setPlaceholderText("#RRGGBB")
        cur = self.data.get(key)
        if cur:
            edit.setText(cur)
        return edit

    def _pick_btn(self, key, label):
        btn = QPushButton("色图"); btn.setObjectName("DlgCancel"); btn.setFixedSize(56, 30)
        btn.setCursor(Qt.PointingHandCursor)
        btn.setToolTip("打开色图，自由选色")
        btn.clicked.connect(lambda _=False, k=key, lb=label: self._pick_color(k, lb))
        return btn

    # ---- 交互 ----
    def _pick_swatch(self, hexv):
        """点击色板：填入主题色输入框并高亮对应色块。"""
        self.custom_accent_edit.setText(hexv)
        for b, h in self._swatches:
            sel = "border: 2px solid #ffffff;" if h == hexv else "border: 1px solid rgba(0,0,0,0);"
            b.setStyleSheet(f"background: {h}; border-radius: 10px; {sel}")

    def _pick_color(self, key, label):
        """打开系统取色器，选中的颜色填入对应输入框。"""
        edit = getattr(self, f"{key}_edit")
        init = QColor(_parse_hex(edit.text())) if edit.text().strip() else QColor()
        picked = QColorDialog.getColor(init, self, f"选择{label}")
        if picked.isValid():
            edit.setText(picked.name())

    def _reset_colors(self):
        """清空三项自定义颜色，恢复默认。"""
        for key in ("custom_accent", "custom_bg_dark", "custom_bg_light"):
            getattr(self, f"{key}_edit").clear()
        for b, h in self._swatches:
            b.setStyleSheet(f"background: {h}; border-radius: 11px; border: 1px solid rgba(0,0,0,0);")

    # ---- 保存 ----
    def _save(self):
        # 颜色模式
        rid = self.mode_group.checkedId()
        mode = ["light", "dark", "auto"][rid]
        self.data.set("theme", mode)
        if mode != "auto":
            self.data.set("auto_theme", False)

        # 自定义颜色
        vals = {}
        for key, name in [("custom_accent", "主题色"),
                          ("custom_bg_dark", "深色模式背景"),
                          ("custom_bg_light", "浅色模式背景")]:
            text = getattr(self, f"{key}_edit").text()
            if text.strip():
                parsed = _parse_hex(text)
                if parsed is None:
                    QMessageBox.warning(self, "色号无效", f"「{name}」的色号 {text.strip()} 无法解析。\n请使用 #RRGGBB 格式，例如 #2563eb。")
                    return
                vals[key] = parsed
            else:
                vals[key] = None
        self.data.set("custom_accent", vals["custom_accent"])
        self.data.set("custom_bg_dark", vals["custom_bg_dark"])
        self.data.set("custom_bg_light", vals["custom_bg_light"])

        app = QApplication.instance()
        theme = effective_theme(app, mode)
        apply_theme(app, theme, vals["custom_accent"], vals["custom_bg_dark"], vals["custom_bg_light"])
        win = self.parent()
        if win is not None and hasattr(win, "title_bar"):
            win.title_bar.refresh_icons()
        QMessageBox.information(self, "提示", "设置已保存！\n颜色模式与主题色已立即生效；渲染模式需重启程序。")
        self.accept()
