"""全局主题（QSS）集中管理。

九套主题 + 自定义强调色覆盖，颜色全部集中在 PALETTES 中，
QSS 模板通过 $key 占位符引用。新增主题只需向 PALETTES 增加一组颜色。
"""

import re
from pathlib import Path
from PySide6.QtCore import Qt
from PySide6.QtGui import QColor

# 自定义主题色预设色板（显示名 → 色值）
ACCENT_PRESETS = [
    ("蓝色", "#2563eb"),
    ("天蓝", "#0ea5e9"),
    ("青绿", "#10b981"),
    ("青色", "#0d9488"),
    ("紫色", "#8b5cf6"),
    ("靛蓝", "#4f46e5"),
    ("玫红", "#e11d48"),
    ("粉色", "#ec4899"),
    ("橙色", "#ea580c"),
    ("琥珀", "#f59e0b"),
    ("黄色", "#eab308"),
    ("灰蓝", "#64748b"),
]

PALETTES = {
    # 黑白（默认）：黑白灰单色系，无彩色强调
    "mono": {
        "bg": "#ffffff",          # 窗口/页面背景
        "panel": "#f3f4f6",       # 面板、输入框、菜单、卡片
        "panel_alt": "#e5e7eb",   # 次级面板
        "border": "#d1d5db",      # 边框
        "text": "#111111",        # 主文字
        "text_soft": "#4b5563",   # 次级文字
        "text_hint": "#6b7280",   # 弱提示文字
        "accent": "#111111",      # 强调色（主按钮、选中、焦点）
        "accent_hover": "#374151",
        "accent_text": "#ffffff", # 强调色上的文字
        "hover_bg": "#e5e7eb",    # 悬停背景
        "selected_bg": "#111111", # 列表选中背景
        "selected_text": "#ffffff",
        "danger": "#111111",      # 危险按钮（单色系下用黑白）
        "danger_hover": "#4b5563",
        "title_bar": "#ffffff",   # 标题栏背景
        "title_border": "#e5e7eb",
        "scroll": "#d1d5db",      # 滚动条滑块
    },
    # 暗色：深色背景 + 绿色强调（沿原版风格）
    "dark": {
        "bg": "#15171e",
        "panel": "#1f222a",
        "panel_alt": "#2a2e38",
        "border": "#2a2e38",
        "text": "#e2e8f0",
        "text_soft": "#94a3b8",
        "text_hint": "#64748b",
        "accent": "#10b981",
        "accent_hover": "#059669",
        "accent_text": "#ffffff",
        "hover_bg": "#2a2e38",
        "selected_bg": "#16302a",
        "selected_text": "#e2e8f0",
        "danger": "#ef4444",
        "danger_hover": "#dc2626",
        "title_bar": "#1f222a",
        "title_border": "#2a2e38",
        "scroll": "#2a2e38",
    },
    # 亮色：纯白底 + 绿色强调（浅色模式默认）
    "light": {
        "bg": "#ffffff",
        "panel": "#f8fafc",
        "panel_alt": "#e2e8f0",
        "border": "#cbd5e1",
        "text": "#0f172a",
        "text_soft": "#475569",
        "text_hint": "#64748b",
        "accent": "#10b981",
        "accent_hover": "#059669",
        "accent_text": "#ffffff",
        "hover_bg": "#f1f5f9",
        "selected_bg": "#ecfdf5",
        "selected_text": "#0f172a",
        "danger": "#ef4444",
        "danger_hover": "#dc2626",
        "title_bar": "#ffffff",
        "title_border": "#e2e8f0",
        "scroll": "#cbd5e1",
    },
    # 蓝色：浅色底 + 蓝色强调
    "blue": {
        "bg": "#f8fafc",
        "panel": "#ffffff",
        "panel_alt": "#e2e8f0",
        "border": "#cbd5e1",
        "text": "#0f172a",
        "text_soft": "#475569",
        "text_hint": "#64748b",
        "accent": "#2563eb",
        "accent_hover": "#1d4ed8",
        "accent_text": "#ffffff",
        "hover_bg": "#f1f5f9",
        "selected_bg": "#eff6ff",
        "selected_text": "#0f172a",
        "danger": "#dc2626",
        "danger_hover": "#b91c1c",
        "title_bar": "#ffffff",
        "title_border": "#e2e8f0",
        "scroll": "#cbd5e1",
    },
    # 紫色：深色底 + 紫色强调
    "purple": {
        "bg": "#171221",
        "panel": "#1f1a2e",
        "panel_alt": "#2a2440",
        "border": "#2a2440",
        "text": "#ede9fe",
        "text_soft": "#a5b4fc",
        "text_hint": "#7c7395",
        "accent": "#8b5cf6",
        "accent_hover": "#7c3aed",
        "accent_text": "#ffffff",
        "hover_bg": "#2a2440",
        "selected_bg": "#241a3d",
        "selected_text": "#ede9fe",
        "danger": "#ef4444",
        "danger_hover": "#dc2626",
        "title_bar": "#1f1a2e",
        "title_border": "#2a2440",
        "scroll": "#2a2440",
    },
    # 橙色：浅色底 + 橙色强调
    "orange": {
        "bg": "#f8fafc",
        "panel": "#ffffff",
        "panel_alt": "#e2e8f0",
        "border": "#cbd5e1",
        "text": "#0f172a",
        "text_soft": "#475569",
        "text_hint": "#64748b",
        "accent": "#ea580c",
        "accent_hover": "#c2410c",
        "accent_text": "#ffffff",
        "hover_bg": "#f1f5f9",
        "selected_bg": "#fff7ed",
        "selected_text": "#0f172a",
        "danger": "#dc2626",
        "danger_hover": "#b91c1c",
        "title_bar": "#ffffff",
        "title_border": "#e2e8f0",
        "scroll": "#cbd5e1",
    },
    # 青绿：深色底 + 青色强调
    "teal": {
        "bg": "#101a1e",
        "panel": "#16242a",
        "panel_alt": "#1f3037",
        "border": "#1f3037",
        "text": "#ccfbf1",
        "text_soft": "#5eead4",
        "text_hint": "#4f6b66",
        "accent": "#0d9488",
        "accent_hover": "#0f766e",
        "accent_text": "#ffffff",
        "hover_bg": "#1f3037",
        "selected_bg": "#0f2b29",
        "selected_text": "#ccfbf1",
        "danger": "#ef4444",
        "danger_hover": "#dc2626",
        "title_bar": "#16242a",
        "title_border": "#1f3037",
        "scroll": "#1f3037",
    },
    # 玫红：浅色底 + 玫红强调
    "rose": {
        "bg": "#f8fafc",
        "panel": "#ffffff",
        "panel_alt": "#e2e8f0",
        "border": "#cbd5e1",
        "text": "#0f172a",
        "text_soft": "#475569",
        "text_hint": "#64748b",
        "accent": "#e11d48",
        "accent_hover": "#be123c",
        "accent_text": "#ffffff",
        "hover_bg": "#f1f5f9",
        "selected_bg": "#fff1f2",
        "selected_text": "#0f172a",
        "danger": "#dc2626",
        "danger_hover": "#b91c1c",
        "title_bar": "#ffffff",
        "title_border": "#e2e8f0",
        "scroll": "#cbd5e1",
    },
    # 石墨：深色冷灰（灰蓝无彩色系）
    "graphite": {
        "bg": "#14161a",
        "panel": "#1c1f24",
        "panel_alt": "#262a31",
        "border": "#262a31",
        "text": "#e5e7eb",
        "text_soft": "#9ca3af",
        "text_hint": "#6b7280",
        "accent": "#94a3b8",
        "accent_hover": "#64748b",
        "accent_text": "#111827",
        "hover_bg": "#262a31",
        "selected_bg": "#374151",
        "selected_text": "#f3f4f6",
        "danger": "#dc2626",
        "danger_hover": "#b91c1c",
        "title_bar": "#1c1f24",
        "title_border": "#262a31",
        "scroll": "#262a31",
    },
}

