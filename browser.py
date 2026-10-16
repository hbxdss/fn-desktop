from pathlib import Path
import json
from PySide6.QtWidgets import (QMainWindow, QWidget, QVBoxLayout, QHBoxLayout, QPushButton,
                               QProgressBar, QStackedWidget, QMenu, QLabel, QDialog, QMessageBox,
                               QApplication)
from PySide6.QtCore import Qt, QUrl, QTimer, QThread, QEvent, Signal as QtSignal
from PySide6.QtGui import QAction, QIcon
from PySide6.QtWebEngineWidgets import QWebEngineView

from config import apply_theme, MODE_OPTIONS, effective_theme, normalize_theme_mode
from data_store import get_store
from trust_store import TrustStore
from custom_page import CustomPage
from title_bar import TitleBar
from pages.welcome import WelcomePage
from pages.loading import LoadingPage
from pages.error import ErrorPage
from settings_dialog import SettingsDialog
from secure_store import SecureStore
from password_dialog import WindowsPasswordDialog, GenericPasswordDialog, SaveCredentialDialog
from migrate_dialog import MigrateDialog, ExportDialog, ImportDialog
from fn_connect import search_fnos
import keyboard_sim
from search_dialog import SearchResultDialog


class SearchWorker(QThread):
    done = QtSignal(list)
    def __init__(self, fnid):
        super().__init__()
        self.fnid = fnid
    def run(self):
        try:
            r = search_fnos(self.fnid)
        except Exception:
            r = []
        self.done.emit(r)


class _EdgeResize(QWidget):
    """无边框窗口的四周/四角拖拽调大小条，提供与正常窗口一致的调整体验。
    dirmask 位标记：1=左 2=右 4=上 8=下。
    背景色用主题底色（config.py 中 QWidget#EdgeResize { background: $bg; }），
    浅色主题下与白底一致、深色主题下与深底一致，无白块/黑块。"""

    def __init__(self, parent, dirmask):
        super().__init__(parent)
        self._d = dirmask
        self._start = None
        self.setMouseTracking(True)
        self.setCursor({
            1: Qt.SizeHorCursor, 2: Qt.SizeHorCursor,
            4: Qt.SizeVerCursor, 8: Qt.SizeVerCursor,
            5: Qt.SizeFDiagCursor, 10: Qt.SizeFDiagCursor,
            6: Qt.SizeBDiagCursor, 9: Qt.SizeBDiagCursor,
        }.get(dirmask, Qt.ArrowCursor))

    def mousePressEvent(self, e):
        if e.button() == Qt.LeftButton and not self.window().isMaximized():
            self._start = (e.globalPosition(), self.window().geometry())
            e.accept()
            return
        super().mousePressEvent(e)

    def mouseMoveEvent(self, e):
        if self._start is not None:
            sp, g = self._start
            dx = e.globalPosition().x() - sp.x()
            dy = e.globalPosition().y() - sp.y()
            x, y, w, h = g.x(), g.y(), g.width(), g.height()
            if self._d & 1:
                x = g.x() + dx
                w = g.width() - dx
            if self._d & 2:
                w = g.width() + dx
            if self._d & 4:
                y = g.y() + dy
                h = g.height() - dy
            if self._d & 8:
                h = g.height() + dy
            win = self.window()
            mw, mh = win.minimumWidth(), win.minimumHeight()
            if w < mw:
                w = mw
            if h < mh:
                h = mh
            win.setGeometry(x, y, w, h)
            e.accept()
            return
        super().mouseMoveEvent(e)

    def mouseReleaseEvent(self, e):
        self._start = None
        super().mouseReleaseEvent(e)


