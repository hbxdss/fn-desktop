QSS = """
* { font-family: "Microsoft YaHei UI", "Segoe UI", sans-serif; font-size: 13px; color: #e2e8f0; }
QMainWindow, QWidget#Root { background: #15171e; border: 1px solid #2a2e38; }
QWidget#TitleBar { background: #1f222a; border-bottom: 1px solid #2a2e38; }
QLabel#TitleText { color: #e2e8f0; font-size: 13px; font-weight: 600; }
QPushButton#WinBtn { background: transparent; border: none; color: #94a3b8; font-size: 14px; border-radius: 0; }
QPushButton#WinBtn:hover { background: #2a2e38; color: #e2e8f0; }
QPushButton#CloseBtn { background: transparent; border: none; color: #94a3b8; font-size: 14px; border-radius: 0; }
QPushButton#CloseBtn:hover { background: #ef4444; color: #ffffff; }
QWidget#Bar { background: #1f222a; border-bottom: 1px solid #2a2e38; }
QPushButton#Nav { background: transparent; border: none; color: #94a3b8; border-radius: 8px; font-size: 15px; }
QPushButton#Nav:hover { background: #2a2e38; color: #10b981; }
QPushButton#Nav:disabled { color: #475569; }
QPushButton#MenuBtn { background: transparent; border: none; color: #94a3b8; border-radius: 8px; font-size: 15px; }
QPushButton#MenuBtn:hover { background: #2a2e38; color: #10b981; }
QProgressBar { background: transparent; border: none; }
QProgressBar::chunk { background: #10b981; }
QWidget#Page { background: #15171e; }
QLabel#BigTitle { color: #f1f5f9; font-size: 28px; font-weight: 700; }
QLabel#SubTitle { color: #94a3b8; font-size: 14px; }
QLabel#Hint { color: #64748b; font-size: 12px; }
QLabel#CardTitle { color: #e2e8f0; font-size: 13px; font-weight: 600; }
QLineEdit, QComboBox { background: #1f222a; border: 1px solid #2a2e38; border-radius: 10px; padding: 0 14px; color: #e2e8f0; font-size: 14px; min-height: 38px; selection-background-color: #10b981; }
QLineEdit:focus, QComboBox:focus { border: 1px solid #10b981; }
QLineEdit#BigInput { font-size: 15px; min-height: 46px; padding: 0 18px; border-radius: 12px; }
QPushButton#BigBtn { background: #10b981; color: white; border: none; border-radius: 12px; font-size: 14px; font-weight: 600; min-height: 42px; padding: 0 24px; }
QPushButton#BigBtn:hover { background: #059669; }
QPushButton#GhostBtn { background: transparent; color: #94a3b8; border: 1px solid #2a2e38; border-radius: 12px; font-size: 13px; min-height: 42px; padding: 0 24px; }
QPushButton#GhostBtn:hover { background: #1f222a; color: #f1f5f9; }
QCheckBox { color: #94a3b8; font-size: 13px; spacing: 8px; }
QCheckBox::indicator { width: 16px; height: 16px; border-radius: 4px; border: 1px solid #2a2e38; background: #1f222a; }
QCheckBox::indicator:checked { background: #10b981; border: 1px solid #10b981; }
QLabel#ErrIcon { font-size: 48px; }
QLabel#ErrTitle { color: #e2e8f0; font-size: 18px; font-weight: 600; }
QLabel#ErrMsg { color: #94a3b8; font-size: 13px; }
QLabel#ErrDetail { color: #64748b; font-size: 12px; background: #1f222a; border: 1px solid #2a2e38; border-radius: 10px; padding: 14px; }
QDialog#Dlg { background: #15171e; }
QLabel#DlgTitle { color: #e2e8f0; font-size: 16px; font-weight: 600; }
QLabel#DlgIcon { font-size: 36px; }
QLabel#DlgDesc { color: #94a3b8; font-size: 13px; }
QLabel#CertSection { color: #64748b; font-size: 12px; font-weight: 600; }
QLabel#CertFieldKey { color: #64748b; font-size: 12px; }
QLabel#CertFieldVal { color: #e2e8f0; font-size: 12px; }
QLabel#CertFp { color: #10b981; font-family: "Consolas", monospace; font-size: 11px; background: #1f222a; border: 1px solid #2a2e38; border-radius: 10px; padding: 10px 12px; }
QPushButton#DlgCancel { background: transparent; color: #94a3b8; border: 1px solid #2a2e38; border-radius: 10px; font-size: 13px; min-height: 38px; padding: 0 20px; }
QPushButton#DlgCancel:hover { background: #1f222a; color: #e2e8f0; }
QPushButton#DlgPrimary { background: #10b981; color: white; border: none; border-radius: 10px; font-size: 13px; font-weight: 600; min-height: 38px; padding: 0 20px; }
QPushButton#DlgPrimary:hover { background: #059669; }
QPushButton#DlgDanger { background: #ef4444; color: white; border: none; border-radius: 10px; font-size: 13px; font-weight: 600; min-height: 38px; padding: 0 20px; }
QPushButton#DlgDanger:hover { background: #dc2626; }
QListWidget { background: transparent; border: none; outline: none; }
QListWidget::item { background: #1f222a; border: 1px solid #2a2e38; border-radius: 10px; padding: 12px 16px; margin-bottom: 8px; color: #e2e8f0; }
QListWidget::item:hover { border-color: #10b981; background: #1a2a24; }
QListWidget::item:selected { background: #16302a; border-color: #10b981; color: #e2e8f0; }
QMenu { background: #1f222a; border: 1px solid #2a2e38; border-radius: 10px; padding: 6px; }
QMenu::item { padding: 7px 24px 7px 14px; border-radius: 6px; color: #e2e8f0; }
QMenu::item:selected { background: #10b981; color: #ffffff; }
QScrollBar:vertical { background: transparent; width: 8px; }
QScrollBar::handle:vertical { background: #2a2e38; border-radius: 4px; min-height: 30px; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }

QComboBox QAbstractItemView {
    background: #1f222a;
    border: 1px solid #2a2e38;
    border-radius: 8px;
    color: #e2e8f0;
    outline: none;
    padding: 4px;
    selection-background-color: #10b981;
    selection-color: #ffffff;
}
QComboBox QAbstractItemView::item {
    min-height: 30px;
    padding: 0 12px;
    border-radius: 6px;
}
QComboBox QAbstractItemView::item:hover {
    background: #2a2e38;
    color: #10b981;
}
QComboBox QAbstractItemView::item:selected {
    background: #10b981;
    color: #ffffff;
}
"""