# 主题选项（菜单 / 设置共用）：(key, 显示名)
THEME_OPTIONS = [
    ("mono", "黑白（默认）"),
    ("dark", "暗色"),
    ("light", "亮色"),
    ("blue", "蓝色"),
    ("purple", "紫色"),
    ("orange", "橙色"),
    ("teal", "青绿"),
    ("rose", "玫红"),
    ("graphite", "石墨"),
]

_QSS_TEMPLATE = """
* { font-family: "Microsoft YaHei UI", "Segoe UI", sans-serif; font-size: 13px; color: $text; }
QMainWindow, QWidget#Root { background: $bg; border: 1px solid $border; }
QWidget#TitleBar { background: $title_bar; border-bottom: 1px solid $title_border; }
QLabel#TitleText { color: $text; font-size: 13px; font-weight: 600; }
QPushButton#WinBtn { background: transparent; border: none; color: $text_soft; font-size: 14px; border-radius: 0; }
QPushButton#WinBtn:hover { background: $hover_bg; color: $text; }
QPushButton#CloseBtn { background: transparent; border: none; color: $text_soft; font-size: 14px; border-radius: 0; }
QPushButton#CloseBtn:hover { background: $danger; color: #ffffff; }
QWidget#Bar { background: $panel; border-bottom: 1px solid $border; }
QPushButton#Nav { background: transparent; border: none; color: $text_soft; border-radius: 8px; font-size: 15px; }
QPushButton#Nav:hover { background: $hover_bg; color: $accent; }
QPushButton#Nav:disabled { color: $text_hint; }
QPushButton#MenuBtn { background: transparent; border: none; color: $text_soft; border-radius: 8px; font-size: 15px; }
QPushButton#MenuBtn:hover { background: $hover_bg; color: $accent; }
QProgressBar { background: transparent; border: none; }
QProgressBar::chunk { background: $accent; }
QWidget#Page { background: $bg; }
QLabel#BigTitle { color: $text; font-size: 28px; font-weight: 700; }
QLabel#SubTitle { color: $text_soft; font-size: 14px; }
QLabel#Hint { color: $text_hint; font-size: 12px; }
QLabel#CardTitle { color: $text; font-size: 13px; font-weight: 600; }
QLabel#ModeLabel { color: $text_hint; font-size: 12px; margin-right: 10px; }
QLineEdit, QComboBox { background: $panel; border: 1px solid $border; border-radius: 10px; padding: 0 14px; color: $text; font-size: 14px; min-height: 38px; selection-background-color: $accent; }
QLineEdit:focus, QComboBox:focus { border: 1px solid $accent; }
QComboBox::drop-down { subcontrol-origin: padding; subcontrol-position: center right; width: 32px; border: none; background: transparent; }
QComboBox::down-arrow { image: none; width: 0; height: 0; border-left: 5px solid rgba(0,0,0,0); border-right: 5px solid rgba(0,0,0,0); border-top: 6px solid $text_soft; }
QLineEdit#BigInput { font-size: 15px; min-height: 46px; padding: 0 18px; border-radius: 12px; }
QPushButton#BigBtn { background: $accent; color: $accent_text; border: none; border-radius: 12px; font-size: 14px; font-weight: 600; min-height: 42px; padding: 0 24px; }
QPushButton#BigBtn:hover { background: $accent_hover; }
QPushButton#GhostBtn { background: transparent; color: $text_soft; border: 1px solid $border; border-radius: 12px; font-size: 13px; min-height: 42px; padding: 0 24px; }
QPushButton#GhostBtn:hover { background: $panel; color: $text; }
QCheckBox { color: $text_soft; font-size: 13px; spacing: 8px; }
QCheckBox::indicator { width: 16px; height: 16px; border-radius: 4px; border: 1px solid $border; background: $panel; }
QCheckBox::indicator:hover { border-color: $accent; }
QCheckBox::indicator:checked { background: $accent; border: 1px solid $accent; image: url($check_mark); }
QRadioButton { color: $text_soft; font-size: 13px; spacing: 8px; }
QRadioButton::indicator { width: 16px; height: 16px; border-radius: 8px; border: 1px solid $border; background: $panel; }
QRadioButton::indicator:hover { border-color: $accent; }
QRadioButton::indicator:checked { border: 5px solid $panel; background: $accent; }
QLabel#ErrIcon { font-size: 48px; }
QLabel#ErrTitle { color: $text; font-size: 18px; font-weight: 600; }
QLabel#ErrMsg { color: $text_soft; font-size: 13px; }
QLabel#ErrDetail { color: $text_hint; font-size: 12px; background: $panel; border: 1px solid $border; border-radius: 10px; padding: 14px; }
QDialog#Dlg { background: $bg; }
QMessageBox { background: $bg; }
QMessageBox QLabel { color: $text; font-size: 13px; }
QMessageBox QPushButton { background: $panel; color: $text; border: 1px solid $border; border-radius: 8px; padding: 6px 18px; min-height: 30px; font-size: 13px; }
QMessageBox QPushButton:hover { border-color: $accent; color: $accent; }
QToolTip { background: $panel; color: $text; border: 1px solid $border; border-radius: 6px; padding: 4px 8px; font-size: 12px; }
QWidget#EdgeResize { background: transparent; }
QWidget#EdgeResize:hover { background: $accent; }
QLabel#DlgTitle { color: $text; font-size: 16px; font-weight: 600; }
QLabel#DlgIcon { font-size: 36px; }
QLabel#DlgDesc { color: $text_soft; font-size: 13px; }
QLabel#CertSection { color: $text_hint; font-size: 12px; font-weight: 600; }
QLabel#CertFieldKey { color: $text_hint; font-size: 12px; }
QLabel#CertFieldVal { color: $text; font-size: 12px; }
QLabel#CertFp { color: $accent; font-family: "Consolas", monospace; font-size: 11px; background: $panel; border: 1px solid $border; border-radius: 10px; padding: 10px 12px; }
QPushButton#DlgCancel { background: transparent; color: $text_soft; border: 1px solid $border; border-radius: 10px; font-size: 13px; min-height: 38px; padding: 0 20px; }
QPushButton#DlgCancel:hover { background: $panel; color: $text; }
QPushButton#DlgPrimary { background: $accent; color: $accent_text; border: none; border-radius: 10px; font-size: 13px; font-weight: 600; min-height: 38px; padding: 0 20px; }
QPushButton#DlgPrimary:hover { background: $accent_hover; }
QPushButton#DlgDanger { background: $danger; color: #ffffff; border: none; border-radius: 10px; font-size: 13px; font-weight: 600; min-height: 38px; padding: 0 20px; }
QPushButton#DlgDanger:hover { background: $danger_hover; }
QListWidget { background: transparent; border: none; outline: none; }
QListWidget::item { background: $panel; border: 1px solid $border; border-radius: 10px; padding: 12px 16px; margin-bottom: 8px; color: $text; }
QListWidget::item:hover { border-color: $accent; background: $hover_bg; }
QListWidget::item:selected { background: $selected_bg; border-color: $accent; color: $selected_text; }
QMenu { background: $panel; border: 1px solid $border; border-radius: 10px; padding: 6px; }
QMenu::item { padding: 7px 24px 7px 14px; border-radius: 6px; color: $text; }
QMenu::item:selected { background: $accent; color: $accent_text; }
QScrollBar:vertical { background: transparent; width: 8px; }
QScrollBar::handle:vertical { background: $scroll; border-radius: 4px; min-height: 30px; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }

QComboBox QAbstractItemView {
    background: $panel;
    border: 1px solid $border;
    border-radius: 8px;
    color: $text;
    outline: none;
    padding: 4px;
    selection-background-color: $accent;
    selection-color: $accent_text;
}
QComboBox QAbstractItemView::item {
    min-height: 30px;
    padding: 0 12px;
    border-radius: 6px;
}
QComboBox QAbstractItemView::item:hover {
    background: $hover_bg;
    color: $accent;
}
QComboBox QAbstractItemView::item:selected {
    background: $accent;
    color: $accent_text;
}
"""