class Browser(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("飞牛桌面版")
        self.resize(1200, 780)
        self.setWindowFlags(Qt.Window | Qt.FramelessWindowHint)
        # 无边框窗口默认无法用鼠标拖拽调整大小：最小尺寸 + 四周透明拖拽条
        self.setMinimumSize(900, 600)
        # 样式统一由 config.apply_theme 在 QApplication 全局管理（支持主题切换）
        icon = self._app_icon_path()
        if icon:
            self.setWindowIcon(QIcon(icon))

        self.data = get_store()
        self.trust_store = TrustStore()
        self.secure_store = SecureStore()
        self._session_password = ""

        self._load_timer = QTimer(self)
        self._load_timer.setSingleShot(True)
        self._load_timer.timeout.connect(self._on_load_timeout)

        self._build_ui()
        # 记住密码偏好恢复：上次是否勾选“记住密码”持久化在 data.json
        self.welcome_page.remember_cb.setChecked(self.data.get("remember_password", True))
        self._restore_credentials()

        QTimer.singleShot(600, self._show_first_run_notice)

        # 系统深浅色变化时自动切换主题（仅在“跟随系统”开启时生效）
        QApplication.instance().styleHints().colorSchemeChanged.connect(self._on_color_scheme_changed)

        saved_url = (self.data.get("url") or "").strip()
        # 跳过无效地址（只有协议头等），直接回到欢迎页，避免加载空地址报错页
        if saved_url and saved_url.lower() not in ("https://", "http://", "https:", "http:", "https", "http"):
            self._start_loading(saved_url)
            self.web_view.setUrl(QUrl(saved_url))
        else:
            self.stack.setCurrentWidget(self.welcome_page)

    def _build_ui(self):
        root = QWidget()
        root.setObjectName("Root")
        v = QVBoxLayout(root)
        # 四周 8px 主题底色边距：既是窗口“边框”，也是拖拽调大小的热区。
        # webview（原生窗口）只覆盖边距内的内容区，边缘热区完全不会被它盖住，
        # 普通控件即可命中鼠标——不需要 native、透明或 z-order 提升。
        v.setContentsMargins(8, 8, 8, 8)
        v.setSpacing(0)

        self.title_bar = TitleBar(self)
        v.addWidget(self.title_bar)

        # 导航控件全部放入标题栏（与最小化/最大化/关闭同一行）
        self.back_btn = self._nav_btn("←", "后退")
        self.back_btn.clicked.connect(self._back)
        self.title_bar.add_nav_widget(self.back_btn)

        self.forward_btn = self._nav_btn("→", "前进")
        self.forward_btn.clicked.connect(self._forward)
        self.title_bar.add_nav_widget(self.forward_btn)

        self.refresh_btn = self._nav_btn("⟳", "刷新")
        self.refresh_btn.clicked.connect(self._refresh)
        self.title_bar.add_nav_widget(self.refresh_btn)

        self.home_btn = self._nav_btn("⌂", "回到飞牛首页")
        self.home_btn.clicked.connect(self._go_home)
        self.title_bar.add_nav_widget(self.home_btn)

        mode = self.data.get("render_mode", "auto")
        txt = {"software": "🟡 软件渲染", "hardware": "🟢 硬件加速"}.get(mode, "🔵 自动")
        self.mode_label = QLabel(txt)
        self.mode_label.setObjectName("ModeLabel")
        self.title_bar.add_nav_widget(self.mode_label)

        self.menu_btn = self._nav_btn("⋮", "菜单")
        self.menu_btn.setObjectName("MenuBtn")
        self.menu_btn.clicked.connect(self._show_menu)
        self.title_bar.add_nav_widget(self.menu_btn)

        self.progress = QProgressBar()
        self.progress.setTextVisible(False)
        self.progress.setFixedHeight(2)
        self.progress.hide()
        v.addWidget(self.progress)

        self.stack = QStackedWidget()
        self.welcome_page = WelcomePage()
        self.loading_page = LoadingPage()
        self.error_page = ErrorPage()

        self.web_view = QWebEngineView()
        self.custom_page = CustomPage(self.trust_store)
        self.web_view.setPage(self.custom_page)
        self.custom_page.focus_ready.connect(self._on_focus_ready)
        

        self.welcome_page.connect_requested.connect(self._on_connect)
        self.welcome_page.search_requested.connect(self._on_search)
        self.error_page.retry_requested.connect(self._retry)
        self.error_page.change_address_requested.connect(self._goto_welcome)

        self.stack.addWidget(self.welcome_page)
        self.stack.addWidget(self.loading_page)
        self.stack.addWidget(self.error_page)
        self.stack.addWidget(self.web_view)

        v.addWidget(self.stack, 1)
        self.setCentralWidget(root)
        # 页面切换时刷新后退/前进可用状态
        self.stack.currentChanged.connect(self._on_stack_changed)

        # 四周/四角拖拽调大小条：放在 8px 主题色边距区（webview 不覆盖此处），
        # 普通控件即可被真实鼠标命中，拖拽与正常窗口一致
        self._edges = {}
        for k, d in (("lt", 5), ("rt", 6), ("lb", 9), ("rb", 10),
                     ("l", 1), ("r", 2), ("t", 4), ("b", 8)):
            ed = _EdgeResize(self, d)
            ed.setObjectName("EdgeResize")
            self._edges[k] = ed
        self._layout_edges()
        self.back_btn.setEnabled(False)
        self.forward_btn.setEnabled(False)

        self.web_view.urlChanged.connect(self._on_url_changed)
        self.web_view.loadStarted.connect(self._on_load_started)
        self.web_view.loadProgress.connect(self._on_load_progress)
        self.web_view.loadFinished.connect(self._on_load_finished)
        self.web_view.titleChanged.connect(self._on_title_changed)

        self._refresh_enc_label()

    def _refresh_enc_label(self):
        self.welcome_page.set_encryption_label(self._build_enc_text())

    def _build_enc_text(self):
        m = self.secure_store.get_mode() or self.secure_store.effective_mode()
        text = {
            "tpm": "🔒 当前使用 TPM 2.0 硬件加密（自动，无需密码）",
            "fingerprint": "🧬 当前使用机器指纹加密（无感解密）",
            "password": "🔑 当前使用 Windows 密码加密（启动时需输入密码）",
            "custom": "🔐 当前使用自定义密码加密（启动时需输入）",
            "pin": "📌 当前使用 PIN 码加密（安全性较弱）",
        }.get(m, "未启用凭据加密")
        note = self.secure_store.password_state_note()
        if note:
            text += "\n" + note
        return text

    def _show_first_run_notice(self):
        if self.data.get("first_run_done", False):
            return
        self.data.set("first_run_done", True)
        text = self._build_enc_text()
        if self.secure_store.get_mode():
            title = "凭据加密已启用"
            desc = text + "\n\n程序已自动选择合适的加密方式。\n如需更改，可从菜单  密码迁移 手动切换。"
        else:
            title = "首次使用提示"
            desc = "当前将使用：" + text + "\n\n程序尚未保存任何凭据。\n首次连接时填写账号密码并勾选「记住密码」，\n系统会按上述方式加密保存。"
        dlg = QDialog(self)
        dlg.setObjectName("Dlg")
        dlg.setWindowTitle(title)
        dlg.setModal(True)
        dlg.setMinimumWidth(480)
        v = QVBoxLayout(dlg)
        v.setContentsMargins(26, 24, 26, 20)
        v.setSpacing(14)
        i = QLabel("🔐")
        i.setObjectName("DlgIcon")
        i.setAlignment(Qt.AlignCenter)
        v.addWidget(i)
        t = QLabel(title)
        t.setObjectName("DlgTitle")
        t.setAlignment(Qt.AlignCenter)
        v.addWidget(t)
        d = QLabel(desc)
        d.setObjectName("DlgDesc")
        d.setAlignment(Qt.AlignCenter)
        d.setWordWrap(True)
        v.addWidget(d)
        v.addSpacing(8)
        b = QHBoxLayout()
        b.addStretch()
        ok = QPushButton("我知道了")
        ok.setObjectName("DlgPrimary")
        ok.setFixedSize(120, 38)
        ok.setCursor(Qt.PointingHandCursor)
        ok.clicked.connect(dlg.accept)
        b.addWidget(ok)
        b.addStretch()
        v.addLayout(b)
        dlg.exec()

    def _nav_btn(self, text, tip):
        b = QPushButton(text)
        b.setObjectName("Nav")
        b.setToolTip(tip)
        b.setFixedSize(28, 30)  # 放入标题栏，尺寸紧凑
        b.setCursor(Qt.PointingHandCursor)
        b.setFocusPolicy(Qt.NoFocus)
        return b

    def _restore_credentials(self):
        m = self.secure_store.get_mode()
        if not m:
            return
        if m in ("tpm", "fingerprint"):
            c = self.secure_store.load()
            if c:
                self._fill_welcome(c)
            return
        if m == "password":
            user = self.secure_store.get_saved_username() or SecureStore.current_username()
            for i in range(3):
                dlg = WindowsPasswordDialog(user, self, retry=(i > 0))
                if dlg.exec() != QDialog.Accepted:
                    return
                if not SecureStore.verify_windows_password(user, dlg.password):
                    if i < 2:
                        continue
                    QMessageBox.warning(self, "提示", "密码错误次数过多")
                    return
                c = self.secure_store.load(password=dlg.password)
                if c:
                    self._session_password = dlg.password
                    self._fill_welcome(c)
                return
        if m in ("custom", "pin"):
            title = "自定义密码" if m == "custom" else "PIN 码"
            for i in range(3):
                dlg = GenericPasswordDialog("解锁凭据" if i == 0 else "密码错误，请重试",
                                             f"输入{title}以解密已保存的凭据。", placeholder=title, parent=self)
                if dlg.exec() != QDialog.Accepted:
                    return
                c = self.secure_store.load(password=dlg.password)
                if c:
                    self._session_password = dlg.password
                    self._fill_welcome(c)
                    return
                if i < 2:
                    QMessageBox.warning(self, "提示", f"{title}错误")

    def _fill_welcome(self, c):
        self.welcome_page.fill(url=c.get("last_url", ""), user=c.get("username", ""),
                                password=c.get("password", ""), remember=c.get("remember", True))

    def _save_credentials(self, url, user, password, remember):
        # 记住密码偏好持久化到 data.json，下次启动按此预勾选
        self.data.set("remember_password", bool(remember))
        if not remember:
            self.secure_store.clear()
            self._refresh_enc_label()
            return
        if not user and not password:
            return
        data = {"last_url": url, "username": user, "password": password, "remember": True}

        if self.secure_store.mode == "auto":
            actual = self.secure_store.effective_mode()
            self.secure_store.mode = actual
            if actual in ("tpm", "fingerprint"):
                self.secure_store.save(data)
            elif actual == "password":
                u = SecureStore.current_username()
                for i in range(3):
                    dlg = WindowsPasswordDialog(u, self, retry=(i > 0))
                    if dlg.exec() != QDialog.Accepted:
                        return
                    if SecureStore.verify_windows_password(u, dlg.password):
                        self.secure_store.save(data, password=dlg.password)
                        self._session_password = dlg.password
                        break
                    if i < 2:
                        QMessageBox.warning(self, "提示", "Windows 密码错误")
                else:
                    return
        else:
            actual = self.secure_store.mode
            if actual in ("tpm", "fingerprint"):
                self.secure_store.save(data)
            else:
                pwd = self._session_password
                if not pwd:
                    if actual == "password":
                        u = SecureStore.current_username()
                        dlg = WindowsPasswordDialog(u, self)
                        if dlg.exec() != QDialog.Accepted:
                            return
                        if not SecureStore.verify_windows_password(u, dlg.password):
                            QMessageBox.warning(self, "提示", "Windows 密码错误")
                            return
                        pwd = dlg.password
                    else:
                        tt = "自定义密码" if actual == "custom" else "PIN 码"
                        dlg = GenericPasswordDialog(tt, "用于保护已保存的凭据。", placeholder=tt, parent=self)
                        if dlg.exec() != QDialog.Accepted:
                            return
                        pwd = dlg.password
                    self._session_password = pwd
                self.secure_store.save(data, password=pwd)
        self._refresh_enc_label()

    def _is_autofill_allowed(self):
        """自动填充只允许发生在「用户输入并连接的那个地址」页面上。
        规则：当前页面 host+port 与用户输入的目标地址完全一致才填充；
        只有飞牛官方域名（*.fnos.net / *.5ddd.com）允许子域放宽。
        其他任何网站、任何不同端口一律不填充（凭据泄漏防护）。"""
        target = (self.data.get("url") or "").strip()
        cur = self.web_view.url()
        if not target or not cur.isValid():
            return False
        try:
            tu = QUrl(target)
            th = (tu.host() or "").lower()
            tp = tu.port()
            ch = (cur.host() or "").lower()
            cp = cur.port()
        except Exception:
            return False
        if not th or not ch:
            return False
        # host 相同：端口也须一致（或未指定端口）才允许
        if th == ch:
            return tp == cp or tp == -1 or cp == -1
        # 子域放宽仅限飞牛官方域名
        if th.endswith(".fnos.net") or th.endswith(".5ddd.com"):
            return ch.endswith("." + th)
        return False

    def _inject_credentials(self):
        if not self._is_autofill_allowed():
            return
        c = self.secure_store.load(password=self._session_password)
        if not c:
            return
        user = c.get("username", "")
        pwd = c.get("password", "")
        if not user and not pwd:
            return
        import sys as _sys
        if getattr(_sys, "frozen", False):
            _base = Path(_sys._MEIPASS)
        else:
            _base = Path(__file__).parent
        js_path = _base / "inject.js"
        if not js_path.exists():
            return
        js = js_path.read_text(encoding="utf-8")
        # 把目标 NAS host 白名单传给 JS，JS 层再做一次域名校验（双保险）
        import json as _json
        _th = QUrl(self.data.get("url", "")).host() or ""
        js = "window.__fnosAllowedHosts = " + _json.dumps([_th]) + ";\n" + js
        self.web_view.page().runJavaScript(js)

    def _on_focus_ready(self):
        """网页用户名框已聚焦，开始键盘输入（仅限目标 NAS 域，防凭据泄漏）"""
        if not self._is_autofill_allowed():
            return
        c = self.secure_store.load(password=self._session_password)
        if not c:
            return
        user = c.get("username", "")
        pwd = c.get("password", "")
        if not user and not pwd:
            return
        # 确保窗口在前台
        QTimer.singleShot(250, lambda: self._type_via_keyboard(user, pwd))

    def _type_via_keyboard(self, user, pwd):
        # 打字前最后一道闸：当前页面必须仍是目标 NAS 域，防止任何时序/漏网导致密码外泄
        if not self._is_autofill_allowed():
            return
        try:
            self.web_view.setFocus()
            QTimer.singleShot(80, lambda: self._type_step1(user, pwd))
        except Exception as e:
            print("键盘输入失败:", e)

    def _type_step1(self, user, pwd):
        try:
            keyboard_sim.send_text(self.web_view, user, delay_ms=35)
            QTimer.singleShot(80, lambda: self._type_step2(pwd))
        except Exception as e:
            print("用户名输入失败:", e)

    def _type_step2(self, pwd):
        try:
            keyboard_sim.send_tab(self.web_view)
            QTimer.singleShot(80, lambda: self._type_step3(pwd))
        except Exception as e:
            print("切换失败:", e)

    def _type_step3(self, pwd):
        try:
            keyboard_sim.send_text(self.web_view, pwd, delay_ms=35)
        except Exception as e:
            print("密码输入失败:", e)


    def _normalize(self, url):
        url = url.strip()
        if not url:
            return ""
        if url.startswith(("http://", "https://")):
            return url
        if ".fnos.net" in url or ".5ddd.com" in url:
            return "https://" + url
        if "." not in url and ":" not in url:
            return f"https://{url}.5ddd.com"
        return "https://" + url

    def _on_connect(self, addr, user, password, remember):
        url = self._normalize(addr)
        if not url:
            return
        self.data.set("url", url)
        self._save_credentials(url, user, password, remember)
        self._start_loading(url)
        self.web_view.setUrl(QUrl(url))

    def _on_search(self, fnid):
        self._start_loading(f"正在搜索 {fnid}...")
        self.loading_page.set_message(f"正在搜索 FN ID: {fnid}", "同时尝试 5ddd.com 和 fnos.net")
        self._worker = SearchWorker(fnid)
        self._worker.done.connect(self._on_search_done)
        self._worker.start()

    def _on_search_done(self, results):
        self._load_timer.stop()
        self.progress.hide()
        if not results:
            self.error_page.set_error("未找到设备", "在两个域名中都没有找到该 FN ID 对应的设备",
                                       " 检查 FN ID 是否输入正确\n 确认 NAS 已开机并开启 FN Connect\n 确认 NAS 已连接到互联网")
            self.stack.setCurrentWidget(self.error_page)
            return
        if len(results) == 1:
            self._on_connect(results[0]["url"], self.welcome_page.user_input.text(),
                             self.welcome_page.pass_input.text(), self.welcome_page.remember_cb.isChecked())
            return
        dlg = SearchResultDialog(results, self)
        if dlg.exec() == QDialog.Accepted:
            p = dlg.picked_url()
            if p:
                self._on_connect(p, self.welcome_page.user_input.text(),
                                 self.welcome_page.pass_input.text(), self.welcome_page.remember_cb.isChecked())
        else:
            self._goto_welcome()

    def _retry(self):
        u = self.data.get("url", "")
        if u:
            self._start_loading(u)
            self.web_view.setUrl(QUrl(u))
        else:
            self._goto_welcome()

    def _go_home(self):
        u = self.data.get("url", "")
        if u:
            self._start_loading(u)
            self.web_view.setUrl(QUrl(u))
        else:
            self._goto_welcome()

    def _start_loading(self, url):
        self.loading_page.set_message(f"正在连接 {url}", "首次连接可能需要几秒")
        self.stack.setCurrentWidget(self.loading_page)
        self.progress.show()
        self.progress.setValue(0)
        self._load_timer.start(25000)

    def _back(self):
        # 标准浏览器行为：仅在存在历史时后退到上一页
        if self.web_view.history().canGoBack():
            self.web_view.back()

    def _forward(self):
        # 标准浏览器行为：仅在存在前进历史时前进
        if self.web_view.history().canGoForward():
            self.web_view.forward()

    def _refresh_nav_state(self):
        """刷新后退/前进按钮可用状态（标准浏览器行为）：
        后退：存在浏览器历史时可用；前进：存在前进历史时可用。"""
        self.back_btn.setEnabled(self.web_view.history().canGoBack())
        self.forward_btn.setEnabled(self.web_view.history().canGoForward())

    def _refresh(self):
        idx = self.stack.currentIndex()
        if idx == self.stack.indexOf(self.web_view):
            self.web_view.reload()
        elif idx == self.stack.indexOf(self.error_page):
            self._retry()

    def _toggle_max(self):
        if self.isMaximized():
            self.showNormal()
        else:
            self.showMaximized()
        self.title_bar.set_maximized(self.isMaximized())

    def _on_stack_changed(self, index):
        self._refresh_nav_state()

    def _goto_welcome(self):
        self.stack.setCurrentWidget(self.welcome_page)

    def _on_url_changed(self, url):
        self._refresh_nav_state()

    def _on_load_started(self):
        self.progress.show()
        self.progress.setValue(0)
        self._load_timer.start(25000)

    def _on_load_progress(self, v):
        self.progress.setValue(v)
        if 0 < v < 100:
            self.loading_page.set_message(self.loading_page.label.text(), f"加载中 {v}%")

    def _on_load_finished(self, ok):
        self._load_timer.stop()
        self.progress.hide()
        # 加载完成时浏览器历史才提交，此时刷新后退/前进按钮状态
        self._refresh_nav_state()
        if ok:
            self.stack.setCurrentWidget(self.web_view)
            t = self.web_view.title() or "飞牛桌面版"
            self.title_bar.set_title(f"{t}  飞牛桌面版")
            self.setWindowTitle(f"{t}  飞牛桌面版")
            QTimer.singleShot(800, self._inject_credentials)
        else:
            u = self.data.get("url", "")
            self.error_page.set_error("无法加载页面", f"无法连接到 {u}",
                                       " 地址或端口不正确\n NAS 未开机或不在同一网络\n 防火墙拦截了连接")
            self.stack.setCurrentWidget(self.error_page)

    def _on_load_timeout(self):
        if self.stack.currentWidget() is self.loading_page:
            self.progress.hide()
            u = self.data.get("url", "")
            self.error_page.set_error("连接超时", f"连接 {u} 超过 25 秒无响应",
                                       " NAS 未开机或网络不通\n IP 地址不正确\n 端口被防火墙拦截")
            self.stack.setCurrentWidget(self.error_page)

    def _on_title_changed(self, t):
        if self.stack.currentWidget() is self.web_view and t:
            self.title_bar.set_title(f"{t}  飞牛桌面版")

    def _show_menu(self):
        menu = QMenu(self)
        items = [
            ("🔧  更换飞牛地址", self._goto_welcome),
            ("🔄  重新加载", self._refresh),
            (None, None),
            ("💾  保存当前登录信息", self._open_save),
            ("🔐  密码迁移", self._open_migrate),
            ("📤  导出凭据", self._open_export),
            ("📥  导入凭据", self._open_import),
            ("🗑  清除已保存密码", self._clear_cred),
            (None, None),
            ("⚙️  设置", self._open_settings),
        ]
        for text, cb in items:
            if text is None:
                menu.addSeparator()
                continue
            a = QAction(text, self)
            a.triggered.connect(cb)
            menu.addAction(a)
        menu.addSeparator()
        # 颜色模式子菜单（浅色 / 深色 / 跟随系统，立即生效）
        theme_menu = menu.addMenu("🎨  颜色模式")
        cur = normalize_theme_mode(self.data.get("theme"), self.data.get("auto_theme", False))
        for key, label in MODE_OPTIONS:
            a = QAction(label, self)
            a.setCheckable(True)
            a.setChecked(cur == key)
            a.triggered.connect(lambda _=False, k=key: self._set_theme(k))
            theme_menu.addAction(a)
        menu.addSeparator()
        q = QAction("退出", self)
        q.triggered.connect(self.close)
        menu.addAction(q)
        pos = self.menu_btn.mapToGlobal(self.menu_btn.rect().bottomLeft())
        menu.exec(pos)

    def _set_theme(self, key):
        """切换颜色模式：浅色 / 深色 / 跟随系统，立即生效并持久化。

        自定义主题色（custom_accent）与深/浅模式背景（custom_bg_dark/light）
        随模式一起应用。
        """
        if key not in [k for k, _ in MODE_OPTIONS]:
            key = normalize_theme_mode(self.data.get("theme"), self.data.get("auto_theme", False))
        self.data.set("theme", key)
        if key != "auto":
            self.data.set("auto_theme", False)
        theme = effective_theme(QApplication.instance(), key)
        apply_theme(QApplication.instance(), theme,
                    self.data.get("custom_accent"),
                    self.data.get("custom_bg_dark") or self.data.get("custom_bg"),
                    self.data.get("custom_bg_light"))
        self.title_bar.refresh_icons()

    def _on_color_scheme_changed(self, _scheme):
        """系统深浅色变化时，若处于“跟随系统”模式则自动切换。"""
        if self.data.get("theme") == "auto" or self.data.get("auto_theme", False):
            self._set_theme("auto")

    @staticmethod
    def _app_icon_path():
        """应用图标路径：打包后取 exe 旁 assets/，源码运行取项目 assets/"""
        import sys as _sys
        if getattr(_sys, "frozen", False):
            base = Path(_sys.executable).resolve().parent
        else:
            base = Path(__file__).resolve().parent
        for name in ("app.ico", "app.png"):
            p = base / "assets" / name
            if p.exists():
                return str(p)
        return ""

    def _open_settings(self):
        SettingsDialog(self).exec()

    def _open_save(self):
        dlg = SaveCredentialDialog(url=self.data.get("url", ""), parent=self)
        if dlg.exec() != QDialog.Accepted:
            return
        if not dlg.username() or not dlg.password():
            QMessageBox.warning(self, "提示", "用户名和密码都不能为空")
            return
        self._save_credentials(self.data.get("url", ""), dlg.username(), dlg.password(), True)
        QMessageBox.information(self, "成功", f"已保存！\n加密方式：{self.secure_store.encryption_label()}")

    def _open_migrate(self):
        MigrateDialog(self.secure_store, self).exec()
        self._refresh_enc_label()

    def _open_export(self):
        c = self.secure_store.load(password=self._session_password)
        if not c:
            QMessageBox.warning(self, "提示", "当前没有可导出的凭据")
            return
        ExportDialog(self.secure_store, c, self).exec()

    def _open_import(self):
        dlg = ImportDialog(self.secure_store, self)
        if dlg.exec() and dlg.imported:
            self._fill_welcome(dlg.imported)

    def _clear_cred(self):
        self.secure_store.clear()
        self._session_password = ""
        self.welcome_page.user_input.clear()
        self.welcome_page.pass_input.clear()
        self._refresh_enc_label()

    def changeEvent(self, e):
        super().changeEvent(e)
        # 窗口最大化/还原（含拖拽到屏幕顶部触发的系统行为）时同步切换按钮图标
        if e.type() == QEvent.WindowStateChange:
            self.title_bar.set_maximized(self.isMaximized())

    def _layout_edges(self):
        """把 8 条拖拽热区贴到 Root 四边（与内容 8px 边距区重合，不侵入内容区）"""
        r = self.centralWidget()
        if r is None:
            return
        W, H = r.width(), r.height()
        C, E = 8, 8  # 角尺寸、边厚度（与边距一致，保证不遮挡网页内容）
        geo = {
            "lt": (0, 0, C, C), "rt": (W - C, 0, C, C),
            "lb": (0, H - C, C, C), "rb": (W - C, H - C, C, C),
            "l": (0, C, E, H - 2 * C), "r": (W - E, C, E, H - 2 * C),
            "t": (C, 0, W - 2 * C, E), "b": (C, H - E, W - 2 * C, E),
        }
        for k, g in geo.items():
            if k in self._edges:
                self._edges[k].setGeometry(*g)

    def resizeEvent(self, e):
        super().resizeEvent(e)
        # 拖拽热区始终贴边
        if getattr(self, "_edges", None):
            self._layout_edges()

    def showEvent(self, e):
        super().showEvent(e)
        if getattr(self, "_edges", None):
            self._layout_edges()

    def closeEvent(self, e):
        self._load_timer.stop()
        super().closeEvent(e)