def asset_path(name):
    """返回 assets 目录下文件的绝对路径（源码运行 / PyInstaller 打包均兼容）。

    使用正斜杠：QSS 的 url() 只接受正斜杠分隔（Windows 反斜杠会解析失败）。
    """
    import sys as _sys
    if getattr(_sys, "frozen", False):
        base = Path(_sys.executable).resolve().parent
        # PyInstaller 6 onedir 会把数据放进 _internal/，两种布局都兼容
        if not (base / "assets").exists():
            base = base / "_internal"
    else:
        base = Path(__file__).resolve().parent
    return (base / "assets" / name).as_posix()


# 颜色模式选项（设置 / 菜单共用）：浅色 / 深色 / 跟随系统
MODE_OPTIONS = [
    ("light", "浅色模式"),
    ("dark", "深色模式"),
    ("auto", "跟随系统"),
]

# 深色系主题（旧版 9 主题数据迁移用），其余主题视为浅色系
DARK_THEMES = {"dark", "purple", "teal", "graphite"}


def normalize_theme_mode(raw, auto_flag=None):
    """把存储的主题值规整为三模式之一（light / dark / auto）。

    兼容旧版数据：auto_theme=True 或 theme=="auto" → auto；
    旧 9 主题按深浅归类；未知值回退 light。
    """
    if auto_flag or raw == "auto":
        return "auto"
    if raw in ("light", "dark"):
        return raw
    if raw in DARK_THEMES:
        return "dark"
    return "light"


def _derive_from_bg(bg):
    """从自定义背景色派生一组协调的面板相关色。

    返回 (panel, border, title_bar, hover_bg, scroll)。
    深色背景 → 面板略亮；浅色背景 → 面板略深，保证输入框/卡片/标题栏
    与背景有明显但柔和的层次。
    """
    c = QColor(bg)
    light = c.lightness() >= 140
    if light:
        panel = c.darker(104).name()
    else:
        panel = c.lighter(114).name()
    pc = QColor(panel)
    border = pc.darker(110).name()
    title_bar = panel
    hover = pc.lighter(106).name() if pc.lightness() < 140 else pc.darker(103).name()
    return panel, border, title_bar, hover, border


def build_qss(theme_name, accent=None, bg_dark=None, bg_light=None):
    """按主题名生成完整 QSS 字符串。未知主题回退到默认黑白。

    可选自定义覆盖（均为 #RRGGBB hex 或 None，None 使用主题默认）：
      accent   —— 主题颜色（强调色）：按钮/勾选/焦点等；自动计算 hover 与文字色。
      bg_dark  —— 深色模式背景色（深色系主题生效）。
      bg_light —— 浅色模式背景色（浅色系主题生效）。
      背景色生效时，面板/边框/标题栏/悬停/滚动条自动从背景色派生。

    资产占位符（如 $check_mark）先替换为绝对路径，颜色占位符再用正则
    精确匹配 $key，避免 $text 误伤 $text_soft/$text_hint 等前缀冲突。
    """
    p = PALETTES.get(theme_name, PALETTES["mono"])
    bg = bg_dark if theme_name in DARK_THEMES else bg_light
    if accent or bg:
        p = dict(p)
        if accent:
            c = QColor(accent)
            p["accent"] = accent
            p["accent_hover"] = c.darker(122).name()
            p["accent_text"] = "#111827" if c.lightness() > 140 else "#ffffff"
        if bg:
            p["bg"] = bg
            panel, border, title_bar, hover, scroll = _derive_from_bg(bg)
            p["panel"] = panel
            p["border"] = border
            p["title_bar"] = title_bar
            p["hover_bg"] = hover
            p["scroll"] = scroll
    qss = _QSS_TEMPLATE.replace("$check_mark", asset_path("check.png"))
    return re.sub(
        r"\$([a-z_]+)",
        lambda m: p.get(m.group(1), m.group(0)),
        qss,
    )


def effective_theme(app, key):
    """解析主题 key：key 为 "auto" 时按系统深浅色返回 dark / light。

    其余 key 若合法原样返回，非法回退 mono（防御未知值）。
    """
    if key == "auto":
        scheme = app.styleHints().colorScheme()
        return "dark" if scheme == Qt.ColorScheme.Dark else "light"
    return key if key in PALETTES else "mono"


def apply_theme(app, theme_name="mono", accent=None, bg_dark=None, bg_light=None):
    """把主题应用到 QApplication 全局：所有窗口、弹窗、菜单立即生效。

    accent / bg_dark / bg_light 为自定义颜色（hex 字符串或 None）：
      主题颜色（强调色）与深/浅模式背景色，覆盖主题对应颜色。
    """
    app.setStyleSheet(build_qss(theme_name, accent, bg_dark, bg_light))


# 向后兼容常量：默认主题（黑白）
QSS = build_qss("mono")
